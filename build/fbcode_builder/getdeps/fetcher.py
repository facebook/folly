# Copyright (c) Facebook, Inc. and its affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from __future__ import absolute_import, division, print_function, unicode_literals

import errno
import hashlib
import os
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import time
import zipfile

from .copytree import prefetch_dir_if_eden
from .envfuncs import Env
from .errors import TransientFailure
from .platform import is_windows
from .runcmd import run_cmd


try:
    from urlparse import urlparse
    from urllib import urlretrieve
except ImportError:
    from urllib.parse import urlparse
    from urllib.request import urlretrieve


class ChangeStatus(object):
    """ Indicates the nature of changes that happened while updating
    the source directory.  There are two broad uses:
    * When extracting archives for third party software we want to
      know that we did something (eg: we either extracted code or
      we didn't do anything)
    * For 1st party code where we use shipit to transform the code,
      we want to know if we changed anything so that we can perform
      a build, but we generally want to be a little more nuanced
      and be able to distinguish between just changing a source file
      and whether we might need to reconfigure the build system.
    """

    def __init__(self, all_changed=False):
        """ Construct a ChangeStatus object.  The default is to create
        a status that indicates no changes, but passing all_changed=True
        will create one that indicates that everything changed """
        if all_changed:
            self.source_files = 1
            self.make_files = 1
        else:
            self.source_files = 0
            self.make_files = 0

    def record_change(self, file_name):
        """ Used by the shipit fetcher to record changes as it updates
        files in the destination.  If the file name might be one used
        in the cmake build system that we use for 1st party code, then
        record that as a "make file" change.  We could broaden this
        to match any file used by various build systems, but it is
        only really useful for our internal cmake stuff at this time.
        If the file isn't a build file and is under the `fbcode_builder`
        dir then we don't class that as an interesting change that we
        might need to rebuild, so we ignore it.
        Otherwise we record the file as a source file change. """

        if "cmake" in file_name.lower():
            self.make_files += 1
            return
        if "/fbcode_builder/" in file_name:
            return
        self.source_files += 1

    def sources_changed(self):
        """ Returns true if any source files were changed during
        an update operation.  This will typically be used to decide
        that the build system to be run on the source dir in an
        incremental mode """
        return self.source_files > 0

    def build_changed(self):
        """ Returns true if any build files were changed during
        an update operation.  This will typically be used to decidfe
        that the build system should be reconfigured and re-run
        as a full build """
        return self.make_files > 0


class Fetcher(object):
    """ The Fetcher is responsible for fetching and extracting the
    sources for project.  The Fetcher instance defines where the
    extracted data resides and reports this to the consumer via
    its `get_src_dir` method. """

    def update(self):
        """ Brings the src dir up to date, ideally minimizing
        changes so that a subsequent build doesn't over-build.
        Returns a ChangeStatus object that helps the caller to
        understand the nature of the changes required during
        the update. """
        return ChangeStatus()

    def clean(self):
        """ Reverts any changes that might have been made to
        the src dir """
        pass

    def hash(self):
        """ Returns a hash that identifies the version of the code in the
        working copy.  For a git repo this is commit hash for the working
        copy.  For other Fetchers this should relate to the version of
        the code in the src dir.  The intent is that if a manifest
        changes the version/rev of a project that the hash be different.
        Importantly, this should be computable without actually fetching
        the code, as we want this to factor into a hash used to download
        a pre-built version of the code, without having to first download
        and extract its sources (eg: boost on windows is pretty painful).
        """
        pass

    def get_src_dir(self):
        """ Returns the source directory that the project was
        extracted into """
        pass


class LocalDirFetcher(object):
    """ This class exists to override the normal fetching behavior, and
    use an explicit user-specified directory for the project sources.

    This fetcher cannot update or track changes.  It always reports that the
    project has changed, forcing it to always be built. """

    def __init__(self, path):
        self.path = os.path.realpath(path)

    def update(self):
        return ChangeStatus(all_changed=True)

    def hash(self):
        return "0" * 40

    def get_src_dir(self):
        return self.path


