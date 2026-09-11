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

import io
import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from folly.agents.backtest import compress_checkpoint_outputs as compressor


def make_sample(root: Path, phases: list[tuple[str, str]]) -> None:
    records = []
    for name, contents in phases:
        artifact = f"output-{name}.md"
        (root / artifact).write_text(contents)
        records.append({"artifact": artifact, "phase": name})
    (root / "output.md").write_text(phases[-1][1])
    (root / "checkpoints.json").write_text(json.dumps(records))


def compress(sample: Path) -> str:
    mapping = io.StringIO()
    compressor.compress(sample, mapping)
    return mapping.getvalue()


class CompressCheckpointOutputsTest(unittest.TestCase):
    def test_deduplicates_and_removes_artifact_fields(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            sample = Path(temporary)
            repeated = "".join(f"line {index}\n" for index in range(100))
            author = repeated.replace("line 20\n", "author edit\n")
            final = repeated.replace("line 80\n", "review edit\n")
            make_sample(
                sample,
                [
                    ("initial", repeated),
                    ("author", author),
                    ("review1", repeated),
                    ("review2", final),
                    ("review3", final),
                ],
            )
            (sample / "output-author-to-initial.diff").write_text("stale")
            (sample / "output-unrelated.diff").write_text("unrelated")

            mapping = compress(sample)

            self.assertTrue((sample / "output-initial.md").is_file())
            self.assertFalse((sample / "output-author.md").exists())
            self.assertFalse((sample / "output-review1.md").exists())
            self.assertFalse((sample / "output-review2.md").exists())
            self.assertFalse((sample / "output-review3.md").exists())
            self.assertFalse((sample / "output-author-to-initial.diff").exists())
            self.assertTrue((sample / "output-unrelated.diff").is_file())
            self.assertEqual(
                json.loads((sample / "checkpoints.json").read_text()),
                [
                    {"phase": "initial"},
                    {"phase": "author"},
                    {"phase": "review1"},
                    {"phase": "review2"},
                    {"phase": "review3"},
                ],
            )
            helper = os.path.relpath(compressor._APPLY_DIFFS, sample)
            self.assertEqual(
                mapping,
                f"""## Checkpoints

Below, `apply_diffs` is short for `{helper}`.

- **Initial draft:** [output-initial.md](output-initial.md)
- **Author review:** `apply_diffs output-initial.md output-review1-to-author.diff`
- **Review 1:** [output-initial.md](output-initial.md)
- **Review 2:** [output.md](output.md)
- **Review 3:** [output.md](output.md)
""",
            )

    def test_reverse_diffs_reconstruct_each_phase(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            sample = Path(temporary)
            initial = "".join(f"line {index}\n" for index in range(100)).replace(
                "line 40\n", "line 40\rsegment\n"
            )
            author = initial.replace("line 20\n", "").replace("line 80\n", "")
            final = author.replace("line 90\n", "review edit\n")
            make_sample(
                sample,
                [("initial", initial), ("author", author), ("review1", final)],
            )

            mapping = compress(sample)

            author_diff = "output-review1-to-author.diff"
            initial_diff = "output-author-to-initial.diff"
            reconstructed = subprocess.run(
                [
                    str(compressor._APPLY_DIFFS),
                    "output.md",
                    author_diff,
                    initial_diff,
                ],
                cwd=sample,
                check=True,
                stdout=subprocess.PIPE,
            ).stdout
            self.assertEqual(reconstructed, initial.encode())
            self.assertFalse((sample / "output-initial.md").exists())
            self.assertFalse((sample / "output-author.md").exists())
            self.assertEqual(
                (sample / author_diff).read_text(),
                "89c89\n< review edit\n---\n> line 90\n",
            )
            self.assertEqual(
                (sample / initial_diff).read_text(),
                "20a21\n> line 20\n79a81\n> line 80\n",
            )
            self.assertIn(
                f"`apply_diffs output.md {author_diff} {initial_diff}`", mapping
            )
            self.assertIn(
                f"- **Author review:** truncate the Initial draft command after "
                f"`{author_diff}`",
                mapping,
            )
            self.assertEqual(mapping.count("`apply_diffs "), 1)

    def test_diff_threshold_is_strict(self) -> None:
        self.assertTrue(compressor._use_diff(179, 300))
        self.assertFalse(compressor._use_diff(180, 300))

    def test_keeps_output_that_cannot_be_diffed(self) -> None:
        for initial in ("missing newline", "binary\0value\n"):
            with self.subTest(initial=initial), tempfile.TemporaryDirectory() as temp:
                sample = Path(temp)
                make_sample(sample, [("initial", initial), ("author", "complete\n")])

                mapping = compress(sample)

                self.assertTrue((sample / "output-initial.md").is_file())
                self.assertFalse((sample / "output-author.md").exists())
                self.assertFalse((sample / "output-author-to-initial.diff").exists())
                self.assertNotIn("apply_diffs", mapping)

    def test_rejects_invalid_phase_sequence(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            sample = Path(temporary)
            make_sample(sample, [("initial", "initial\n"), ("final", "final\n")])

            with self.assertRaisesRegex(ValueError, "phase or artifact"):
                compress(sample)

    def test_reconstruction_failure_keeps_sources_and_accounting(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            sample = Path(temporary)
            initial = "".join(f"line {index}\n" for index in range(100))
            final = initial.replace("line 50\n", "review edit\n")
            make_sample(sample, [("initial", initial), ("author", final)])
            accounting = (sample / "checkpoints.json").read_bytes()
            executable = shutil.which("diff")
            self.assertIsNotNone(executable)
            bin_dir = sample / "bin"
            bin_dir.mkdir()
            (bin_dir / "diff").symlink_to(executable)
            path = os.environ.get("PATH")
            os.environ["PATH"] = str(bin_dir)
            try:
                with self.assertRaises(subprocess.CalledProcessError):
                    compress(sample)
            finally:
                if path is None:
                    del os.environ["PATH"]
                else:
                    os.environ["PATH"] = path

            self.assertTrue((sample / "output-initial.md").is_file())
            self.assertTrue((sample / "output-author.md").is_file())
            self.assertEqual((sample / "checkpoints.json").read_bytes(), accounting)

    def test_mapping_write_failure_keeps_sources_and_accounting(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            sample = Path(temporary)
            initial = "".join(f"line {index}\n" for index in range(100))
            final = initial.replace("line 50\n", "review edit\n")
            make_sample(sample, [("initial", initial), ("author", final)])
            accounting = (sample / "checkpoints.json").read_bytes()
            mapping = io.StringIO()
            mapping.close()

            with self.assertRaisesRegex(ValueError, "closed file"):
                compressor.compress(sample, mapping)

            self.assertTrue((sample / "output-initial.md").is_file())
            self.assertTrue((sample / "output-author.md").is_file())
            self.assertEqual((sample / "checkpoints.json").read_bytes(), accounting)
            self.assertTrue((sample / "output-author-to-initial.diff").is_file())

            compress(sample)

    def test_rejects_symlinked_inputs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            sample = Path(temporary)
            make_sample(sample, [("initial", "initial\n")])
            (sample / "output-initial.md").unlink()
            (sample / "output-initial.md").symlink_to(sample / "output.md")

            with self.assertRaisesRegex(ValueError, "not a regular file"):
                compress(sample)


if __name__ == "__main__":
    unittest.main()
