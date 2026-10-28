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

Usage: python3 -m folly.agents.tests.compress_checkpoint_outputs SAMPLE

SAMPLE must contain uncompressed `output/main.md`, `checkpoints.json`, and every
artifact named by that JSON. The tool deduplicates exact content, preferring
`output/main.md` and otherwise the earliest phase file. For each remaining adjacent
change, it stores an earlier-to-later normal diff when the diff is less than 60%
of the later file. It verifies every reconstruction before deleting redundant
full files, removes stale `artifact` fields from `checkpoints.json`, and prints
the `## Checkpoints` README section. File membership in `checkpoints.json`
lets `SAMPLE --restore PHASE --destination PATH` restore a phase as output files.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from collections import Counter
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Sequence, TextIO

from folly.agents.tests import directory_snapshot


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


def _read_regular(sample: Path, relative: str | Path) -> bytes:
    parent = sample
    if parent.is_symlink() or not parent.is_dir():
        raise ValueError(f"sample input is not a regular directory: {parent}")
    for component in Path(relative).parts[:-1]:
        parent /= component
        if parent.is_symlink() or not parent.is_dir():
            raise ValueError(f"sample input is not a regular directory: {parent}")
    path = sample / relative
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"sample input is not a regular file: {path}")
    return path.read_bytes()


def _load(sample: Path) -> tuple[list[dict[str, Any]], list[Phase]]:
    """Read the uncompressed phase sequence and enforce its flat-file shape."""
    records = json.loads(_read_regular(sample, "checkpoints.json").decode())
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
        phases.append(Phase(name, artifact, _read_regular(sample, artifact)))

    output = _read_regular(sample, "output/main.md")
    if output != phases[-1].contents:
        raise ValueError("output/main.md does not match the final checkpoint")
    return records, phases


def _use_diff(diff_size: int, full_size: int) -> bool:
    """Apply the strict 60% storage threshold without floating-point math."""
    return diff_size * 5 < full_size * 3


def _forward_diff(
    sample: Path, earlier: Path, later: Path, old: bytes, new: bytes
) -> bytes | None:
    if (
        b"\0" in old
        or b"\0" in new
        or not old.endswith(b"\n")
        or not new.endswith(b"\n")
    ):
        return None
    try:
        old.decode()
        new.decode()
    except UnicodeDecodeError:
        return None
    result = subprocess.run(
        ["diff", "--", str(earlier), str(later)],
        cwd=sample,
        stdout=subprocess.PIPE,
    )
    if result.returncode not in (0, 1):
        result.check_returncode()
    return result.stdout


def _representations(sample: Path, phases: list[Phase]) -> list[Representation]:
    """Choose one verified full-or-diff representation for every phase."""
    output = (sample / "output/main.md").read_bytes()
    canonical = {output: "output/main.md"}
    for phase in phases:
        canonical.setdefault(phase.contents, phase.artifact)

    counts = Counter(phase.contents for phase in phases)
    by_contents = {output: Representation("output/main.md")}
    result = []
    for index, phase in enumerate(phases):
        representation = by_contents.get(phase.contents)
        if representation is None and counts[phase.contents] > 1:
            representation = Representation(canonical[phase.contents])
        if representation is None and index:
            earlier = phases[index - 1]
            diff = _forward_diff(
                sample,
                Path(earlier.artifact),
                Path(phase.artifact),
                earlier.contents,
                phase.contents,
            )
            diff_name = f"output-{earlier.name}-to-{phase.name}.diff"
            if diff is not None and _use_diff(len(diff), len(phase.contents)):
                diff_path = sample / diff_name
                diff_path.unlink(missing_ok=True)
                diff_path.write_bytes(diff)
                representation = Representation(
                    result[-1].base,
                    (*result[-1].diffs, diff_name),
                )
        if representation is None:
            representation = Representation(canonical[phase.contents])
        by_contents[phase.contents] = representation
        result.append(representation)
    return result


def _reconstruct(sample: Path, storage: dict[str, Any]) -> directory_snapshot.Snapshot:
    entries: directory_snapshot.Snapshot = {}
    for name, stored in storage["files"].items():
        relative = Path(name)
        representation = Representation(stored["base"], tuple(stored["diffs"]))
        for source in (representation.base, *representation.diffs):
            path = Path(source)
            if not path.parts or path.is_absolute() or ".." in path.parts:
                raise ValueError(f"invalid storage reference: {source}")
            _read_regular(sample, path)
        if representation.diffs:
            result = subprocess.run(
                [str(_APPLY_DIFFS), representation.base, *representation.diffs],
                cwd=sample,
                check=True,
                stdout=subprocess.PIPE,
            )
            entries[relative] = result.stdout
        else:
            entries[relative] = _read_regular(sample, representation.base)
    return entries


def restore(sample: Path, phase: str, destination: Path) -> None:
    """Restore each file at its original relative path."""
    records = json.loads(_read_regular(sample, "checkpoints.json").decode())
    matching = [record for record in records if record["phase"] == phase]
    if len(matching) != 1:
        raise ValueError(f"expected one checkpoint phase: {phase}")
    entries = _reconstruct(sample, matching[0]["storage"])
    if destination.exists() or destination.is_symlink():
        raise FileExistsError(f"restore destination already exists: {destination}")
    directory_snapshot.write(entries, destination)


def _mapping(phases: list[Phase]) -> str:
    lines = [
        "## Checkpoints",
        "",
        "From the checkout's `fbcode/`, replace `SAMPLE_DIRECTORY` and each destination path.",
        "",
    ]
    for phase in phases:
        label = (
            "Initial draft"
            if phase.name == "initial"
            else (
                "Author review"
                if phase.name == "author"
                else f"Review {phase.name.removeprefix('review')}"
            )
        )
        lines.append(
            f"- **{label}:** `python3 -m folly.agents.tests.compress_checkpoint_outputs "
            f"SAMPLE_DIRECTORY --restore {phase.name} --destination /path/to/{phase.name}`"
        )
    return "\n".join(lines) + "\n"


def compress(sample: Path, mapping_output: TextIO) -> None:
    """Compress only after reconstruction and map output both succeed."""
    sample = sample.resolve()
    records, phases = _load(sample)
    stored = _representations(sample, phases)
    for record, phase, representation in zip(records, phases, stored):
        record["storage"] = {"files": {"main.md": asdict(representation)}}
        if _reconstruct(sample, record["storage"]) != {Path("main.md"): phase.contents}:
            raise ValueError(f"could not reconstruct phase {phase.name}")

    mapping_output.write(_mapping(phases))
    mapping_output.flush()

    retained = {
        "output/main.md",
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
        f"output-{source.name}-to-{target.name}.diff"
        for earlier, later in zip(phases, phases[1:])
        for source, target in ((earlier, later), (later, earlier))
    }
    for diff in known_diffs - retained:
        (sample / diff).unlink(missing_ok=True)


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sample", type=Path)
    parser.add_argument("--restore", metavar="PHASE")
    parser.add_argument("--destination", type=Path)
    args = parser.parse_args(argv)
    if (args.restore is None) != (args.destination is None):
        parser.error("--restore and --destination must be supplied together")
    if args.restore is None:
        compress(args.sample, sys.stdout)
    else:
        restore(args.sample, args.restore, args.destination)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