class GitFetcher(Fetcher):
    DEFAULT_DEPTH = 100

    def __init__(self, build_options, manifest, repo_url, rev, depth):
        # Extract the host/path portions of the URL and generate a flattened
        # directory name.  eg:
        # github.com/facebook/folly.git -> github.com-facebook-folly.git
        url = urlparse(repo_url)
        directory = "%s%s" % (url.netloc, url.path)
        for s in ["/", "\\", ":"]:
            directory = directory.replace(s, "-")

        # Place it in a repos dir in the scratch space
        repos_dir = os.path.join(build_options.scratch_dir, "repos")
        if not os.path.exists(repos_dir):
            os.makedirs(repos_dir)
        self.repo_dir = os.path.join(repos_dir, directory)

        if not rev and build_options.project_hashes:
            hash_file = os.path.join(
                build_options.project_hashes,
                re.sub("\\.git$", "-rev.txt", url.path[1:]),
            )
            if os.path.exists(hash_file):
                with open(hash_file, "r") as f:
                    data = f.read()
                    m = re.match("Subproject commit ([a-fA-F0-9]{40})", data)
                    if not m:
                        raise Exception("Failed to parse rev from %s" % hash_file)
                    rev = m.group(1)
                    print("Using pinned rev %s for %s" % (rev, repo_url))

        self.rev = rev or "master"
        self.origin_repo = repo_url
        self.manifest = manifest
        self.depth = depth if depth else GitFetcher.DEFAULT_DEPTH

    def _update(self):
        current_hash = (
            subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=self.repo_dir)
            .strip()
            .decode("utf-8")
        )
        target_hash = (
            subprocess.check_output(["git", "rev-parse", self.rev], cwd=self.repo_dir)
            .strip()
            .decode("utf-8")
        )
        if target_hash == current_hash:
            # It's up to date, so there are no changes.  This doesn't detect eg:
            # if origin/master moved and rev='master', but that's ok for our purposes;
            # we should be using explicit hashes or eg: a stable branch for the cases
            # that we care about, and it isn't unreasonable to require that the user
            # explicitly perform a clean build if those have moved.  For the most
            # part we prefer that folks build using a release tarball from github
            # rather than use the git protocol, as it is generally a bit quicker
            # to fetch and easier to hash and verify tarball downloads.
            return ChangeStatus()

        print("Updating %s -> %s" % (self.repo_dir, self.rev))
        run_cmd(["git", "fetch", "origin"], cwd=self.repo_dir)
        run_cmd(["git", "checkout", self.rev], cwd=self.repo_dir)
        run_cmd(["git", "submodule", "update", "--init"], cwd=self.repo_dir)

        return ChangeStatus(True)

    def update(self):
        if os.path.exists(self.repo_dir):
            return self._update()
        self._clone()
        return ChangeStatus(True)

    def _clone(self):
        print("Cloning %s..." % self.origin_repo)
        # The basename/dirname stuff allows us to dance around issues where
        # eg: this python process is native win32, but the git.exe is cygwin
        # or msys and doesn't like the absolute windows path that we'd otherwise
        # pass to it.  Careful use of cwd helps avoid headaches with cygpath.
        run_cmd(
            [
                "git",
                "clone",
                "--depth=" + str(self.depth),
                "--",
                self.origin_repo,
                os.path.basename(self.repo_dir),
            ],
            cwd=os.path.dirname(self.repo_dir),
        )
        self._update()

    def clean(self):
        if os.path.exists(self.repo_dir):
            run_cmd(["git", "clean", "-fxd"], cwd=self.repo_dir)

    def hash(self):
        return self.rev

    def get_src_dir(self):
        return self.repo_dir


def does_file_need_update(src_name, src_st, dest_name):
    try:
        target_st = os.lstat(dest_name)
    except OSError as exc:
        if exc.errno != errno.ENOENT:
            raise
        return True

    if src_st.st_size != target_st.st_size:
        return True

    if stat.S_IFMT(src_st.st_mode) != stat.S_IFMT(target_st.st_mode):
        return True
    if stat.S_ISLNK(src_st.st_mode):
        return os.readlink(src_name) != os.readlink(dest_name)
    if not stat.S_ISREG(src_st.st_mode):
        return True

    # They might have the same content; compare.
    with open(src_name, "rb") as sf, open(dest_name, "rb") as df:
        chunk_size = 8192
        while True:
            src_data = sf.read(chunk_size)
            dest_data = df.read(chunk_size)
            if src_data != dest_data:
                return True
            if len(src_data) < chunk_size:
                # EOF
                break
    return False


