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

import json
import mmap
import os
import re
import sys
from pathlib import Path
from typing import Iterator, Optional


UNKNOWN_MODEL = "UNKNOWN-NOTIFY-USER"
UUID_PATTERN = re.compile(
    r"[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-"
    r"[0-9a-fA-F]{4}-[0-9a-fA-F]{12}"
)


def reversed_lines(path: Path) -> Iterator[bytes]:
    with path.open("rb") as session_file:
        if os.fstat(session_file.fileno()).st_size == 0:
            return
        with mmap.mmap(session_file.fileno(), 0, access=mmap.ACCESS_READ) as contents:
            end = len(contents)
            while end > 0:
                if contents[end - 1 : end] == b"\n":
                    end -= 1
                    if end == 0:
                        return
                start = contents.rfind(b"\n", 0, end)
                yield contents[start + 1 : end]
                end = start if start >= 0 else 0


def latest_model(path: Path, trace_type: str) -> Optional[str]:  # noqa: C901
    try:
        for line in reversed_lines(path):
            try:
                event = json.loads(line)
            except (json.JSONDecodeError, UnicodeDecodeError):
                continue
            if not isinstance(event, dict):
                continue

            model = None
            if trace_type == "codex" and event.get("type") == "turn_context":
                payload = event.get("payload")
                if isinstance(payload, dict):
                    model = payload.get("model")
            elif trace_type == "claude" and event.get("type") == "assistant":
                message = event.get("message")
                if isinstance(message, dict):
                    model = message.get("model")
                if model is None:
                    model = event.get("model")

            # Claude uses angle-bracketed model values such as <synthetic> for
            # synthetic assistant events.
            if isinstance(model, str) and model and not model.startswith("<"):
                return model
    except (OSError, ValueError):
        pass
    return None


def roots(env_var: str, default: Path) -> list[Path]:
    configured = os.environ.get(env_var)
    candidates = [Path(configured), default] if configured else [default]
    # A nested reviewer can override its config root while the parent trace
    # remains under the default root.
    return list(dict.fromkeys(path.expanduser().resolve() for path in candidates))


def session_files(session_uuid: str) -> Iterator[tuple[Path, str]]:
    candidates = []
    home = Path.home()
    for root in roots("CODEX_HOME", home / ".codex"):
        for path in (root / "sessions").rglob("*.jsonl"):
            if session_uuid in path.name.lower():
                try:
                    candidates.append((path.stat().st_mtime_ns, path, "codex"))
                except OSError:
                    continue
    for root in roots("CLAUDE_CONFIG_DIR", home / ".claude"):
        for path in (root / "projects").rglob("*.jsonl"):
            if session_uuid in path.name.lower():
                try:
                    candidates.append((path.stat().st_mtime_ns, path, "claude"))
                except OSError:
                    continue

    # Check newer trace files first, falling back when they have no model yet.
    for _, path, trace_type in sorted(
        candidates, key=lambda candidate: candidate[0], reverse=True
    ):
        yield path, trace_type


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {Path(sys.argv[0]).name} SESSION_UUID", file=sys.stderr)
        return 2

    if UUID_PATTERN.fullmatch(sys.argv[1]) is None:
        print(UNKNOWN_MODEL)
        return 0
    session_uuid = sys.argv[1].lower()

    for path, trace_type in session_files(session_uuid):
        model = latest_model(path, trace_type)
        if model is not None:
            print(model)
            return 0
    print(UNKNOWN_MODEL)
    return 0


if __name__ == "__main__":
    sys.exit(main())
