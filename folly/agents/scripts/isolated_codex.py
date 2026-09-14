#!/usr/bin/python3 -I
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

"""Run isolated Codex evaluator sessions."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Sequence

# Evaluators also invoke this file directly from a source checkout.
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from folly.agents.scripts import isolated_agent  # noqa: E402


STATE_FILE = "isolated-codex.json"
TASK_ROOT_INSTRUCTION = (
    '`$W` is the task root. Start task commands with `cd "$W" &&`.\n\n'
)


def _write_state(path: Path, state: dict[str, object]) -> None:
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(state, indent=2, sort_keys=True) + "\n")
    temporary.replace(path)


def _read_state(root: Path) -> dict[str, object]:
    path = root / STATE_FILE
    try:
        state = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as error:
        raise isolated_agent.IsolationError(
            f"could not read evaluator state: {error}"
        ) from error
    if type(state) is not dict or state.get("engine") != isolated_agent.CODEX.name:
        raise isolated_agent.IsolationError("invalid evaluator state")
    return state


def _attempt_name(value: str) -> str:
    if re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", value) is None:
        raise argparse.ArgumentTypeError("attempt name must be a simple filename")
    return value


def _evaluator_request(
    root: Path,
    args: argparse.Namespace,
    resume: bool,
) -> tuple[isolated_agent.Request, Path]:
    state_path = root / STATE_FILE
    if not resume:
        if state_path.exists():
            raise isolated_agent.IsolationError("evaluator session has already started")
        return isolated_agent.Request(
            args.model, args.effort, access="read-only"
        ), isolated_agent.CODEX.resolve_executable()

    state = _read_state(root)
    model = state.get("model")
    effort = state.get("effort")
    session_id = state.get("session_id")
    executable = state.get("executable")
    if not (
        isinstance(model, str)
        and isinstance(effort, str)
        and isinstance(session_id, str)
        and isinstance(executable, str)
    ):
        raise isolated_agent.IsolationError("evaluator run has invalid session state")
    return (
        isolated_agent.Request(
            model, effort, access="read-only", resume_session_id=session_id
        ),
        Path(executable),
    )


def _run_evaluator(
    args: argparse.Namespace,
    resume: bool,
) -> int:
    workspace = isolated_agent.CODEX.load(args.root)
    state_path = workspace.root / STATE_FILE
    request, executable = _evaluator_request(workspace.root, args, resume)

    attempt = workspace.root / "attempts" / args.name
    attempt.mkdir(mode=0o700)
    response = attempt / "response.md"
    trace = attempt / "trace.jsonl"
    errors = attempt / "err.txt"
    prompt = attempt / "prompt.md"
    prompt.write_text(
        TASK_ROOT_INSTRUCTION + args.prompt.read_text(encoding="utf-8"),
        encoding="utf-8",
    )
    request = isolated_agent.Request(
        request.model,
        request.effort,
        access=request.access,
        resume_session_id=request.resume_session_id,
        response_path=response,
    )

    try:
        with (
            prompt.open("rb") as prompt_input,
            trace.open("xb") as trace_output,
            errors.open("xb") as error_output,
        ):
            result = isolated_agent.CODEX.run(
                workspace,
                request,
                stdin=prompt_input,
                stdout=trace_output,
                stderr=error_output,
                executable=executable,
            )
    except OSError as error:
        errors.write_text(f"could not run Codex: {error}\n")
        return 2

    if result.returncode:
        return result.returncode
    try:
        response_is_empty = not response.read_text(encoding="utf-8").strip()
        observed_session_id = isolated_agent.CODEX.extract_session_id(trace)
    except (OSError, ValueError) as error:
        with errors.open("ab") as error_output:
            error_output.write(f"could not validate result: {error}\n".encode())
        return 2
    validation_error = None
    if response_is_empty:
        validation_error = "Codex produced no final response."
    elif resume and observed_session_id not in (None, request.resume_session_id):
        validation_error = (
            f"Codex resumed as {observed_session_id!r}, "
            f"expected {request.resume_session_id!r}."
        )
    elif not resume and observed_session_id is None:
        validation_error = "Codex trace did not identify the initial session."
    if validation_error is not None:
        with errors.open("a", encoding="utf-8") as error_output:
            print(validation_error, file=error_output)
        return 2
    if not resume:
        _write_state(
            state_path,
            {
                "effort": request.effort,
                "engine": isolated_agent.CODEX.name,
                "executable": str(executable),
                "model": request.model,
                "session_id": observed_session_id,
            },
        )
    print(attempt)
    return 0


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="operation", required=True)
    prepare_parser = subparsers.add_parser("prepare")
    prepare_parser.add_argument("root", type=Path)

    run_parser = subparsers.add_parser("run")
    run_parser.add_argument("root", type=Path)
    run_parser.add_argument("--prompt", required=True, type=Path)
    run_parser.add_argument("--name", required=True, type=_attempt_name)
    run_parser.add_argument("--model", required=True)
    run_parser.add_argument("--effort", required=True)

    resume_parser = subparsers.add_parser("resume")
    resume_parser.add_argument("root", type=Path)
    resume_parser.add_argument("--prompt", required=True, type=Path)
    resume_parser.add_argument("--name", required=True, type=_attempt_name)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        if args.operation == "prepare":
            workspace = isolated_agent.CODEX.prepare(args.root)
            (workspace.root / "attempts").mkdir(mode=0o700)
            print(workspace.task)
            return 0
        return _run_evaluator(
            args,
            resume=args.operation == "resume",
        )
    except (isolated_agent.IsolationError, OSError, UnicodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