def copy_if_different(src_name, dest_name):
    """ Copy src_name -> dest_name, but only touch dest_name
    if src_name is different from dest_name, making this a
    more build system friendly way to copy. """
    src_st = os.lstat(src_name)
    if not does_file_need_update(src_name, src_st, dest_name):
        return False

    dest_parent = os.path.dirname(dest_name)
    if not os.path.exists(dest_parent):
        os.makedirs(dest_parent)
    if stat.S_ISLNK(src_st.st_mode):
        try:
            os.unlink(dest_name)
        except OSError as exc:
            if exc.errno != errno.ENOENT:
                raise
        target = os.readlink(src_name)
        print("Symlinking %s -> %s" % (dest_name, target))
        os.symlink(target, dest_name)
    else:
        print("Copying %s -> %s" % (src_name, dest_name))
        shutil.copy2(src_name, dest_name)

    return True


class ShipitPathMap(object):
    def __init__(self):
        self.roots = []
        self.mapping = []
        self.exclusion = []

    def add_mapping(self, fbsource_dir, target_dir):
        """ Add a posix path or pattern.  We cannot normpath the input
        here because that would change the paths from posix to windows
        form and break the logic throughout this class. """
        self.roots.append(fbsource_dir)
        self.mapping.append((fbsource_dir, target_dir))

    def add_exclusion(self, pattern):
        self.exclusion.append(re.compile(pattern))

    def _minimize_roots(self):
        """ compute the de-duplicated set of roots within fbsource.
        We take the shortest common directory prefix to make this
        determination """
        self.roots.sort(key=len)
        minimized = []

        for r in self.roots:
            add_this_entry = True
            for existing in minimized:
                if r.startswith(existing + "/"):
                    add_this_entry = False
                    break
            if add_this_entry:
                minimized.append(r)

        self.roots = minimized

    def _sort_mapping(self):
        self.mapping.sort(reverse=True, key=lambda x: len(x[0]))

    def _map_name(self, norm_name, dest_root):
        if norm_name.endswith(".pyc") or norm_name.endswith(".swp"):
            # Ignore some incidental garbage while iterating
            return None

        for excl in self.exclusion:
            if excl.match(norm_name):
                return None

        for src_name, dest_name in self.mapping:
            if norm_name == src_name or norm_name.startswith(src_name + "/"):
                rel_name = os.path.relpath(norm_name, src_name)
                # We can have "." as a component of some paths, depending
                # on the contents of the shipit transformation section.
                # normpath doesn't always remove `.` as the final component
                # of the path, which be problematic when we later mkdir
                # the dirname of the path that we return.  Take care to avoid
                # returning a path with a `.` in it.
                rel_name = os.path.normpath(rel_name)
                if dest_name == ".":
                    return os.path.normpath(os.path.join(dest_root, rel_name))
                dest_name = os.path.normpath(dest_name)
                return os.path.normpath(os.path.join(dest_root, dest_name, rel_name))

        raise Exception("%s did not match any rules" % norm_name)

    def mirror(self, fbsource_root, dest_root):
        self._minimize_roots()
        self._sort_mapping()

        change_status = ChangeStatus()

        # Record the full set of files that should be in the tree
        full_file_list = set()

        for fbsource_subdir in self.roots:
            dir_to_mirror = os.path.join(fbsource_root, fbsource_subdir)
            prefetch_dir_if_eden(dir_to_mirror)
            if not os.path.exists(dir_to_mirror):
                raise Exception(
                    "%s doesn't exist; check your sparse profile!" % dir_to_mirror
                )
            for root, _dirs, files in os.walk(dir_to_mirror):
                for src_file in files:
                    full_name = os.path.join(root, src_file)
                    rel_name = os.path.relpath(full_name, fbsource_root)
                    norm_name = rel_name.replace("\\", "/")

                    target_name = self._map_name(norm_name, dest_root)
                    if target_name:
                        full_file_list.add(target_name)
                        if copy_if_different(full_name, target_name):
                            change_status.record_change(target_name)

        # Compare the list of previously shipped files; if a file is
        # in the old list but not the new list then it has been
        # removed from the source and should be removed from the
        # destination.
        # Why don't we simply create this list by walking dest_root?
        # Some builds currently have to be in-source builds and
        # may legitimately need to keep some state in the source tree :-/
        installed_name = os.path.join(dest_root, ".shipit_shipped")
        if os.path.exists(installed_name):
            with open(installed_name, "rb") as f:
                for name in f.read().decode("utf-8").splitlines():
                    name = name.strip()
                    if name not in full_file_list:
                        print("Remove %s" % name)
                        os.unlink(name)
                        change_status.record_change(name)

        with open(installed_name, "wb") as f:
            for name in sorted(list(full_file_list)):
                f.write(("%s\n" % name).encode("utf-8"))

        return change_status


