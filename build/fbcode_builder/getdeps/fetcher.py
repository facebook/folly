# Copyright (c) 2019-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

from __future__ import absolute_import, division, print_function, unicode_literals

import hashlib
import os
import re
import shutil
import subprocess
import sys
import tarfile
import time
import zipfile

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
        changes the version/rev of a project that the hash be different. """
        pass

    def get_src_dir(self):
        """ Returns the source directory that the project was
        extracted into """
        pass


class GitFetcher(Fetcher):
    def __init__(self, build_options, manifest, repo_url, rev):
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

        if not rev:
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
                "--depth=100",
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
        """ Returns a hash that identifies the version of the code in the
        working copy """
        return (
            subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=self.repo_dir)
            .strip()
            .decode("utf-8")[0:6]
        )

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
        cmd = ["hg", "log", "-r.", "-T{node}"]
        env = os.environ.copy()
        env["HGPLAIN"] = "1"
        fbsource_hash = subprocess.check_output(
            cmd, cwd=self.build_options.fbsource_dir, env=env
        )
        return fbsource_hash[0:6]

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
    (_filename, headers) = urlretrieve(url, file_name, reporthook=progress.progress)
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

    def _download(self):
        download_dir = os.path.dirname(self.file_name)
        if not os.path.exists(download_dir):
            os.makedirs(download_dir)

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
        return self.sha256[0:6]

    def get_src_dir(self):
        return self.src_dir
