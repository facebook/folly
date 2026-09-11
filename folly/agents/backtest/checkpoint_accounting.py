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

"""Build phase artifacts and costs from a checkpointed backtest."""

from __future__ import annotations

import json
import re
import shutil
from datetime import datetime, timezone
from difflib import SequenceMatcher
from pathlib import Path
from typing import Any

from .checkpoint import CHECKPOINT_MARKER_PREFIX, CHECKPOINT_MARKER_SUFFIX


TOKEN_FIELDS = (
    "input_tokens",
    "cached_input_tokens",
    "cache_write_input_tokens",
    "output_tokens",
    "reasoning_output_tokens",
)


# Decode Codex traces here so phase accounting below does not depend on their
# wire format.
def _read_jsonl(path: Path) -> list[dict[str, Any]]:
    return [json.loads(line) for line in path.read_text().splitlines()]


def _tokens(value: Any) -> dict[str, int]:
    """Preserve every billed token category; absent fields mean wire drift."""
    result = {field: value[field] for field in TOKEN_FIELDS}
    assert all(type(count) is int and count >= 0 for count in result.values())
    return result


def _public_run(path: Path) -> tuple[str, str]:
    """Link the public trace to its private rollout and budget-stop notice."""
    events = _read_jsonl(path)
    (thread_id,) = {
        event["thread_id"] for event in events if event.get("type") == "thread.started"
    }
    messages = []
    for event in events:
        if event.get("type") != "item.completed":
            continue
        item = event["item"]
        if item.get("type") != "agent_message":
            continue
        messages.append(item["text"])
    final_message = messages[-1]
    assert isinstance(final_message, str)
    return thread_id, final_message


def _checkpoint_marker(event: dict[str, Any]) -> int | None:
    """Read the marker emitted after a checkpoint is saved."""
    if event.get("type") != "event_msg":
        return None
    payload = event["payload"]
    if payload.get("type") != "item_completed":
        return None
    item = payload["item"]
    if item.get("type") != "CommandExecution":
        return None
    marker = item["stdout"].partition("\n")[0]
    if not marker.startswith(CHECKPOINT_MARKER_PREFIX) or not marker.endswith(
        CHECKPOINT_MARKER_SUFFIX
    ):
        return None
    if item["exit_code"] != 0:
        raise ValueError("checkpoint command failed")
    index_text = marker.removeprefix(CHECKPOINT_MARKER_PREFIX).removesuffix(
        CHECKPOINT_MARKER_SUFFIX
    )
    return int(index_text)


def _token_usage(
    event: dict[str, Any],
) -> tuple[dict[str, int], int, datetime] | None:
    """Read one cumulative author-usage update from the private trace."""
    if event.get("type") != "event_msg":
        return None
    payload = event["payload"]
    if payload.get("type") != "token_count":
        return None
    info = payload["info"]
    if info is None:
        return None
    context_window = info["model_context_window"]
    assert type(context_window) is int and context_window > 0
    return (
        _tokens(info["total_token_usage"]),
        context_window,
        datetime.fromisoformat(event["timestamp"].replace("Z", "+00:00")),
    )


def _author_checkpoints(
    codex_home: Path, thread_id: str
) -> tuple[
    datetime,
    list[tuple[dict[str, int], int]],
    tuple[dict[str, int], int, datetime],
]:
    """Pair checkpoints with usage and retain the final delivery cost."""
    (rollout,) = (codex_home / "sessions").glob(f"*/*/*/rollout-*-{thread_id}.jsonl")
    events = _read_jsonl(rollout)
    (started,) = (
        event["timestamp"] for event in events if event.get("type") == "session_meta"
    )
    started_at = datetime.fromisoformat(started.replace("Z", "+00:00"))

    indices = []
    checkpoints = []
    pending = False
    final_usage = None
    for event in events:
        index = _checkpoint_marker(event)
        if index is not None:
            if pending:
                raise ValueError("checkpoint commands overlap")
            indices.append(index)
            pending = True
            continue
        usage = _token_usage(event)
        if usage is not None:
            if pending:
                checkpoints.append(usage[:2])
                pending = False
                final_usage = None
            else:
                final_usage = usage

    if pending:
        raise ValueError("final checkpoint has no following token event")
    if final_usage is None:
        raise ValueError("final response has no token usage")
    if indices != list(range(len(checkpoints))):
        raise ValueError("rollout checkpoints are not a complete sequence")
    return started_at, checkpoints, final_usage


def _review_attempts(root: Path) -> list[tuple[datetime, dict[str, int]]]:
    """Keep failed attempts visible; a completed retry can still finish the phase."""
    attempts = []
    for directory in root.iterdir():
        trace = directory / "run.jsonl"
        if trace.is_file():
            completed = [
                event
                for event in _read_jsonl(trace)
                if event.get("type") == "turn.completed"
            ]
            completed_at = trace.stat().st_mtime
        else:
            completed = []
            completed_at = directory.stat().st_mtime
        # One Codex invocation can complete at most one review turn.
        assert len(completed) <= 1
        attempts.append(
            (
                datetime.fromtimestamp(completed_at, timezone.utc),
                _tokens(completed[0]["usage"]) if completed else {},
            )
        )
    return sorted(attempts, key=lambda attempt: attempt[0])


def _outcome(
    index: int, final_index: int, review_budget: int | None, final_message: str
) -> str:
    """Distinguish ordinary progress, safety continuation, and budget stops."""
    if index < final_index:
        if review_budget is not None and index >= 2 and index - 1 >= review_budget:
            return "must-continue"
        return "continued"
    if "OutOfBudget:" in final_message:
        return "budget-stop"
    return "converged"