FBSOURCE_REPO_HASH = {}


def get_fbsource_repo_hash(build_options):
    """ Returns the hash for the fbsource repo.
    Since we may have multiple first party projects to
    hash, and because we don't mutate the repo, we cache
    this hash in a global. """
    global FBSOURCE_REPO_HASH
    cached_hash = FBSOURCE_REPO_HASH.get(build_options.fbsource_dir)
    if cached_hash:
        return cached_hash

    cmd = ["hg", "log", "-r.", "-T{node}"]
    env = Env()
    env.set("HGPLAIN", "1")
    cached_hash = subprocess.check_output(
        cmd, cwd=build_options.fbsource_dir, env=dict(env.items())
    ).decode("ascii")

    FBSOURCE_REPO_HASH[build_options.fbsource_dir] = cached_hash

    return cached_hash


class SimpleShipitTransformerFetcher(Fetcher):
    def __init__(self, build_options, manifest):
        self.build_options = build_options
        self.manifest = manifest
        self.repo_dir = os.path.join(build_options.scratch_dir, "shipit", manifest.name)

    def clean(self):
        if os.path.exists(self.repo_dir):
            shutil.rmtree(self.repo_dir)

    def update(self):
        mapping = ShipitPathMap()
        for src, dest in self.manifest.get_section_as_ordered_pairs("shipit.pathmap"):
            mapping.add_mapping(src, dest)
        if self.manifest.shipit_fbcode_builder:
            mapping.add_mapping(
                "fbcode/opensource/fbcode_builder", "build/fbcode_builder"
            )
        for pattern in self.manifest.get_section_as_args("shipit.strip"):
            mapping.add_exclusion(pattern)

        return mapping.mirror(self.build_options.fbsource_dir, self.repo_dir)

    def hash(self):
        return get_fbsource_repo_hash(self.build_options)

    def get_src_dir(self):
        return self.repo_dir


class ShipitTransformerFetcher(Fetcher):
    SHIPIT = "/var/www/scripts/opensource/shipit/run_shipit.php"

    def __init__(self, build_options, project_name):
        self.build_options = build_options
        self.project_name = project_name
        self.repo_dir = os.path.join(build_options.scratch_dir, "shipit", project_name)

    def update(self):
        if os.path.exists(self.repo_dir):
            return ChangeStatus()
        self.run_shipit()
        return ChangeStatus(True)

    def clean(self):
        if os.path.exists(self.repo_dir):
            shutil.rmtree(self.repo_dir)

    @classmethod
    def available(cls):
        return os.path.exists(cls.SHIPIT)

    def run_shipit(self):
        tmp_path = self.repo_dir + ".new"
        try:
            if os.path.exists(tmp_path):
                shutil.rmtree(tmp_path)

            # Run shipit
            run_cmd(
                [
                    "php",
                    ShipitTransformerFetcher.SHIPIT,
                    "--project=" + self.project_name,
                    "--create-new-repo",
                    "--source-repo-dir=" + self.build_options.fbsource_dir,
                    "--source-branch=.",
                    "--skip-source-init",
                    "--skip-source-pull",
                    "--skip-source-clean",
                    "--skip-push",
                    "--skip-reset",
                    "--skip-project-specific",
                    "--destination-use-anonymous-https",
                    "--create-new-repo-output-path=" + tmp_path,
                ]
            )

            # Remove the .git directory from the repository it generated.
            # There is no need to commit this.
            repo_git_dir = os.path.join(tmp_path, ".git")
            shutil.rmtree(repo_git_dir)
            os.rename(tmp_path, self.repo_dir)
        except Exception:
            # Clean up after a failed extraction
            if os.path.exists(tmp_path):
                shutil.rmtree(tmp_path)
            self.clean()
            raise

    def hash(self):
        return get_fbsource_repo_hash(self.build_options)

    def get_src_dir(self):
        return self.repo_dir


