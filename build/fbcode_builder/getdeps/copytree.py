# Copyright (c) Facebook, Inc. and its affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from __future__ import absolute_import, division, print_function, unicode_literals

import os
import shutil
import subprocess


PREFETCHED_DIRS = set()


def is_eden(dirpath):
    """Returns True if the specified directory is the root directory of,
    or is a sub-directory of an Eden mount."""
    return os.path.islink(os.path.join(dirpath, ".eden", "root"))


def find_eden_root(dirpath):
    """If the specified directory is the root directory of, or is a
    sub-directory of an Eden mount, returns the canonical absolute path
    to the root of that Eden mount."""
    return os.readlink(os.path.join(dirpath, ".eden", "root"))


def prefetch_dir_if_eden(dirpath):
    """ After an amend/rebase, Eden may need to fetch a large number
    of trees from the servers.  The simplistic single threaded walk
    performed by copytree makes this more expensive than is desirable
    so we help accelerate things by performing a prefetch on the
    source directory """
    global PREFETCHED_DIRS
    if not is_eden(dirpath) or dirpath in PREFETCHED_DIRS:
        return
    root = find_eden_root(dirpath)
    rel = os.path.relpath(dirpath, root)
    print("Prefetching %s..." % rel)
    # TODO: this should be edenfsctl but until I swing through a new
    # package deploy, I only have `eden` on my mac to test this
    subprocess.call(["eden", "prefetch", "--repo", root, "--silent", "%s/**" % rel])
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
