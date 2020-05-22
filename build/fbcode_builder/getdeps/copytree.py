# Copyright (c) Facebook, Inc. and its affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from __future__ import absolute_import, division, print_function, unicode_literals

import os
import shutil
import subprocess

from .platform import is_windows


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
                return os.path.realpath(repo_root)
        return None

    try:
        return os.readlink(os.path.join(dirpath, ".eden", "root"))
    except OSError:
        return None


def prefetch_dir_if_eden(dirpath):
    """ After an amend/rebase, Eden may need to fetch a large number
    of trees from the servers.  The simplistic single threaded walk
    performed by copytree makes this more expensive than is desirable
    so we help accelerate things by performing a prefetch on the
    source directory """
    global PREFETCHED_DIRS
    if dirpath in PREFETCHED_DIRS:
        return
    root = find_eden_root(dirpath)
    if root is None:
        return
    rel = os.path.relpath(dirpath, root)
    print("Prefetching %s..." % rel)
    subprocess.call(
        ["edenfsctl", "prefetch", "--repo", root, "--silent", "%s/**" % rel]
    )
    PREFETCHED_DIRS.add(dirpath)


def copytree(src_dir, dest_dir, ignore=None):
    """ Recursively copy the src_dir to the dest_dir, filtering
    out entries using the ignore lambda.  The behavior of the
    ignore lambda must match that described by `shutil.copytree`.
    This `copytree` function knows how to prefetch data when
    running in an eden repo.
    TODO: I'd like to either extend this or add a variant that
    uses watchman to mirror src_dir into dest_dir.
    """
    prefetch_dir_if_eden(src_dir)
    return shutil.copytree(src_dir, dest_dir, ignore=ignore)
