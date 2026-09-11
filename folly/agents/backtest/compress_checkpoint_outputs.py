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

"""Compress checkpoint outputs in a saved backtest sample.

Usage: python3 -m folly.agents.backtest.compress_checkpoint_outputs SAMPLE

SAMPLE must contain uncompressed `output.md`, `checkpoints.json`, and every
artifact named by that JSON. The tool deduplicates exact content, preferring
`output.md` and otherwise the earliest phase file. For each remaining adjacent
change, it stores a later-to-earlier normal diff when the diff is less than 60%
of the earlier file. It verifies every reconstruction before deleting redundant
full files, removes stale `artifact` fields from `checkpoints.json`, and prints
the `## Checkpoints` README section.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Sequence, TextIO


_STAGED_SCRIPTS = Path(__file__).parent / "folly/agents/scripts"
_APPLY_DIFFS = (
    _STAGED_SCRIPTS / "apply_diffs"
    if _STAGED_SCRIPTS.is_dir()
    else Path(__file__).resolve().parent.parent / "scripts/apply_diffs"
)


@dataclass(frozen=True)
class Phase:
    name: str
    artifact: str
    contents: bytes


@dataclass(frozen=True)
class Representation:
    base: str
    diffs: tuple[str, ...] = ()


def _read_regular(path: Path) -> bytes:
    """Reject links and special files before reading destructive-work inputs."""
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"sample input is not a regular file: {path.name}")
    return path.read_bytes()


def _load(sample: Path) -> tuple[list[dict[str, Any]], list[Phase]]:
    """Read the uncompressed phase sequence and enforce its flat-file shape."""
    records = json.loads(_read_regular(sample / "checkpoints.json").decode())
    if not isinstance(records, list) or not records:
        raise ValueError("checkpoints.json must contain a nonempty list")

    phases = []
    for index, record in enumerate(records):
        if not isinstance(record, dict):
            raise ValueError("checkpoint record is not an object")
        name = record.get("phase")
        artifact = record.get("artifact")
        if index == 0:
            expected_name = "initial"
        elif index == 1:
            expected_name = "author"
        else:
            expected_name = f"review{index - 1}"
        if (
            not isinstance(name, str)
            or name != expected_name
            or not isinstance(artifact, str)
            or artifact != f"output-{name}.md"
        ):
            raise ValueError("checkpoint phase or artifact is invalid")
        phases.append(Phase(name, artifact, _read_regular(sample / artifact)))

    output = _read_regular(sample / "output.md")
    if output != phases[-1].contents:
        raise ValueError("output.md does not match the final checkpoint")
    return records, phases


def _use_diff(diff_size: int, full_size: int) -> bool:
    """Apply the strict 60% storage threshold without floating-point math."""
    return diff_size * 5 < full_size * 3


def _reverse_diff(sample: Path, later: Phase, earlier: Phase) -> bytes | None:
    """Describe one text transition, or retain non-text input as a full file."""
    if (
        b"\0" in later.contents
        or b"\0" in earlier.contents
        or not later.contents.endswith(b"\n")
        or not earlier.contents.endswith(b"\n")
    ):
        return None
    try:
        later.contents.decode()
        earlier.contents.decode()
    except UnicodeDecodeError:
        return None
    result = subprocess.run(
        ["diff", "--", later.artifact, earlier.artifact],
        cwd=sample,
        stdout=subprocess.PIPE,
    )
    if result.returncode not in (0, 1):
        result.check_returncode()
    return result.stdout


def _representations(sample: Path, phases: list[Phase]) -> list[Representation]:
    """Choose one verified full-or-diff representation for every phase."""
    output = (sample / "output.md").read_bytes()
    canonical = {output: "output.md"}
    for phase in phases:
        canonical.setdefault(phase.contents, phase.artifact)

    counts = Counter(phase.contents for phase in phases)
    by_contents = {output: Representation("output.md")}
    reverse_result = []
    for index in range(len(phases) - 1, -1, -1):
        phase = phases[index]
        representation = by_contents.get(phase.contents)
        if representation is None and counts[phase.contents] > 1:
            representation = Representation(canonical[phase.contents])
        if representation is None:
            later = phases[index + 1]
            later_representation = reverse_result[-1]
            diff = _reverse_diff(sample, later, phase)
            diff_name = f"output-{later.name}-to-{phase.name}.diff"
            if diff is not None and _use_diff(len(diff), len(phase.contents)):
                diff_path = sample / diff_name
                diff_path.unlink(missing_ok=True)
                diff_path.write_bytes(diff)
                representation = Representation(
                    later_representation.base,
                    (*later_representation.diffs, diff_name),
                )
            else:
                representation = Representation(canonical[phase.contents])
            by_contents[phase.contents] = representation
        reverse_result.append(representation)
    return list(reversed(reverse_result))


def _reconstruct(sample: Path, representation: Representation) -> bytes:
    """Use the reader-facing helper to verify one stored representation."""
    if not representation.diffs:
        return (sample / representation.base).read_bytes()
    result = subprocess.run(
        [str(_APPLY_DIFFS), representation.base, *representation.diffs],
        cwd=sample,
        check=True,
        stdout=subprocess.PIPE,
    )
    return result.stdout


def _mapping(sample: Path, phases: list[Phase], stored: list[Representation]) -> str:
    """Render the complete phase-to-storage map for the sample README."""
    helper = os.path.relpath(_APPLY_DIFFS, sample)
    lines = ["## Checkpoints", ""]
    if any(representation.diffs for representation in stored):
        lines.extend([f"Below, `apply_diffs` is short for `{helper}`.", ""])
    command_label_by_base = {}
    for phase, representation in zip(phases, stored):
        label = (
            "Initial draft"
            if phase.name == "initial"
            else "Author review"
            if phase.name == "author"
            else f"Review {phase.name.removeprefix('review')}"
        )
        if not representation.diffs:
            target = f"[{representation.base}]({representation.base})"
        elif representation.base not in command_label_by_base:
            command_label_by_base[representation.base] = label
            target = (
                f"`apply_diffs {representation.base} {' '.join(representation.diffs)}`"
            )
        else:
            command_label = command_label_by_base[representation.base]
            target = (
                f"truncate the {command_label} command after "
                f"`{representation.diffs[-1]}`"
            )
        lines.append(f"- **{label}:** {target}")
    return "\n".join(lines) + "\n"


def compress(sample: Path, mapping_output: TextIO) -> None:
    """Compress only after reconstruction and map output both succeed."""
    sample = sample.resolve()
    records, phases = _load(sample)
    stored = _representations(sample, phases)
    for phase, representation in zip(phases, stored):
        if _reconstruct(sample, representation) != phase.contents:
            raise ValueError(f"could not reconstruct phase {phase.name}")

    mapping_output.write(_mapping(sample, phases, stored))
    mapping_output.flush()

    retained = {
        "output.md",
        *(item.base for item in stored),
        *(diff for item in stored for diff in item.diffs),
    }
    for record in records:
        record.pop("artifact")
    accounting = sample / "checkpoints.json"
    temporary_accounting = sample / ".checkpoints.json.tmp"
    temporary_accounting.unlink(missing_ok=True)
    temporary_accounting.write_text(
        json.dumps(records, indent=2, sort_keys=True) + "\n"
    )
    temporary_accounting.replace(accounting)
    for phase in phases:
        if phase.artifact not in retained:
            (sample / phase.artifact).unlink()
    known_diffs = {
        f"output-{later.name}-to-{earlier.name}.diff"
        for earlier, later in zip(phases, phases[1:])
    }
    for diff in known_diffs - retained:
        (sample / diff).unlink(missing_ok=True)


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sample", type=Path)
    args = parser.parse_args(argv)
    compress(args.sample, sys.stdout)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
