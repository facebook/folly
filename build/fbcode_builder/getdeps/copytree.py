# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# pyre-unsafe

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


# pyre-fixme[9]: ignore has type `bool`; used as `None`.
def copytree(src_dir, dest_dir, ignore: bool = None):
    """Recursively copy the src_dir to the dest_dir, filtering
    out entries using the ignore lambda.  The behavior of the
    ignore lambda must match that described by `shutil.copytree`.
    This `copytree` function knows how to prefetch data when
    running in an eden repo.
    TODO: I'd like to either extend this or add a variant that
    uses watchman to mirror src_dir into dest_dir.
    """
    prefetch_dir_if_eden(src_dir)
    # pyre-fixme[6]: For 3rd param expected
    #  `Union[typing.Callable[[Union[PathLike[str], str], List[str]], Iterable[str]],
    #  typing.Callable[[str, List[str]], Iterable[str]], None]` but got `bool`.
    return shutil.copytree(src_dir, dest_dir, ignore=ignore)
