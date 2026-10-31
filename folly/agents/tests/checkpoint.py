#!/usr/bin/env python3
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

"""Save one c-i backtest phase and tell the author when to checkpoint again.

Usage: backtest-checkpoint N

run_scenario supplies the run and work directories through the environment.
Snapshots stay outside the work directory so ordinary task-file discovery does
not expose earlier drafts. This is not a security boundary.
"""

from __future__ import annotations

import hashlib
import importlib
import json
import os
import stat
import sys
from pathlib import Path

# A main/library split would add more import wiring for this small copied CLI.
if __package__:
    from . import directory_snapshot
else:
    directory_snapshot = importlib.import_module("directory_snapshot")


# Accounting checks that checkpoint indices are sequential, but the agent being
# checkpointed should not see those indices in the marker.
# Birthday-collision probability:
# checkpoints  4 nibbles  5 nibbles  6 nibbles
# 5            0.02%      0.001%     0.00006%
# 10           0.07%      0.004%     0.0003%
# 20           0.3%       0.02%      0.001%
# 100          7%         0.5%       0.03%
def make_checkpoint_marker(index: int, key: bytes) -> str:
    token = hashlib.blake2s(str(index).encode(), key=key, digest_size=3).hexdigest()
    return f"@@FOLLY_BACKTEST_CHECKPOINT:{token}@@"


def get_checkpoint_dir() -> Path:
    return Path(os.environ["FOLLY_BACKTEST_RUN_DIR"]) / "checkpoints"


def get_output_dir() -> Path:
    workdir = Path(os.environ["FOLLY_BACKTEST_WORKDIR"])
    for directory in (workdir.parent, workdir):
        if not stat.S_ISDIR(directory.lstat().st_mode):
            raise ValueError(f"output ancestor is not a regular directory: {directory}")
    return workdir / "output"


def next_instruction(index: int) -> str:
    if index == 0:
        return (
            "If author self-review is required, finish it, then immediately run "
            "`backtest-checkpoint 1` and follow its stdout. Otherwise, do not run "
            "another checkpoint."
        )
    return (
        "If another external review is required, complete one round and its "
        f"follow-up checks, then immediately run `backtest-checkpoint {index + 1}` "
        "and follow its stdout. Otherwise, do not run another checkpoint."
    )


def main() -> None:
    try:
        if len(sys.argv) != 2:
            raise ValueError("usage: backtest-checkpoint N")
        index = int(sys.argv[1])
        checkpoints = get_checkpoint_dir()
        if index == 0:
            checkpoints.mkdir(exist_ok=True)
        if not stat.S_ISDIR(checkpoints.lstat().st_mode):
            raise ValueError("checkpoints are not a regular directory")
        paths = sorted(checkpoints.iterdir(), key=lambda path: int(path.name))
        expected = len(paths)
        if [path.name for path in paths] != [
            str(number) for number in range(expected)
        ] or any(not stat.S_ISDIR(path.lstat().st_mode) for path in paths):
            raise ValueError("checkpoints are not a gap-free sequence")
        if index != expected:
            raise ValueError(f"expected checkpoint {expected}, got {index}")
        metadata = json.loads((checkpoints.parent / "run.json").read_text())
        checkpoint_key = bytes.fromhex(metadata["checkpoint_key"])
        entries = directory_snapshot.read(get_output_dir())
        if not any(entries.values()):
            raise ValueError("checkpoint requires a nonempty draft")
        directory_snapshot.write(entries, checkpoints / str(index))
        print(make_checkpoint_marker(index, checkpoint_key))
        print(next_instruction(index))
    except Exception:
        print(
            "This run is invalid. Stop and explain the error in your final response.",
            flush=True,
        )
        raise


if __name__ == "__main__":
    main()
