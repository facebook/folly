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

from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from folly.agents.backtest import checkpoint


class CheckpointTest(unittest.TestCase):
    def run_checkpoint(
        self, index: int, run_root: Path, workdir: Path
    ) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [checkpoint.__file__, str(index)],
            env={
                **os.environ,
                "FOLLY_BACKTEST_RUN_DIR": str(run_root),
                "FOLLY_BACKTEST_WORKDIR": str(workdir),
            },
            text=True,
            capture_output=True,
            check=False,
        )

    def test_captures_sequence_and_reports_errors(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "workdir/output.md"
            output.parent.mkdir()
            output.write_text("initial")

            first = self.run_checkpoint(0, root, output.parent)
            output.write_text("author")
            second = self.run_checkpoint(1, root, output.parent)

            self.assertEqual(first.returncode, 0)
            self.assertEqual(first.stderr, "")
            self.assertEqual(
                first.stdout.splitlines(),
                [
                    f"{checkpoint.CHECKPOINT_MARKER_PREFIX}0"
                    f"{checkpoint.CHECKPOINT_MARKER_SUFFIX}",
                    checkpoint.next_instruction(0),
                ],
            )
            self.assertEqual(second.returncode, 0)
            self.assertEqual(second.stderr, "")
            self.assertEqual(
                second.stdout.splitlines()[0],
                f"{checkpoint.CHECKPOINT_MARKER_PREFIX}1"
                f"{checkpoint.CHECKPOINT_MARKER_SUFFIX}",
            )
            for phrase in (
                "complete one round",
                "follow-up checks",
                "`backtest-checkpoint 2`",
                "follow its stdout",
            ):
                self.assertIn(phrase, second.stdout)
            self.assertEqual((root / "checkpoints/0.md").read_text(), "initial")
            self.assertEqual((root / "checkpoints/1.md").read_text(), "author")
            invalid = self.run_checkpoint(1, root, output.parent)
            self.assertNotEqual(invalid.returncode, 0)
            self.assertEqual(
                invalid.stdout.strip(),
                "This run is invalid. Stop and explain the error in your final response.",
            )
