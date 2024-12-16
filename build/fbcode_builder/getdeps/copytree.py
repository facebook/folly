# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# pyre-unsafe

import os
import shutil
import subprocess

from .platform import is_windows
from .runcmd import run_cmd


PREFETCHED_DIRS = set()


def containing_repo_type(path):
    while True:
        if os.path.exists(os.path.join(path, ".git")):
            return ("git", path)
        if os.path.exists(os.path.join(path, ".hg")):
            return ("hg", path)

        parent = os.path.dirname(path)
        if parent == path:
            return None, None
        path = parent


def find_eden_root(dirpath):
    """If the specified directory is inside an EdenFS checkout, returns
    the canonical absolute path to the root of that checkout.

    Returns None if the specified directory is not in an EdenFS checkout.
    """
    if is_windows():
        repo_type, repo_root = containing_repo_type(dirpath)
        if repo_root is not None:
            if os.path.exists(os.path.join(repo_root, ".eden", "config")):
                return repo_root
        return None

    try:
        return os.readlink(os.path.join(dirpath, ".eden", "root"))
    except OSError:
        return None


def prefetch_dir_if_eden(dirpath) -> None:
    """After an amend/rebase, Eden may need to fetch a large number
    of trees from the servers.  The simplistic single threaded walk
    performed by copytree makes this more expensive than is desirable
    so we help accelerate things by performing a prefetch on the
    source directory"""
    global PREFETCHED_DIRS
    if dirpath in PREFETCHED_DIRS:
        return
    root = find_eden_root(dirpath)
    if root is None:
        return
    glob = f"{os.path.relpath(dirpath, root).replace(os.sep, '/')}/**"
    print(f"Prefetching {glob}")
    subprocess.call(["edenfsctl", "prefetch", "--repo", root, glob, "--background"])
    PREFETCHED_DIRS.add(dirpath)


def simple_copytree(src_dir, dest_dir, symlinks=False):
    """A simple version of shutil.copytree() that can delegate to native tools if faster"""
    if is_windows():
        os.makedirs(dest_dir, exist_ok=True)
        cmd = [
            "robocopy.exe",
            src_dir,
            dest_dir,
            # copy directories, including empty ones
            "/E",
            # Ignore Extra files in destination
            "/XX",
            # enable parallel copy
            "/MT",
            # be quiet
            "/NFL",
            "/NDL",
            "/NJH",
            "/NJS",
            "/NP",
        ]
        if symlinks:
            cmd.append("/SL")
        # robocopy exits with code 1 if it copied ok, hence allow_fail
        # https://learn.microsoft.com/en-us/troubleshoot/windows-server/backup-and-storage/return-codes-used-robocopy-utility
        exit_code = run_cmd(cmd, allow_fail=True)
        if exit_code > 1:
            raise subprocess.CalledProcessError(exit_code, cmd)
        return dest_dir
    else:
        return shutil.copytree(src_dir, dest_dir, symlinks=symlinks)
