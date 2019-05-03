# Copyright (c) 2019-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

from __future__ import absolute_import, division, print_function, unicode_literals

import errno
import os
import subprocess
import tempfile

from .platform import HostType, is_windows


def containing_repo_type(path):
    while True:
        if os.path.exists(os.path.join(path, ".git")):
            return ("git", path)
        if os.path.exists(os.path.join(path, ".hg")):
            return ("hg", path)

        parent = os.path.dirname(path)
        if parent == path:
            return None
        path = parent


class BuildOptions(object):
    def __init__(
        self,
        fbcode_builder_dir,
        scratch_dir,
        host_type,
        install_dir=None,
        num_jobs=0,
        use_shipit=False,
    ):
        """ fbcode_builder_dir - the path to either the in-fbsource fbcode_builder dir,
                                 or for shipit-transformed repos, the build dir that
                                 has been mapped into that dir.
            scratch_dir - a place where we can store repos and build bits.
                          This path should be stable across runs and ideally
                          should not be in the repo of the project being built,
                          but that is ultimately where we generally fall back
                          for builds outside of FB
            install_dir - where the project will ultimately be installed
            num_jobs - the level of concurrency to use while building
            use_shipit - use real shipit instead of the simple shipit transformer
        """
        if not num_jobs:
            import multiprocessing

            num_jobs = multiprocessing.cpu_count()

        if not install_dir:
            install_dir = os.path.join(scratch_dir, "install")

        self.project_hashes = None
        for p in ["../deps/github_hashes", "../project_hashes"]:
            hashes = os.path.join(fbcode_builder_dir, p)
            if os.path.exists(hashes):
                self.project_hashes = hashes
                break

        # Use a simplistic heuristic to figure out if we're in fbsource
        # and where the root of fbsource can be found
        repo_type, repo_root = containing_repo_type(fbcode_builder_dir)
        if repo_type == "hg":
            self.fbsource_dir = repo_root
        else:
            self.fbsource_dir = None

        self.num_jobs = num_jobs
        self.scratch_dir = scratch_dir
        self.install_dir = install_dir
        self.fbcode_builder_dir = fbcode_builder_dir
        self.host_type = host_type
        self.use_shipit = use_shipit

    def is_darwin(self):
        return self.host_type.is_darwin()

    def is_windows(self):
        return self.host_type.is_windows()

    def is_linux(self):
        return self.host_type.is_linux()

    def compute_dirs(self, manifest, fetcher):
        hash = fetcher.hash()
        directory = "%s-%s" % (manifest.name, hash)
        build_dir = os.path.join(self.scratch_dir, "build", directory)
        inst_dir = os.path.join(self.scratch_dir, "installed", directory)

        return {"build_dir": build_dir, "inst_dir": inst_dir, "hash": hash}


def list_win32_subst_letters():
    output = subprocess.check_output(["subst"]).decode("utf-8")
    # The output is a set of lines like: `F:\: => C:\open\some\where`
    lines = output.strip().split("\r\n")
    mapping = {}
    for line in lines:
        fields = line.split(": => ")
        if len(fields) != 2:
            continue
        letter = fields[0]
        path = fields[1]
        mapping[letter] = path

    return mapping


def find_existing_win32_subst_for_path(path):
    path = os.path.normpath(path)
    mapping = list_win32_subst_letters()
    for letter, target in mapping.items():
        if target == path:
            return letter
    return None


def find_unused_drive_letter():
    import ctypes

    buffer_len = 256
    blen = ctypes.c_uint(buffer_len)
    rv = ctypes.c_uint()
    bufs = ctypes.create_string_buffer(buffer_len)
    rv = ctypes.windll.kernel32.GetLogicalDriveStringsA(blen, bufs)
    if rv > buffer_len:
        raise Exception("GetLogicalDriveStringsA result too large for buffer")
    nul = "\x00".encode("ascii")

    used = [drive.decode("ascii")[0] for drive in bufs.raw.strip(nul).split(nul)]
    possible = [c for c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ"]
    available = sorted(list(set(possible) - set(used)))
    if len(available) == 0:
        return None
    # Prefer to assign later letters rather than earlier letters
    return available[-1]


def create_subst_path(path):
    for _attempt in range(0, 24):
        drive = find_existing_win32_subst_for_path(path)
        if drive:
            return drive
        available = find_unused_drive_letter()
        if available is None:
            raise Exception(
                (
                    "unable to make shorter subst mapping for %s; "
                    "no available drive letters"
                )
                % path
            )

        # Try to set up a subst mapping; note that we may be racing with
        # other processes on the same host, so this may not succeed.
        try:
            subprocess.check_call(["subst", "%s:" % available, path])
            return "%s:\\" % available
        except Exception:
            print("Failed to map %s -> %s" % (available, path))

    raise Exception("failed to set up a subst path for %s" % path)


def _check_host_type(args, host_type):
    if host_type is None:
        host_tuple_string = getattr(args, "host_type", None)
        if host_tuple_string:
            host_type = HostType.from_tuple_string(host_tuple_string)
        else:
            host_type = HostType()

    assert isinstance(host_type, HostType)
    return host_type


def setup_build_options(args, host_type=None):
    """ Create a BuildOptions object based on the arguments """

    fbcode_builder_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    scratch_dir = args.scratch_path
    if not scratch_dir:
        # TODO: `mkscratch` doesn't currently know how best to place things on
        # sandcastle, so whip up something reasonable-ish
        if "SANDCASTLE" in os.environ:
            if "DISK_TEMP" not in os.environ:
                raise Exception(
                    (
                        "I need DISK_TEMP to be set in the sandcastle environment "
                        "so that I can store build products somewhere sane"
                    )
                )
            scratch_dir = os.path.join(
                os.environ["DISK_TEMP"], "fbcode_builder_getdeps"
            )
        if not scratch_dir:
            try:
                scratch_dir = (
                    subprocess.check_output(
                        ["mkscratch", "path", "--subdir", "fbcode_builder_getdeps"]
                    )
                    .strip()
                    .decode("utf-8")
                )
            except OSError as exc:
                if exc.errno != errno.ENOENT:
                    # A legit failure; don't fall back, surface the error
                    raise
                # This system doesn't have mkscratch so we fall back to
                # something local.
                munged = fbcode_builder_dir.replace("Z", "zZ")
                for s in ["/", "\\", ":"]:
                    munged = munged.replace(s, "Z")
                scratch_dir = os.path.join(
                    tempfile.gettempdir(), "fbcode_builder_getdeps-%s" % munged
                )

        if not os.path.exists(scratch_dir):
            os.makedirs(scratch_dir)

        if is_windows():
            subst = create_subst_path(scratch_dir)
            print("Mapping scratch dir %s -> %s" % (scratch_dir, subst))
            scratch_dir = subst

    host_type = _check_host_type(args, host_type)

    return BuildOptions(
        fbcode_builder_dir,
        scratch_dir,
        host_type,
        install_dir=args.install_prefix,
        num_jobs=args.num_jobs,
        use_shipit=args.use_shipit,
    )
