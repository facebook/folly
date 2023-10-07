# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import errno
import glob
import ntpath
import os
import subprocess
import sys
import tempfile
from typing import Mapping, Optional

from .copytree import containing_repo_type
from .envfuncs import add_flag, add_path_entry, Env
from .fetcher import get_fbsource_repo_data, homebrew_package_prefix
from .manifest import ContextGenerator
from .platform import get_available_ram, HostType, is_windows


def detect_project(path):
    repo_type, repo_root = containing_repo_type(path)
    if repo_type is None:
        return None, None

    # Look for a .projectid file.  If it exists, read the project name from it.
    project_id_path = os.path.join(repo_root, ".projectid")
    try:
        with open(project_id_path, "r") as f:
            project_name = f.read().strip()
            return repo_root, project_name
    except EnvironmentError as ex:
        if ex.errno != errno.ENOENT:
            raise

    return repo_root, None


class BuildOptions(object):
    def __init__(
        self,
        fbcode_builder_dir,
        scratch_dir,
        host_type,
        install_dir=None,
        num_jobs: int = 0,
        use_shipit: bool = False,
        vcvars_path=None,
        allow_system_packages: bool = False,
        lfs_path=None,
        shared_libs: bool = False,
        facebook_internal=None,
        free_up_disk: bool = False,
    ) -> None:
        """fbcode_builder_dir - the path to either the in-fbsource fbcode_builder dir,
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
        vcvars_path - Path to external VS toolchain's vsvarsall.bat
        shared_libs - whether to build shared libraries
        free_up_disk - take extra actions to save runner disk space
        """

        if not install_dir:
            install_dir = os.path.join(scratch_dir, "installed")

        self.project_hashes = None
        for p in ["../deps/github_hashes", "../project_hashes"]:
            hashes = os.path.join(fbcode_builder_dir, p)
            if os.path.exists(hashes):
                self.project_hashes = hashes
                break

        # Detect what repository and project we are being run from.
        self.repo_root, self.repo_project = detect_project(os.getcwd())

        # If we are running from an fbsource repository, set self.fbsource_dir
        # to allow the ShipIt-based fetchers to use it.
        if self.repo_project == "fbsource":
            self.fbsource_dir: Optional[str] = self.repo_root
        else:
            self.fbsource_dir = None

        if facebook_internal is None:
            if self.fbsource_dir:
                facebook_internal = True
            else:
                facebook_internal = False

        self.facebook_internal = facebook_internal
        self.specified_num_jobs = num_jobs
        self.scratch_dir = scratch_dir
        self.install_dir = install_dir
        self.fbcode_builder_dir = fbcode_builder_dir
        self.host_type = host_type
        self.use_shipit = use_shipit
        self.allow_system_packages = allow_system_packages
        self.lfs_path = lfs_path
        self.shared_libs = shared_libs
        self.free_up_disk = free_up_disk

        lib_path = None
        if self.is_darwin():
            lib_path = "DYLD_LIBRARY_PATH"
        elif self.is_linux():
            lib_path = "LD_LIBRARY_PATH"
        elif self.is_windows():
            lib_path = "PATH"
        else:
            lib_path = None
        self.lib_path = lib_path

        if vcvars_path is None and is_windows():

            try:
                # Allow a site-specific vcvarsall path.
                from .facebook.vcvarsall import build_default_vcvarsall
            except ImportError:
                vcvarsall = []
            else:
                vcvarsall = (
                    build_default_vcvarsall(self.fbsource_dir)
                    if self.fbsource_dir is not None
                    else []
                )

            # On Windows, the compiler is not available in the PATH by
            # default so we need to run the vcvarsall script to populate the
            # environment. We use a glob to find some version of this script
            # as deployed with Visual Studio 2017.  This logic can also
            # locate Visual Studio 2019 but note that at the time of writing
            # the version of boost in our manifest cannot be built with
            # VS 2019, so we're effectively tied to VS 2017 until we upgrade
            # the boost dependency.
            for year in ["2017", "2019"]:
                vcvarsall += glob.glob(
                    os.path.join(
                        os.environ["ProgramFiles(x86)"],
                        "Microsoft Visual Studio",
                        year,
                        "*",
                        "VC",
                        "Auxiliary",
                        "Build",
                        "vcvarsall.bat",
                    )
                )
            vcvars_path = vcvarsall[0]

        self.vcvars_path = vcvars_path

    @property
    def manifests_dir(self):
        return os.path.join(self.fbcode_builder_dir, "manifests")

    def is_darwin(self):
        return self.host_type.is_darwin()

    def is_windows(self):
        return self.host_type.is_windows()

    def is_arm(self):
        return self.host_type.is_arm()

    def get_vcvars_path(self):
        return self.vcvars_path

    def is_linux(self):
        return self.host_type.is_linux()

    def is_freebsd(self):
        return self.host_type.is_freebsd()

    def get_num_jobs(self, job_weight: int) -> int:
        """Given an estimated job_weight in MiB, compute a reasonable concurrency limit."""
        if self.specified_num_jobs:
            return self.specified_num_jobs

        available_ram = get_available_ram()

        import multiprocessing

        return max(1, min(multiprocessing.cpu_count(), available_ram // job_weight))

    def get_context_generator(self, host_tuple=None):
        """Create a manifest ContextGenerator for the specified target platform."""
        if host_tuple is None:
            host_type = self.host_type
        elif isinstance(host_tuple, HostType):
            host_type = host_tuple
        else:
            host_type = HostType.from_tuple_string(host_tuple)

        return ContextGenerator(
            {
                "os": host_type.ostype,
                "distro": host_type.distro,
                "distro_vers": host_type.distrovers,
                "fb": "on" if self.facebook_internal else "off",
                "fbsource": "on" if self.fbsource_dir else "off",
                "test": "off",
                "shared_libs": "on" if self.shared_libs else "off",
            }
        )

    def compute_env_for_install_dirs(
        self, install_dirs, env=None, manifest=None
    ):  # noqa: C901
        if env is not None:
            env = env.copy()
        else:
            env = Env()

        env["GETDEPS_BUILD_DIR"] = os.path.join(self.scratch_dir, "build")
        env["GETDEPS_INSTALL_DIR"] = self.install_dir

        # Python setuptools attempts to discover a local MSVC for
        # building Python extensions. On Windows, getdeps already
        # supports invoking a vcvarsall prior to compilation.
        #
        # Tell setuptools to bypass its own search. This fixes a bug
        # where setuptools would fail when run from CMake on GitHub
        # Actions with the inscrutable message 'error: Microsoft
        # Visual C++ 14.0 is required. Get it with "Build Tools for
        # Visual Studio"'. I suspect the actual error is that the
        # environment or PATH is overflowing.
        #
        # For extra credit, someone could patch setuptools to
        # propagate the actual error message from vcvarsall, because
        # often it does not mean Visual C++ is not available.
        #
        # Related discussions:
        # - https://github.com/pypa/setuptools/issues/2028
        # - https://github.com/pypa/setuptools/issues/2307
        # - https://developercommunity.visualstudio.com/t/error-microsoft-visual-c-140-is-required/409173
        # - https://github.com/OpenMS/OpenMS/pull/4779
        # - https://github.com/actions/virtual-environments/issues/1484

        if self.is_windows() and self.get_vcvars_path():
            env["DISTUTILS_USE_SDK"] = "1"

        # On macOS we need to set `SDKROOT` when we use clang for system
        # header files.
        if self.is_darwin() and "SDKROOT" not in env:
            sdkroot = subprocess.check_output(["xcrun", "--show-sdk-path"])
            env["SDKROOT"] = sdkroot.decode().strip()

        if (
            self.is_darwin()
            and self.allow_system_packages
            and self.host_type.get_package_manager() == "homebrew"
            and manifest
            and manifest.resolved_system_packages
        ):
            # Homebrew packages may not be on the default PATHs
            brew_packages = manifest.resolved_system_packages.get("homebrew", [])
            for p in brew_packages:
                found = self.add_homebrew_package_to_env(p, env)
                # Try extra hard to find openssl, needed with homebrew on macOS
                if found and p.startswith("openssl"):
                    candidate = homebrew_package_prefix("openssl@1.1")
                    if os.path.exists(candidate):
                        os.environ["OPENSSL_ROOT_DIR"] = candidate
                        env["OPENSSL_ROOT_DIR"] = os.environ["OPENSSL_ROOT_DIR"]

        if self.fbsource_dir:
            env["YARN_YARN_OFFLINE_MIRROR"] = os.path.join(
                self.fbsource_dir, "xplat/third-party/yarn/offline-mirror"
            )
            yarn_exe = "yarn.bat" if self.is_windows() else "yarn"
            env["YARN_PATH"] = os.path.join(
                self.fbsource_dir, "xplat/third-party/yarn/", yarn_exe
            )
            node_exe = "node-win-x64.exe" if self.is_windows() else "node"
            env["NODE_BIN"] = os.path.join(
                self.fbsource_dir, "xplat/third-party/node/bin/", node_exe
            )
            env["RUST_VENDORED_CRATES_DIR"] = os.path.join(
                self.fbsource_dir, "third-party/rust/vendor"
            )
            hash_data = get_fbsource_repo_data(self)
            env["FBSOURCE_HASH"] = hash_data.hash
            env["FBSOURCE_DATE"] = hash_data.date

        # reverse as we are prepending to the PATHs
        for d in reversed(install_dirs):
            self.add_prefix_to_env(d, env, append=False)

        # Linux is always system openssl
        system_openssl = self.is_linux()

        # For other systems lets see if package is requested
        if not system_openssl and manifest and manifest.resolved_system_packages:
            for _pkg_type, pkgs in manifest.resolved_system_packages.items():
                for p in pkgs:
                    if p.startswith("openssl") or p.startswith("libssl"):
                        system_openssl = True
                        break

        # Let openssl know to pick up the system certs if present
        if system_openssl or "OPENSSL_DIR" in env:
            for system_ssl_cfg in ["/etc/pki/tls", "/etc/ssl"]:
                if os.path.isdir(system_ssl_cfg):
                    cert_dir = system_ssl_cfg + "/certs"
                    if os.path.isdir(cert_dir):
                        env["SSL_CERT_DIR"] = cert_dir
                    cert_file = system_ssl_cfg + "/cert.pem"
                    if os.path.isfile(cert_file):
                        env["SSL_CERT_FILE"] = cert_file

        return env

    def add_homebrew_package_to_env(self, package, env) -> bool:
        prefix = homebrew_package_prefix(package)
        if prefix and os.path.exists(prefix):
            return self.add_prefix_to_env(
                prefix, env, append=False, add_library_path=True
            )
        return False

    def add_prefix_to_env(
        self, d, env, append: bool = True, add_library_path: bool = False
    ) -> bool:  # noqa: C901
        bindir = os.path.join(d, "bin")
        found = False
        pkgconfig = os.path.join(d, "lib", "pkgconfig")
        if os.path.exists(pkgconfig):
            found = True
            add_path_entry(env, "PKG_CONFIG_PATH", pkgconfig, append=append)

        pkgconfig = os.path.join(d, "lib64", "pkgconfig")
        if os.path.exists(pkgconfig):
            found = True
            add_path_entry(env, "PKG_CONFIG_PATH", pkgconfig, append=append)

        add_path_entry(env, "CMAKE_PREFIX_PATH", d, append=append)

        # Tell the thrift compiler about includes it needs to consider
        thriftdir = os.path.join(d, "include", "thrift-files")
        if os.path.exists(thriftdir):
            found = True
            add_path_entry(env, "THRIFT_INCLUDE_PATH", thriftdir, append=append)

        # module detection for python is old fashioned and needs flags
        includedir = os.path.join(d, "include")
        if os.path.exists(includedir):
            found = True
            ncursesincludedir = os.path.join(d, "include", "ncurses")
            if os.path.exists(ncursesincludedir):
                add_path_entry(env, "C_INCLUDE_PATH", ncursesincludedir, append=append)
                add_flag(env, "CPPFLAGS", f"-I{includedir}", append=append)
                add_flag(env, "CPPFLAGS", f"-I{ncursesincludedir}", append=append)
            elif "/bz2-" in d:
                add_flag(env, "CPPFLAGS", f"-I{includedir}", append=append)

        # Map from FB python manifests to PYTHONPATH
        pydir = os.path.join(d, "lib", "fb-py-libs")
        if os.path.exists(pydir):
            found = True
            manifest_ext = ".manifest"
            pymanifestfiles = [
                f
                for f in os.listdir(pydir)
                if f.endswith(manifest_ext) and os.path.isfile(os.path.join(pydir, f))
            ]
            for f in pymanifestfiles:
                subdir = f[: -len(manifest_ext)]
                add_path_entry(
                    env, "PYTHONPATH", os.path.join(pydir, subdir), append=append
                )

        # Allow resolving shared objects built earlier (eg: zstd
        # doesn't include the full path to the dylib in its linkage
        # so we need to give it an assist)
        if self.lib_path:
            for lib in ["lib", "lib64"]:
                libdir = os.path.join(d, lib)
                if os.path.exists(libdir):
                    found = True
                    add_path_entry(env, self.lib_path, libdir, append=append)
                    # module detection for python is old fashioned and needs flags
                    if "/ncurses-" in d:
                        add_flag(env, "LDFLAGS", f"-L{libdir}", append=append)
                    elif "/bz2-" in d:
                        add_flag(env, "LDFLAGS", f"-L{libdir}", append=append)
                    if add_library_path:
                        add_path_entry(env, "LIBRARY_PATH", libdir, append=append)

        # Allow resolving binaries (eg: cmake, ninja) and dlls
        # built by earlier steps
        if os.path.exists(bindir):
            found = True
            add_path_entry(env, "PATH", bindir, append=append)

        # If rustc is present in the `bin` directory, set RUSTC to prevent
        # cargo uses the rustc installed in the system.
        if self.is_windows():
            cargo_path = os.path.join(bindir, "cargo.exe")
            rustc_path = os.path.join(bindir, "rustc.exe")
            rustdoc_path = os.path.join(bindir, "rustdoc.exe")
        else:
            cargo_path = os.path.join(bindir, "cargo")
            rustc_path = os.path.join(bindir, "rustc")
            rustdoc_path = os.path.join(bindir, "rustdoc")

        if os.path.isfile(rustc_path):
            env["CARGO_BIN"] = cargo_path
            env["RUSTC"] = rustc_path
            env["RUSTDOC"] = rustdoc_path

        openssl_include = os.path.join(d, "include", "openssl")
        if os.path.isdir(openssl_include) and any(
            os.path.isfile(os.path.join(d, "lib", libcrypto))
            for libcrypto in ("libcrypto.lib", "libcrypto.so", "libcrypto.a")
        ):
            # This must be the openssl library, let Rust know about it
            env["OPENSSL_DIR"] = d

        return found


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


def find_existing_win32_subst_for_path(
    path: str,
    subst_mapping: Mapping[str, str],
) -> Optional[str]:
    path = ntpath.normcase(ntpath.normpath(path))
    for letter, target in subst_mapping.items():
        if ntpath.normcase(target) == path:
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


def create_subst_path(path: str) -> str:
    for _attempt in range(0, 24):
        drive = find_existing_win32_subst_for_path(
            path, subst_mapping=list_win32_subst_letters()
        )
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


def setup_build_options(args, host_type=None) -> BuildOptions:
    """Create a BuildOptions object based on the arguments"""

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

                if is_windows() and os.path.isdir("c:/open"):
                    temp = "c:/open/scratch"
                else:
                    temp = tempfile.gettempdir()

                scratch_dir = os.path.join(temp, "fbcode_builder_getdeps-%s" % munged)
                if not is_windows() and os.geteuid() == 0:
                    # Running as root; in the case where someone runs
                    # sudo getdeps.py install-system-deps
                    # and then runs as build without privs, we want to avoid creating
                    # a scratch dir that the second stage cannot write to.
                    # So we generate a different path if we are root.
                    scratch_dir += "-root"

        if not os.path.exists(scratch_dir):
            os.makedirs(scratch_dir)

        if is_windows():
            subst = create_subst_path(scratch_dir)
            print(
                "Mapping scratch dir %s -> %s" % (scratch_dir, subst), file=sys.stderr
            )
            scratch_dir = subst
    else:
        if not os.path.exists(scratch_dir):
            os.makedirs(scratch_dir)

    # Make sure we normalize the scratch path.  This path is used as part of the hash
    # computation for detecting if projects have been updated, so we need to always
    # use the exact same string to refer to a given directory.
    # But! realpath in some combinations of Windows/Python3 versions can expand the
    # drive substitutions on Windows, so avoid that!
    if not is_windows():
        scratch_dir = os.path.realpath(scratch_dir)

    # Save these args passed by the user in an env variable, so it
    # can be used while hashing this build.
    os.environ["GETDEPS_CMAKE_DEFINES"] = getattr(args, "extra_cmake_defines", "") or ""

    host_type = _check_host_type(args, host_type)

    build_args = {
        k: v
        for (k, v) in vars(args).items()
        if k
        in {
            "num_jobs",
            "use_shipit",
            "vcvars_path",
            "allow_system_packages",
            "lfs_path",
            "shared_libs",
            "free_up_disk",
        }
    }

    return BuildOptions(
        fbcode_builder_dir,
        scratch_dir,
        host_type,
        install_dir=args.install_prefix,
        facebook_internal=args.facebook_internal,
        **build_args,
    )
