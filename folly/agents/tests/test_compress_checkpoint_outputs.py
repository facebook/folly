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
from unittest import mock

from folly.agents.tests import (
    compress_checkpoint_outputs as compressor,
    directory_snapshot,
)


def make_sample(
    root: Path, phases: list[tuple[str, directory_snapshot.Snapshot]]
) -> None:
    root.mkdir()
    records = []
    for name, entries in phases:
        artifact = f"output-{name}.md"
        (root / artifact).write_bytes(entries[Path("main.md")])
        records.append({"artifact": artifact, "phase": name, "wall_seconds": 1.0})
    (root / "output").mkdir()
    (root / "output/main.md").write_bytes(phases[-1][1][Path("main.md")])
    (root / "checkpoints.json").write_text(json.dumps(records))


def compress(root: Path) -> list[dict]:
    compressor.compress(root, io.StringIO())
    return json.loads((root / "checkpoints.json").read_text())


def text_phases() -> list[tuple[str, directory_snapshot.Snapshot]]:
    initial = b"".join(f"line {index}\n".encode() for index in range(100))
    author = initial.replace(b"line 50\n", b"author edit\n")
    return [
        ("initial", {Path("main.md"): initial}),
        ("author", {Path("main.md"): author}),
        ("review1", {Path("main.md"): b"final\n"}),
    ]