def download_url_to_file_with_progress(url, file_name):
    print("Download %s -> %s ..." % (url, file_name))

    class Progress(object):
        last_report = 0

        def progress(self, count, block, total):
            if total == -1:
                total = "(Unknown)"
            amount = count * block

            if sys.stdout.isatty():
                sys.stdout.write("\r downloading %s of %s " % (amount, total))
            else:
                # When logging to CI logs, avoid spamming the logs and print
                # status every few seconds
                now = time.time()
                if now - self.last_report > 5:
                    sys.stdout.write(".. %s of %s " % (amount, total))
                    self.last_report = now
            sys.stdout.flush()

    progress = Progress()
    start = time.time()
    try:
        (_filename, headers) = urlretrieve(url, file_name, reporthook=progress.progress)
    except OSError as exc:
        raise TransientFailure(
            "Failed to download %s to %s: %s" % (url, file_name, str(exc))
        )

    end = time.time()
    sys.stdout.write(" [Complete in %f seconds]\n" % (end - start))
    sys.stdout.flush()
    print("%s" % (headers))


class ArchiveFetcher(Fetcher):
    def __init__(self, build_options, manifest, url, sha256):
        self.manifest = manifest
        self.url = url
        self.sha256 = sha256
        self.build_options = build_options

        url = urlparse(self.url)
        basename = "%s-%s" % (manifest.name, os.path.basename(url.path))
        self.file_name = os.path.join(build_options.scratch_dir, "downloads", basename)
        self.src_dir = os.path.join(build_options.scratch_dir, "extracted", basename)
        self.hash_file = self.src_dir + ".hash"

    def _verify_hash(self):
        h = hashlib.sha256()
        with open(self.file_name, "rb") as f:
            while True:
                block = f.read(8192)
                if not block:
                    break
                h.update(block)
        digest = h.hexdigest()
        if digest != self.sha256:
            os.unlink(self.file_name)
            raise Exception(
                "%s: expected sha256 %s but got %s" % (self.url, self.sha256, digest)
            )

    def _download_dir(self):
        """ returns the download dir, creating it if it doesn't already exist """
        download_dir = os.path.dirname(self.file_name)
        if not os.path.exists(download_dir):
            os.makedirs(download_dir)
        return download_dir

    def _download(self):
        self._download_dir()
        download_url_to_file_with_progress(self.url, self.file_name)
        self._verify_hash()

    def clean(self):
        if os.path.exists(self.src_dir):
            shutil.rmtree(self.src_dir)

    def update(self):
        try:
            with open(self.hash_file, "r") as f:
                saved_hash = f.read().strip()
                if saved_hash == self.sha256 and os.path.exists(self.src_dir):
                    # Everything is up to date
                    return ChangeStatus()
                print(
                    "saved hash %s doesn't match expected hash %s, re-validating"
                    % (saved_hash, self.sha256)
                )
                os.unlink(self.hash_file)
        except EnvironmentError:
            pass

        # If we got here we know the contents of src_dir are either missing
        # or wrong, so blow away whatever happened to be there first.
        if os.path.exists(self.src_dir):
            shutil.rmtree(self.src_dir)

        # If we already have a file here, make sure it looks legit before
        # proceeding: any errors and we just remove it and re-download
        if os.path.exists(self.file_name):
            try:
                self._verify_hash()
            except Exception:
                if os.path.exists(self.file_name):
                    os.unlink(self.file_name)

        if not os.path.exists(self.file_name):
            self._download()

        if tarfile.is_tarfile(self.file_name):
            opener = tarfile.open
        elif zipfile.is_zipfile(self.file_name):
            opener = zipfile.ZipFile
        else:
            raise Exception("don't know how to extract %s" % self.file_name)
        os.makedirs(self.src_dir)
        print("Extract %s -> %s" % (self.file_name, self.src_dir))
        t = opener(self.file_name)
        if is_windows():
            # Ensure that we don't fall over when dealing with long paths
            # on windows
            src = r"\\?\%s" % os.path.normpath(self.src_dir)
        else:
            src = self.src_dir
        # The `str` here is necessary to ensure that we don't pass a unicode
        # object down to tarfile.extractall on python2.  When extracting
        # the boost tarball it makes some assumptions and tries to convert
        # a non-ascii path to ascii and throws.
        src = str(src)
        t.extractall(src)

        with open(self.hash_file, "w") as f:
            f.write(self.sha256)

        return ChangeStatus(True)

    def hash(self):
        return self.sha256

    def get_src_dir(self):
        return self.src_dir
