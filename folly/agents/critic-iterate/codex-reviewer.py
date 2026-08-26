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
from datetime import date
from pathlib import Path
from typing import BinaryIO, Optional, Sequence, TextIO


CODEX = "codex"


REVIEW_PROFILES = {
    "fresh-review-preamble": None,
    "cold-review-preamble": "read-only",
}


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        allow_abbrev=False,
        usage=("%(prog)s --preamble-dir=PATH --preamble=NAME --workdir=PATH PROMPT"),
    )
    parser.add_argument("--preamble", required=True, choices=REVIEW_PROFILES)
    parser.add_argument("--preamble-dir", required=True, type=Path)
    parser.add_argument("--workdir", required=True, type=Path)
    parser.add_argument("prompt", type=Path)
    if len(argv) == 1 and argv[0] in ("-h", "--help"):
        parser.parse_args(argv)
    fixed_options = ("--preamble-dir=", "--preamble=", "--workdir=")
    if len(argv) != 4 or any(
        not argument.startswith(prefix) for argument, prefix in zip(argv, fixed_options)
    ):
        parser.error(
            "expected --preamble-dir=PATH --preamble=NAME --workdir=PATH PROMPT"
        )
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
    if not args.workdir.is_absolute():
        parser.error(f"workdir is not absolute: {args.workdir}")
    if not args.workdir.is_dir():
        parser.error(f"workdir is not a directory: {args.workdir}")
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
    # fixes their order and accepts only an absolute workdir and prompt path.
    (rules_dir / "default.rules").write_text(
        "host_executable("
        f"name={json.dumps(executable_name)}, "
        f"paths={json.dumps(executable_paths)})\n"
        "prefix_rule("
        f'pattern={json.dumps(fixed_prefix)}, decision="allow", '
        'justification="Fresh reviewer may start one fixed cold reviewer.")\n',
        encoding="utf-8",
    )


def _create_codex_home(
    output_dir: Path, args: argparse.Namespace, wrapper_executable: Path
) -> Path:
    codex_home = output_dir / "codex-home"
    codex_home.mkdir(mode=0o700)
    if args.preamble == "fresh-review-preamble":
        _install_cold_review_policy(codex_home, wrapper_executable, args.preamble_dir)
    return codex_home


def _review_command(sandbox: Optional[str], review_path: Path) -> list[str]:
    command = [CODEX]
    if sandbox is not None:
        command.extend(("-s", sandbox))
    # Reviews are headless, so an approval prompt cannot be answered. Both
    # preambles require the complete review in the final response, which
    # --output-last-message captures as review.md.
    command.extend(
        [
            "-a",
            "never",
            "exec",
            "--ignore-user-config",
            "--skip-git-repo-check",
            "--ephemeral",
            "--json",
            "--output-last-message",
            str(review_path),
            "-",
        ]
    )
    return command


def _execute_review(
    command: list[str],
    workdir: Path,
    codex_home: Path,
    prompt: BinaryIO,
    trace: BinaryIO,
    errors: TextIO,
) -> int:
    try:
        result = subprocess.run(
            command,
            cwd=workdir,
            env={
                **os.environ.copy(),
                "CODEX_HOME": str(codex_home),
            },
            stdin=prompt,
            stdout=trace,
            stderr=errors,
            check=False,
        )
    except OSError as error:
        print(f"could not run {CODEX}: {error}", file=errors)
        return 2
    return result.returncode


def run(args: argparse.Namespace, wrapper_executable: Path) -> int:
    sandbox = REVIEW_PROFILES[args.preamble]
    workdir = args.workdir
    if not workdir.is_dir():
        print(f"workdir is not a directory: {workdir}", file=sys.stderr)
        return 2
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

    output_root = Path.home() / ".codex" / "tmp"
    output_root.mkdir(mode=0o700, parents=True, exist_ok=True)

    # Prompts and traces may contain source text. Keep new files private to the
    # current user, including files that Codex itself creates.
    previous_umask = os.umask(0o077)
    try:
        output_dir = Path(
            tempfile.mkdtemp(
                prefix=f"{date.today():%Y%m%d}-codex-reviewer.", dir=output_root
            )
        )
        print(f"REVIEW_OUTPUT_DIR={output_dir}", flush=True)

        with (output_dir / "err.txt").open("x", encoding="utf-8") as errors:
            effective_prompt = output_dir / "effective-prompt.md"
            effective_prompt.write_bytes(preamble_contents + b"\n\n" + prompt_contents)

            # A private Codex home excludes user rules and executable config.
            # The selected workdir supplies repository instructions and the
            # base for relative paths.
            try:
                codex_home = _create_codex_home(output_dir, args, wrapper_executable)
            except OSError as error:
                print(f"could not create private Codex home: {error}", file=errors)
                return 2

            # Fresh review leaves `-s` unset because a read-only outer sandbox
            # blocks the nested cold review's private output. Cold review stays
            # explicitly read-only.
            review_path = output_dir / "review.md"
            command = _review_command(sandbox, review_path)
            # The wrapper fixes Codex's command and configuration but
            # deliberately inherits the caller's ordinary process environment.
            with (
                effective_prompt.open("rb") as prompt,
                (output_dir / "run.jsonl").open("xb") as trace,
            ):
                result = _execute_review(
                    command, workdir, codex_home, prompt, trace, errors
                )
            if result != 0:
                return result
            return _emit_final_review(review_path, errors)
    finally:
        os.umask(previous_umask)


def main() -> int:
    return run(parse_args(sys.argv[1:]), Path(os.path.abspath(__file__)))


if __name__ == "__main__":
    sys.exit(main())
