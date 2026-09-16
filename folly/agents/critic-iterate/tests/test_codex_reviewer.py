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

from __future__ import annotations

import importlib.util
import io
import subprocess
import tempfile
import unittest
from pathlib import Path
from typing import Any
from unittest import mock


MODULE_PATH = Path(__file__).resolve().parent.parent / "codex-reviewer.py"
SPEC = importlib.util.spec_from_file_location("codex_reviewer", MODULE_PATH)
assert SPEC is not None
assert SPEC.loader is not None
codex_reviewer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(codex_reviewer)

WILL_RETRY = b"WILL_RETRY"
WILL_NOT_RETRY = b"WILL_NOT_RETRY"


class FakeSleep:
    def __init__(self) -> None:
        self.delays: list[float] = []

    def __call__(self, delay: float) -> None:
        self.delays.append(delay)


def completed(returncode: int) -> subprocess.CompletedProcess[bytes]:
    return subprocess.CompletedProcess([], returncode)


class RetryTest(unittest.TestCase):
    def retry(
        self,
        outcomes: list[tuple[subprocess.CompletedProcess[bytes], bytes]],
        delays: tuple[tuple[float, float], ...] = ((1, 3), (4, 8)),
    ) -> tuple[subprocess.CompletedProcess[bytes], mock.Mock, mock.Mock, FakeSleep]:
        attempt = mock.Mock(side_effect=outcomes)
        on_retry = mock.Mock()
        sleep = FakeSleep()
        result = codex_reviewer._retry(
            attempt,
            delays=delays,
            should_retry=lambda stderr: stderr == WILL_RETRY,
            on_retry=on_retry,
            random_float=lambda: 0.5,
            sleep=sleep,
        )
        return result, attempt, on_retry, sleep

    def test_does_not_retry_other_outcomes(self) -> None:
        for returncode, stderr in (
            (0, WILL_RETRY),
            (6, WILL_NOT_RETRY),
        ):
            with self.subTest(returncode=returncode, stderr=stderr):
                result, attempt, on_retry, sleep = self.retry(
                    [(completed(returncode), stderr)]
                )
                self.assertEqual(result.returncode, returncode)
                attempt.assert_called_once_with()
                on_retry.assert_not_called()
                self.assertEqual(sleep.delays, [])

    def test_propagates_launch_failure(self) -> None:
        attempt = mock.Mock(side_effect=FileNotFoundError("codex"))
        sleep = FakeSleep()
        with self.assertRaises(FileNotFoundError):
            codex_reviewer._retry(
                attempt,
                delays=((1, 3),),
                should_retry=lambda stderr: stderr == WILL_RETRY,
                on_retry=mock.Mock(),
                sleep=sleep,
            )
        self.assertEqual(sleep.delays, [])

    def test_stops_after_configured_retries(self) -> None:
        failure = (completed(6), WILL_RETRY)
        result, attempt, on_retry, sleep = self.retry(
            [failure, failure, failure],
        )

        self.assertEqual(result.returncode, 6)
        self.assertEqual(attempt.call_count, 3)
        self.assertEqual(on_retry.call_count, 2)
        self.assertGreaterEqual(sum(sleep.delays), 1 + 4)
        self.assertLess(sum(sleep.delays), 3 + 8)

    def test_codex_policy_and_diagnostics(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        output_dir = Path(temporary.name)
        review_path = output_dir / "review.md"
        stderrs = iter(
            [
                b"Could not resolve host: first\n",
                b"Could not resolve host: second\n",
                b"Could not resolve host: third\n",
                b"final warning\n",
            ]
        )
        prompts = []

        def run(*_args: object, **kwargs: Any) -> subprocess.CompletedProcess[bytes]:
            stderr = next(stderrs)
            prompts.append(kwargs["stdin"].read())
            kwargs["stdout"].write(stderr)
            kwargs["stderr"].write(stderr)
            review_path.write_bytes(stderr)
            return completed(0 if stderr == b"final warning\n" else 6)

        errors_path = output_dir / "err.txt"
        sleep = FakeSleep()
        retry = codex_reviewer._retry

        def retry_without_wait(attempt: Any, **policy: Any) -> Any:
            self.assertTrue(
                policy["should_retry"](b"Could not resolve host: example.invalid")
            )
            self.assertFalse(policy["should_retry"](WILL_NOT_RETRY))
            return retry(
                attempt,
                **policy,
                random_float=lambda: 0.5,
                sleep=sleep,
            )

        with (
            mock.patch.object(codex_reviewer.isolated_agent, "CODEX") as codex,
            mock.patch.object(codex_reviewer, "_review_model", return_value="model"),
            mock.patch.object(codex_reviewer, "_retry", side_effect=retry_without_wait),
            mock.patch("sys.stdout", io.StringIO()),
            errors_path.open("w", encoding="utf-8") as errors,
        ):
            codex.prepare.return_value = mock.sentinel.workspace
            codex.run.side_effect = run
            result = codex_reviewer._run_review(
                mock.Mock(preamble="cold-review-preamble"),
                MODULE_PATH,
                output_dir,
                errors,
                b"prompt",
                b"preamble",
            )

        self.assertEqual(result, 0)
        self.assertEqual(prompts, [b"preamble\n\nprompt"] * 4)
        self.assertGreaterEqual(sum(sleep.delays), 5 + 15 + 45)
        self.assertLess(sum(sleep.delays), 15 + 45 + 120)
        self.assertEqual((output_dir / "run.jsonl").read_bytes(), b"final warning\n")
        self.assertEqual(review_path.read_bytes(), b"final warning\n")
        self.assertEqual(
            errors_path.read_text(),
            "Could not resolve host: first\n"
            "Codex infrastructure flakiness; retrying in 10.0 seconds.\n"
            "Could not resolve host: second\n"
            "Codex infrastructure flakiness; retrying in 30.0 seconds.\n"
            "Could not resolve host: third\n"
            "Codex infrastructure flakiness; retrying in 82.5 seconds.\n"
            "final warning\n",
        )


if __name__ == "__main__":
    unittest.main()