class CompressCheckpointOutputsTest(unittest.TestCase):
    def test_deduplicates_and_removes_artifact_fields(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            sample = Path(temporary) / "sample"
            repeated = "".join(f"line {index}\n" for index in range(100))
            author = repeated.replace("line 20\n", "author edit\n")
            final = repeated.replace("line 80\n", "review edit\n")
            make_sample(
                sample,
                [
                    ("initial", {Path("main.md"): (repeated).encode()}),
                    ("author", {Path("main.md"): (author).encode()}),
                    ("review1", {Path("main.md"): (repeated).encode()}),
                    ("review2", {Path("main.md"): (final).encode()}),
                    ("review3", {Path("main.md"): (final).encode()}),
                ],
            )
            (sample / "output-author-to-initial.diff").write_text("stale")
            (sample / "output-author-to-review1.diff").write_text("stale")
            (sample / "output-unrelated.diff").write_text("unrelated")

            mapping = io.StringIO()
            compressor.compress(sample, mapping)

            self.assertTrue((sample / "output-initial.md").is_file())
            self.assertFalse((sample / "output-author.md").exists())
            self.assertFalse((sample / "output-review1.md").exists())
            self.assertFalse((sample / "output-review2.md").exists())
            self.assertFalse((sample / "output-review3.md").exists())
            self.assertFalse((sample / "output-author-to-initial.diff").exists())
            self.assertFalse((sample / "output-author-to-review1.diff").exists())
            self.assertTrue((sample / "output-unrelated.diff").is_file())
            records = json.loads((sample / "checkpoints.json").read_text())
            self.assertEqual(
                [record["phase"] for record in records],
                ["initial", "author", "review1", "review2", "review3"],
            )
            self.assertEqual(
                records[-1]["storage"]["files"]["main.md"]["base"], "output/main.md"
            )
            self.assertIn(
                "--restore author --destination /path/to/author", mapping.getvalue()
            )
            for record, (name, expected) in zip(
                records,
                (
                    ("initial", repeated),
                    ("author", author),
                    ("review1", repeated),
                    ("review2", final),
                    ("review3", final),
                ),
            ):
                self.assertNotIn("artifact", record)
                destination = sample / f"restore-{name}"
                compressor.restore(sample, name, destination)
                self.assertEqual(
                    directory_snapshot.read(destination),
                    {Path("main.md"): expected.encode()},
                )

    def test_forward_diffs_reconstruct_each_phase(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            sample = Path(temporary) / "sample"
            initial = "".join(f"line {index}\n" for index in range(100)).replace(
                "line 40\n", "line 40\rsegment\n"
            )
            author = initial.replace("line 20\n", "").replace("line 80\n", "")
            review1 = author.replace("line 90\n", "review edit\n")
            final = review1.replace("line 70\n", "final edit\n")
            make_sample(
                sample,
                [
                    ("initial", {Path("main.md"): (initial).encode()}),
                    ("author", {Path("main.md"): (author).encode()}),
                    ("review1", {Path("main.md"): (review1).encode()}),
                    ("review2", {Path("main.md"): (final).encode()}),
                ],
            )

            compress(sample)

            author_diff = "output-initial-to-author.diff"
            review_diff = "output-author-to-review1.diff"
            self.assertTrue((sample / "output-initial.md").is_file())
            self.assertFalse((sample / "output-author.md").exists())
            self.assertFalse((sample / "output-review1.md").exists())
            records = json.loads((sample / "checkpoints.json").read_text())
            self.assertEqual(
                records[2]["storage"]["files"]["main.md"]["diffs"],
                [author_diff, review_diff],
            )
            compressor.restore(sample, "review1", sample / "restored")
            self.assertEqual(
                directory_snapshot.read(sample / "restored"),
                {Path("main.md"): review1.encode()},
            )

    def test_diff_threshold_is_strict(self) -> None:
        self.assertTrue(compressor._use_diff(179, 300))
        self.assertFalse(compressor._use_diff(180, 300))

    def test_diff_threshold_uses_later_phase_size(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            sample = Path(temporary) / "sample"
            make_sample(
                sample,
                [
                    ("initial", {Path("main.md"): ("i" * 199 + "\n").encode()}),
                    ("author", {Path("main.md"): ("a" * 299 + "\n").encode()}),
                    ("review1", {Path("main.md"): ("final\n").encode()}),
                ],
            )
            _, phases = compressor._load(sample)
            with mock.patch.object(
                compressor, "_forward_diff", return_value=b"x" * 179
            ):
                stored = compressor._representations(sample, phases)

            self.assertEqual(stored[1].diffs, ("output-initial-to-author.diff",))

    def test_keeps_output_that_cannot_be_diffed(self) -> None:
        for initial in ("missing newline", "binary\0value\n"):
            with self.subTest(initial=initial), tempfile.TemporaryDirectory() as temp:
                sample = Path(temp) / "sample"
                make_sample(
                    sample,
                    [
                        ("initial", {Path("main.md"): (initial).encode()}),
                        ("author", {Path("main.md"): ("complete\n").encode()}),
                        ("review1", {Path("main.md"): ("final\n").encode()}),
                    ],
                )

                mapping = io.StringIO()
                compressor.compress(sample, mapping)

                self.assertTrue((sample / "output-initial.md").is_file())
                self.assertTrue((sample / "output-author.md").is_file())
                self.assertFalse((sample / "output-initial-to-author.diff").exists())
                self.assertNotIn("apply_diffs", mapping.getvalue())

    def test_declines_diffs_for_unrepresentable_files(self) -> None:
        for unsuitable in (b"missing newline", b"binary\0value\n", b"\xff\n"):
            for old, new in ((unsuitable, b"valid\n"), (b"valid\n", unsuitable)):
                with self.subTest(old=old, new=new):
                    self.assertIsNone(
                        compressor._forward_diff(
                            Path("."), Path("earlier"), Path("later"), old, new
                        )
                    )

    def test_rejects_invalid_phase_sequence(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            sample = Path(temporary) / "sample"
            make_sample(
                sample,
                [
                    ("initial", {Path("main.md"): ("initial\n").encode()}),
                    ("final", {Path("main.md"): ("final\n").encode()}),
                ],
            )

            with self.assertRaisesRegex(ValueError, "phase or artifact"):
                compress(sample)

    def test_reconstruction_failure_keeps_sources_and_accounting(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            sample = Path(temporary) / "sample"
            make_sample(sample, text_phases())
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
            sample = Path(temporary) / "sample"
            make_sample(sample, text_phases())
            accounting = (sample / "checkpoints.json").read_bytes()
            mapping = io.StringIO()
            mapping.close()

            with self.assertRaisesRegex(ValueError, "closed file"):
                compressor.compress(sample, mapping)

            self.assertTrue((sample / "output-initial.md").is_file())
            self.assertTrue((sample / "output-author.md").is_file())
            self.assertEqual((sample / "checkpoints.json").read_bytes(), accounting)
            self.assertTrue((sample / "output-initial-to-author.diff").is_file())

            compress(sample)

    def test_rejects_symlinked_inputs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            sample = Path(temporary) / "sample"
            make_sample(
                sample, [("initial", {Path("main.md"): ("initial\n").encode()})]
            )
            (sample / "output-initial.md").unlink()
            (sample / "output-initial.md").symlink_to(sample / "output/main.md")

            with self.assertRaisesRegex(ValueError, "not a regular file"):
                compress(sample)

    def test_restore_is_exclusive_and_cli_restores_exact_tree(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            sample = root / "sample"
            make_sample(sample, text_phases())
            expected = (sample / "output-author.md").read_bytes()
            compress(sample)
            destination = root / "restored"
            self.assertEqual(
                compressor.main(
                    [
                        str(sample),
                        "--restore",
                        "author",
                        "--destination",
                        str(destination),
                    ]
                ),
                0,
            )
            self.assertEqual(
                directory_snapshot.read(destination), {Path("main.md"): expected}
            )
            existing = root / "existing"
            existing.mkdir()
            with self.assertRaises(FileExistsError):
                compressor.restore(sample, "initial", existing)


if __name__ == "__main__":
    unittest.main()
