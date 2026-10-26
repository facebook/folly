# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Snapshot file contents for comparison and copying.

{'relative/path': b'file bytes'}
Directories are not shown. Throws on symlinks and special files.
"""

from __future__ import annotations

import stat
from pathlib import Path


Snapshot = dict[Path, bytes]


def read(root: Path) -> Snapshot:
    """Read a settled tree, without following links at the root or below it."""
    if not stat.S_ISDIR(root.lstat().st_mode):
        raise ValueError(f"snapshot root is not a regular directory: {root}")

    path_to_bytes: Snapshot = {}
    # os.walk and Path.walk suppress some classification errors.
    directories = [root]
    while directories:
        for path in directories.pop().iterdir():
            mode = path.lstat().st_mode
            if stat.S_ISDIR(mode):
                directories.append(path)
            elif stat.S_ISREG(mode):
                path_to_bytes[path.relative_to(root)] = path.read_bytes()
            else:
                raise ValueError(
                    f"snapshot entry is not a regular file or directory: {path}"
                )

    return path_to_bytes


def write(path_to_bytes: Snapshot, destination: Path) -> None:
    for path in path_to_bytes:
        if not path.parts or path.is_absolute() or ".." in path.parts:
            raise ValueError(f"snapshot path must be relative without '..': {path}")
    destination.mkdir()
    for path, contents in path_to_bytes.items():
        for parent in reversed(path.parents):
            (destination / parent).mkdir(exist_ok=True)
        with (destination / path).open("xb") as output:
            output.write(contents)
