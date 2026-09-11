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

import json
import os
import tempfile
import unittest
from datetime import datetime, timezone
from pathlib import Path

from folly.agents.backtest import checkpoint, checkpoint_accounting


THREAD_ID = "01a00000-0000-7000-8000-000000000000"


def usage(input_tokens: int, output_tokens: int) -> dict[str, int]:
    return {
        "input_tokens": input_tokens,
        "cached_input_tokens": input_tokens // 2,
        "cache_write_input_tokens": 0,
        "output_tokens": output_tokens,
        "reasoning_output_tokens": output_tokens // 2,
    }


def write_jsonl(path: Path, events: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("".join(json.dumps(event) + "\n" for event in events))


def checkpoint_event(index: int, exit_code: int = 0) -> dict[str, object]:
    return {
        "type": "event_msg",
        "payload": {
            "type": "item_completed",
            "item": {
                "type": "CommandExecution",
                "stdout": (
                    f"{checkpoint.CHECKPOINT_MARKER_PREFIX}{index}"
                    f"{checkpoint.CHECKPOINT_MARKER_SUFFIX}\nnext instruction\n"
                ),
                "exit_code": exit_code,
            },
        },
    }


def token_event(tokens: dict[str, int], second: int) -> dict[str, object]:
    return {
        "timestamp": f"2026-09-04T00:00:{second:02d}Z",
        "type": "event_msg",
        "payload": {
            "type": "token_count",
            "info": {
                "total_token_usage": tokens,
                "model_context_window": 1000,
            },
        },
    }


class AccountingFixture:
    """Build the three evidence streams around a visible sequence of phases."""

    def __init__(self, root: Path) -> None:
        self.root = root
        self.workdir = root / "workdir"
        self.codex_home = root / "codex-home"
        self.checkpoints = root / "checkpoints"
        self.reviews = root / "reviews"
        self.workdir.mkdir()
        self.checkpoints.mkdir()
        self.reviews.mkdir()
        self.author_events: list[dict[str, object]] = [
            {
                "timestamp": "2026-09-04T00:00:00Z",
                "type": "session_meta",
            }
        ]
        self.checkpoint_count = 0
        self.review_count = 0

    @staticmethod
    def _set_time(path: Path, second: int) -> None:
        timestamp = datetime(2026, 9, 4, 0, 0, second, tzinfo=timezone.utc).timestamp()
        os.utime(path, (timestamp, timestamp))

    def checkpoint(self, text: str, tokens: dict[str, int], second: int) -> None:
        index = self.checkpoint_count
        snapshot = self.checkpoints / f"{index}.md"
        snapshot.write_text(text)
        self._set_time(snapshot, second)
        (self.workdir / "output.md").write_text(text)
        self.author_events.extend(
            (
                checkpoint_event(index),
                token_event(tokens, second),
            )
        )
        self.checkpoint_count += 1

    def review(self, tokens: dict[str, int] | None, second: int) -> None:
        directory = self.reviews / str(self.review_count)
        directory.mkdir()
        if tokens is None:
            self._set_time(directory, second)
        else:
            trace = directory / "run.jsonl"
            write_jsonl(trace, [{"type": "turn.completed", "usage": tokens}])
            self._set_time(trace, second)
        self.review_count += 1

    def collect(
        self, review_budget: int | None, final_message: str = "Done."
    ) -> list[dict[str, object]]:
        write_jsonl(
            self.root / "trace.jsonl",
            [
                {"type": "thread.started", "thread_id": THREAD_ID},
                {
                    "type": "item.completed",
                    "item": {"type": "agent_message", "text": final_message},
                },
            ],
        )
        write_jsonl(
            self.codex_home / "sessions/2026/09/04" / f"rollout-test-{THREAD_ID}.jsonl",
            self.author_events,
        )
        return checkpoint_accounting.collect(self.root, review_budget)


class CheckpointAccountingTest(unittest.TestCase):
    def test_word_change_ignores_markdown_reflow(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            before = root / "before.md"
            after = root / "after.md"
            before.write_text("> one two\n> three\n")
            after.write_text("> one\n> two three\n")

            self.assertEqual(
                checkpoint_accounting._changed_word_percent(before, after), 0.0
            )

    def test_word_change_is_bounded_for_reordering(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            before = root / "before.md"
            after = root / "after.md"
            before.write_text("one two three\n")
            after.write_text("three two one\n")

            self.assertEqual(
                checkpoint_accounting._changed_word_percent(before, after), 66.7
            )

    def test_tracks_two_review_rounds(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            run = AccountingFixture(Path(temporary))
            run.checkpoint("initial", usage(100, 10), 10)
            run.checkpoint("author", usage(250, 20), 20)
            run.review(usage(50, 6), 25)
            run.review(usage(30, 4), 24)
            run.checkpoint("reviewed once", usage(450, 30), 30)
            run.review(usage(20, 2), 34)
            run.review(usage(10, 1), 35)
            run.checkpoint("reviewed twice", usage(600, 40), 40)
            run.author_events.append(token_event(usage(650, 45), 45))

            records = run.collect(review_budget=2)

            self.assertEqual(
                [record["phase"] for record in records],
                ["initial", "author", "review1", "review2"],
            )
            self.assertEqual(
                [record["outcome"] for record in records],
                ["continued", "continued", "continued", "converged"],
            )
            self.assertEqual(
                [record["changed_word_percent"] for record in records],
                [None, 100.0, 100.0, 50.0],
            )
            self.assertEqual(records[2]["author_tokens"], usage(200, 10))
            self.assertEqual(records[3]["author_tokens"], usage(200, 15))
            self.assertEqual(
                [record["reviewer_tokens"] for record in records],
                [
                    [],
                    [],
                    [usage(30, 4), usage(50, 6)],
                    [usage(20, 2), usage(10, 1)],
                ],
            )
            self.assertEqual(records[2]["wall_seconds"], 10.0)
            self.assertEqual(records[3]["wall_seconds"], 15.0)
            self.assertEqual(
                (run.root / "output-review2.md").read_text(), "reviewed twice"
            )
            self.assertEqual(
                json.loads((run.root / "checkpoints.json").read_text()), records
            )

    def test_counts_distinguish_initial_only_from_ci_zero(self) -> None:
        for budget, contents in (
            (None, ("initial",)),
            (0, ("initial", "author")),
        ):
            with (
                self.subTest(budget=budget),
                tempfile.TemporaryDirectory() as temporary,
            ):
                root = Path(temporary)
                (root / "checkpoints").mkdir()
                (root / "workdir").mkdir()
                for index, content in enumerate(contents):
                    (root / f"checkpoints/{index}.md").write_text(content)
                (root / "workdir/output.md").write_text(contents[-1])

                paths = checkpoint_accounting._checkpoint_paths(root, budget)

                self.assertEqual([path.read_text() for path in paths], list(contents))

    def test_failed_review_is_recorded_before_completed_retry(self) -> None:
        # A reviewer can abort and be retried before the author checkpoints.
        # Keep both attempts; the completed retry validates checkpoint 2 as `review1`.
        with tempfile.TemporaryDirectory() as temporary:
            run = AccountingFixture(Path(temporary))
            run.checkpoint("initial", usage(100, 10), 10)
            run.checkpoint("author", usage(250, 20), 20)
            run.review(None, 22)
            retry_tokens = usage(50, 6)
            run.review(retry_tokens, 25)
            run.checkpoint("reviewed", usage(450, 30), 30)
            run.author_events.append(token_event(usage(500, 35), 35))

            records = run.collect(review_budget=1)

            self.assertEqual(records[2]["reviewer_tokens"], [{}, retry_tokens])

    def test_failed_review_does_not_complete_review_phase(self) -> None:
        # Checkpoints carry only indexes: 0 is `initial`, 1 is `author`, and 2
        # is inferred to be `review1`. If the reviewer aborts and the author
        # still checkpoints, accepting index 2 would mislabel unreviewed work.
        with tempfile.TemporaryDirectory() as temporary:
            run = AccountingFixture(Path(temporary))
            run.checkpoint("initial", usage(100, 10), 10)
            run.checkpoint("author", usage(250, 20), 20)
            run.review(None, 22)
            run.checkpoint("not reviewed", usage(450, 30), 30)
            run.author_events.append(token_event(usage(500, 35), 35))

            with self.assertRaisesRegex(ValueError, "reviewer count"):
                run.collect(review_budget=1)

    def test_requires_usage_after_the_final_checkpoint(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            run = AccountingFixture(Path(temporary))
            run.checkpoint("initial", usage(100, 10), 10)

            with self.assertRaisesRegex(
                ValueError, "final response has no token usage"
            ):
                run.collect(review_budget=None)

    def test_outcomes_distinguish_budget_continuation_and_stop(self) -> None:
        cases = (
            ((2, 3, 2, ""), "continued"),
            ((2, 3, 1, ""), "must-continue"),
            ((2, 2, 2, ""), "converged"),
            ((2, 2, 2, "> OutOfBudget: no rounds remain."), "budget-stop"),
            ((2, 2, 2, "The rule contains OutOfBudget: literally."), "budget-stop"),
            ((2, 2, 2, "The rule contains OutOfBudget literally."), "converged"),
        )
        for arguments, expected in cases:
            with self.subTest(expected=expected):
                self.assertEqual(checkpoint_accounting._outcome(*arguments), expected)

    def test_tokens_preserve_zero_and_require_every_field(self) -> None:
        tokens = usage(0, 0)
        self.assertEqual(checkpoint_accounting._tokens(tokens), tokens)
        del tokens["cache_write_input_tokens"]
        with self.assertRaises(KeyError):
            checkpoint_accounting._tokens(tokens)

    def test_failed_marker_cannot_reuse_the_last_snapshot(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            run = AccountingFixture(Path(temporary))
            run.checkpoint("initial", usage(100, 10), 10)
            run.author_events.append(checkpoint_event(1, exit_code=1))

            with self.assertRaisesRegex(ValueError, "checkpoint command failed"):
                run.collect(review_budget=None)
