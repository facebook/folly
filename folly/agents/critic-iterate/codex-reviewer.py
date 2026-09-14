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

"""Run Codex reviews through a fixed, allow-listable interface."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Optional, Sequence, TextIO

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))
import isolated_agent  # noqa: E402

REVIEW_MODEL_ENV = "CODEX_REVIEW_MODEL"
REVIEW_EFFORT = "high"
UNKNOWN_MODEL = "UNKNOWN-NOTIFY-USER"


REVIEW_PROFILES = {
    "fresh-review-preamble": None,
    "cold-review-preamble": "read-only",
}


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        allow_abbrev=False,
        usage="%(prog)s --preamble-dir=PATH --preamble=NAME PROMPT",
    )
    parser.add_argument("--preamble", required=True, choices=REVIEW_PROFILES)
    parser.add_argument("--preamble-dir", required=True, type=Path)
    parser.add_argument("prompt", type=Path)
    if len(argv) == 1 and argv[0] in ("-h", "--help"):
        parser.parse_args(argv)
    fixed_options = ("--preamble-dir=", "--preamble=")
    if len(argv) != 3 or any(
        not argument.startswith(prefix) for argument, prefix in zip(argv, fixed_options)
    ):
        parser.error("expected --preamble-dir=PATH --preamble=NAME PROMPT")
    args = parser.parse_args(argv)
    if not args.preamble_dir.is_absolute():
        parser.error(f"preamble directory is not absolute: {args.preamble_dir}")
    if not args.preamble_dir.is_dir():
        parser.error(f"preamble directory is not a directory: {args.preamble_dir}")
    preamble = args.preamble_dir / f"{args.preamble}.md"
    if not preamble.is_file():
        parser.error(f"preamble is not a regular file: {preamble}")
    if not args.prompt.is_absolute():
        parser.error(f"prompt is not absolute: {args.prompt}")
    if not args.prompt.is_file():
        parser.error(f"prompt is not a regular file: {args.prompt}")
    return args


def _emit_final_review(review_path: Path, errors: TextIO) -> int:
    try:
        review = review_path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        print(f"could not read final review {review_path}: {error}", file=errors)
        return 2
    if not review.strip():
        print(f"final review is empty or whitespace-only: {review_path}", file=errors)
        return 2
    print(review, end="" if review.endswith("\n") else "\n")
    return 0


def _install_cold_review_policy(
    codex_home: Path, wrapper_executable: Path, preamble_dir: Path
) -> None:
    invocation_path = Path(os.path.abspath(wrapper_executable))
    resolved_path = invocation_path.resolve(strict=True)
    executable_name = invocation_path.name
    executable_paths = sorted({str(invocation_path), str(resolved_path)})
    fixed_prefix = [
        executable_name,
        f"--preamble-dir={preamble_dir}",
        "--preamble=cold-review-preamble",
    ]
    rules_dir = codex_home / "rules"
    rules_dir.mkdir(mode=0o700)
    # Exec-policy prefixes cannot restrict trailing arguments. parse_args()
    # fixes their order and accepts only an absolute prompt path.
    (rules_dir / "default.rules").write_text(
        "host_executable("
        f"name={json.dumps(executable_name)}, "
        f"paths={json.dumps(executable_paths)})\n"
        "prefix_rule("
        f'pattern={json.dumps(fixed_prefix)}, decision="allow", '
        'justification="Fresh reviewer may start one fixed cold reviewer.")\n',
        encoding="utf-8",
    )


def _review_model(wrapper_executable: Path) -> Optional[str]:
    """Return the invoking model; None lets Codex choose its default."""
    session_id = os.environ.get("CODEX_THREAD_ID") or os.environ.get("CODEX_SESSION_ID")
    if session_id:
        helper = wrapper_executable.with_name("session_current_model_id.py")
        result = subprocess.run(
            [str(helper), session_id],
            check=False,
            stderr=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
        )
        model = result.stdout.strip()
        if result.returncode == 0 and model and model != UNKNOWN_MODEL:
            return model
    inherited_model = os.environ.get(REVIEW_MODEL_ENV)
    if inherited_model:
        return inherited_model
    return None


def _review_environment_additions() -> dict[str, str]:
    return {
        name: os.environ[name]
        for name in ("FOLLY_BACKTEST_RUN_DIR", "FOLLY_BACKTEST_WORKDIR")
        if name in os.environ
    }


def _run_review(
    args: argparse.Namespace,
    wrapper_executable: Path,
    output_dir: Path,
    errors: TextIO,
    prompt_contents: bytes,
    preamble_contents: bytes,
) -> int:
    sandbox = REVIEW_PROFILES[args.preamble]
    with tempfile.TemporaryDirectory(prefix="codex-review.") as run_root:
        try:
            workspace = isolated_agent.CODEX.prepare(Path(run_root))
        except (OSError, isolated_agent.IsolationError) as error:
            print(f"could not create private Codex workspace: {error}", file=errors)
            return 2
        model = _review_model(wrapper_executable)
        if model is None:
            print(
                "IMPORTANT FALLBACK: report in the final debrief that the "
                "caller model was unavailable and Codex used its default.",
                file=sys.stderr,
            )
        (output_dir / "metadata.json").write_text(
            json.dumps(
                {
                    "model": model or "Codex default",
                    "reasoning_effort": REVIEW_EFFORT,
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
        )
        effective_prompt = output_dir / "effective-prompt.md"
        effective_prompt.write_bytes(preamble_contents + b"\n\n" + prompt_contents)

        try:
            if args.preamble == "fresh-review-preamble":
                _install_cold_review_policy(
                    workspace.home, wrapper_executable, args.preamble_dir
                )
        except OSError as error:
            print(f"could not create private Codex home: {error}", file=errors)
            return 2

        review_path = output_dir / "review.md"
        with (
            effective_prompt.open("rb") as prompt,
            (output_dir / "run.jsonl").open("xb") as trace,
        ):
            try:
                result = isolated_agent.CODEX.run(
                    workspace,
                    # Fresh review must launch its nested reviewer; cold review
                    # has no such write and stays read-only.
                    isolated_agent.Request(
                        model=model,
                        effort=REVIEW_EFFORT,
                        access=sandbox or "default",
                        ephemeral=True,
                        response_path=review_path,
                    ),
                    stdin=prompt,
                    stdout=trace,
                    stderr=errors.buffer,
                    additions=_review_environment_additions(),
                )
            except (OSError, isolated_agent.IsolationError) as error:
                print(f"could not run Codex: {error}", file=errors)
                return 2
        if result.returncode != 0:
            return result.returncode
        return _emit_final_review(review_path, errors)


def run(args: argparse.Namespace, wrapper_executable: Path) -> int:
    try:
        prompt_contents = args.prompt.read_bytes()
    except OSError as error:
        print(f"could not read prompt {args.prompt}: {error}", file=sys.stderr)
        return 2
    preamble = args.preamble_dir / f"{args.preamble}.md"
    try:
        preamble_contents = preamble.read_bytes()
    except OSError as error:
        print(f"could not read preamble {preamble}: {error}", file=sys.stderr)
        return 2

    # Prompts and traces may contain source text. Keep new files private to the
    # current user, including files that Codex itself creates.
    previous_umask = os.umask(0o077)
    try:
        # Codex drops TMPDIR. Backtests use their run root so fresh and nested
        # reviewers become sibling attempts that accounting can order.
        run_root = os.environ.get("FOLLY_BACKTEST_RUN_DIR")
        output_dir = Path(
            tempfile.mkdtemp(
                prefix="codex-reviewer.",
                dir=Path(run_root) / "reviews" if run_root else None,
            )
        )
        print(f"REVIEW_OUTPUT_DIR={output_dir}", flush=True)

        with (output_dir / "err.txt").open("x", encoding="utf-8") as errors:
            return _run_review(
                args,
                wrapper_executable,
                output_dir,
                errors,
                prompt_contents,
                preamble_contents,
            )
    finally:
        os.umask(previous_umask)


def main() -> int:
    return run(parse_args(sys.argv[1:]), Path(os.path.abspath(__file__)))


if __name__ == "__main__":
    sys.exit(main())