def _checkpoint_paths(
    run_root: Path,
    review_budget: int | None,
) -> list[Path]:
    """Establish one gap-free snapshot sequence ending at the delivered output."""
    paths = sorted(
        (run_root / "checkpoints").glob("*.md"), key=lambda path: int(path.stem)
    )
    if not paths or [path.name for path in paths] != [
        f"{index}.md" for index in range(len(paths))
    ]:
        raise ValueError("checkpoints are not a gap-free sequence")
    if review_budget == 0 and len(paths) != 2:
        raise ValueError("checkpoint count does not match the review mode")
    if review_budget is not None and review_budget > 0 and len(paths) < 3:
        raise ValueError("checkpoint count does not match the review mode")
    if (run_root / "workdir/output.md").read_bytes() != paths[-1].read_bytes():
        raise ValueError("output.md changed after the final checkpoint")
    return paths


def _review_usage_by_checkpoint(
    attempts: list[tuple[datetime, dict[str, int]]],
    captured_at: list[datetime],
) -> list[list[dict[str, int]]]:
    """Charge every attempt to the checkpoint its retry sequence produced."""
    usage = []
    attempt_index = 0
    for checkpoint_index, checkpoint_time in enumerate(captured_at):
        checkpoint_attempts = []
        while (
            attempt_index < len(attempts)
            and attempts[attempt_index][0] <= checkpoint_time
        ):
            checkpoint_attempts.append(attempts[attempt_index][1])
            attempt_index += 1
        # `{}` records an unfinished attempt, not a completed review.
        if (checkpoint_index < 2 and checkpoint_attempts) or (
            checkpoint_index >= 2 and not any(checkpoint_attempts)
        ):
            raise ValueError(
                f"reviewer count does not match checkpoint {checkpoint_index}"
            )
        usage.append(checkpoint_attempts)
    if attempt_index != len(attempts):
        raise ValueError("review attempt occurred after the final checkpoint")
    return usage


def _changed_word_percent(before: Path | None, after: Path) -> float | None:
    """Measure changed words without counting Markdown reflow as a change."""
    if before is None:
        return None
    pattern = r"\w+(?:[-'./:]+\w+)*"
    old_words = re.findall(pattern, before.read_text())
    new_words = re.findall(pattern, after.read_text())
    matcher = SequenceMatcher(None, old_words, new_words, autojunk=False)
    unchanged = sum(block.size for block in matcher.get_matching_blocks())
    changed = max(len(old_words), len(new_words)) - unchanged
    return round(100 * changed / max(len(old_words), len(new_words), 1), 1)


def _write_phase_records(
    run_root: Path,
    paths: list[Path],
    completed_at: list[datetime],
    started_at: datetime,
    author_checkpoints: list[tuple[dict[str, int], int]],
    review_usage: list[list[dict[str, int]]],
    review_budget: int | None,
    final_message: str,
) -> list[dict[str, object]]:
    """Write named phase drafts while keeping time and token cost incremental."""
    records: list[dict[str, object]] = []
    previous_author = dict.fromkeys(TOKEN_FIELDS, 0)
    previous_path: Path | None = None
    previous_time = started_at
    for index, (
        path,
        completed,
        (author_cumulative, context_window),
        reviewer_tokens,
    ) in enumerate(zip(paths, completed_at, author_checkpoints, review_usage)):
        author_tokens = {
            field: author_cumulative[field] - previous_author[field]
            for field in TOKEN_FIELDS
        }
        if completed < previous_time or any(
            count < 0 for count in author_tokens.values()
        ):
            raise ValueError("checkpoint time or token count moved backward")

        phase = "initial" if index == 0 else "author"
        if index >= 2:
            phase = f"review{index - 1}"
        artifact = f"output-{phase}.md"
        shutil.copyfile(path, run_root / artifact)
        records.append(
            {
                "artifact": artifact,
                "author_tokens": author_tokens,
                "changed_word_percent": _changed_word_percent(previous_path, path),
                "context_window": context_window,
                "outcome": _outcome(
                    index, len(paths) - 1, review_budget, final_message
                ),
                "phase": phase,
                "reviewer_tokens": reviewer_tokens,
                "wall_seconds": round((completed - previous_time).total_seconds(), 3),
            }
        )
        previous_author = author_cumulative
        previous_path = path
        previous_time = completed
    return records


def collect(
    run_root: Path,
    review_budget: int | None,
) -> list[dict[str, object]]:
    """Materialize accounting only when snapshots, author, and reviews agree."""
    paths = _checkpoint_paths(run_root, review_budget)

    thread_id, final_message = _public_run(run_root / "trace.jsonl")
    started_at, author_checkpoints, final_usage = _author_checkpoints(
        run_root / "codex-home", thread_id
    )
    if len(author_checkpoints) != len(paths):
        raise ValueError("rollout checkpoints do not match saved snapshots")
    captured_at = [
        datetime.fromtimestamp(path.stat().st_mtime, timezone.utc) for path in paths
    ]
    review_usage = _review_usage_by_checkpoint(
        _review_attempts(run_root / "reviews"), captured_at
    )
    final_tokens, final_context_window, finished_at = final_usage
    if finished_at < captured_at[-1]:
        raise ValueError("final token event precedes the final checkpoint")
    phase_usage = [
        *author_checkpoints[:-1],
        (final_tokens, final_context_window),
    ]
    completed_at = [*captured_at[:-1], finished_at]
    records = _write_phase_records(
        run_root,
        paths,
        completed_at,
        started_at,
        phase_usage,
        review_usage,
        review_budget,
        final_message,
    )
    (run_root / "checkpoints.json").write_text(
        json.dumps(records, indent=2, sort_keys=True) + "\n"
    )
    return records
