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

# @noautodeps

"""Compare Folly benchmark performance across two revisions."""

CLI_DOC = """\
Quick start:

  folly/tool/benchmark_ab.py measure --before REV1 --after REV2 \\
    //folly/result/...

The `measure` command runs the Folly benchmark targets selected via Buck
patterns, and combines their results into one report. It highlights changes
that are large enough to investigate first.

Each round measures every selected benchmark first at the "before" revision,
then at the "after" revision.  Repeated rounds reduce the influence of isolated
run-to-run noise.  Each benchmark uses adaptive measurement to reduce
short-term noise.

The positional arguments are Buck target patterns.  At the "before" revision,
the tool expands each pattern to runnable C++ tests and binaries that
transitively depend on `//folly:benchmark`, excluding targets with the Buck
label `not_a_folly_benchmark`.  It runs that fixed target set at both revisions.
Benchmark targets absent from "before" are not measured.  Each round checks out
both requested `sl` revisions.  At the end, we restore the starting revision.
A failed target invocation, or one where any benchmark fails to converge, is
retried. Results contribute only after a successful, fully converged attempt.

The output directory stores the raw benchmark output plus human-readable
Markdown and sortable TSV reports. A concise comparison goes to stdout;
progress goes to stderr. Unusable runs and benchmarks present at only one
revision are reported separately rather than silently discarded.

Use `reanalyze` to rewrite reports from existing measurements without rerunning
benchmarks:

  folly/tool/benchmark_ab.py reanalyze --out OUT

Results:

  15.2+11.1ns (+73.0%): try_to_result_error
    //folly/result/test:result_bench
    15.2+11.2, 15.2+11.1, 15.3+11.0
"""

RESULT_DOC = """\
Each result line starts with the median "before" timing, with Δ ns to the
median "after" timing.  It also shows (Δ%) when "before" exceeds 2ns.

We aggregate rounds by taking the medians of the "before" and "after" timings.
Each round reports adaptive p{pct} timings, so these are medians-of-p{pct}.

A benchmark appears in the lo-pri or hi-pri section, when the Δ between those
medians meets both that section's nanosecond and percentage thresholds.

The comma-separated `before±Δ` pairs show whether the change is consistent
across rounds.  They are sorted by `before` timing, not by run order.
Parentheses mark a pair whose Δ missed a section threshold.

Within each section, rows are sorted by Δ between medians, smallest first.
"""


import argparse
import csv
import io
import json
import math
import re
import shutil
import statistics
import subprocess
import sys
import time
from collections.abc import Iterable, Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Protocol


### Data model

BEFORE_SIDE = "before"
AFTER_SIDE = "after"
PCT_MIN_BEFORE_NS = 2.0
# Set p33.3 explicitly so comparisons use the same percentile even when
# //folly:benchmark defaults differ between revisions.
BM_TARGET_PERCENTILE = 33.3
MANIFEST_FILENAME = "manifest.json"


@dataclass(frozen=True)
class BenchmarkTarget:
    build_target: str
    # Generated name used only for artifact directories and files.
    artifact_name: str


@dataclass(frozen=True, order=True)
class BenchmarkId:
    file: str
    name: str


@dataclass(frozen=True)
class MeasurementManifest:
    before: str
    after: str
    mode: str
    bm_max_secs: int
    bm_target_percentile: float
    target_patterns: tuple[str, ...]
    targets: tuple[BenchmarkTarget, ...]

    def __post_init__(self) -> None:
        if not self.before or not self.after:
            raise ValueError("before and after revisions must be nonempty")
        if not self.mode:
            raise ValueError("mode must be nonempty")
        if self.bm_max_secs < 1:
            raise ValueError("bm_max_secs must be at least 1")
        if not self.target_patterns:
            raise ValueError("target_patterns must be nonempty")
        if any(not pattern for pattern in self.target_patterns):
            raise ValueError("target patterns must be nonempty strings")
        if not self.targets:
            raise ValueError("targets must be nonempty")

        build_targets = [target.build_target for target in self.targets]
        artifact_names = [target.artifact_name for target in self.targets]
        if any(not build_target for build_target in build_targets):
            raise ValueError("build targets must be nonempty")
        if len(set(build_targets)) != len(build_targets):
            raise ValueError("build targets must be unique")
        if len(set(artifact_names)) != len(artifact_names):
            raise ValueError("target artifact names must be unique")
        for artifact_name in artifact_names:
            if (
                artifact_name in {"", ".", ".."}
                or Path(artifact_name).name != artifact_name
            ):
                raise ValueError(
                    "target artifact_name must be a single path component so "
                    "benchmark artifacts stay inside their target directory; "
                    f"got {artifact_name!r}"
                )


@dataclass(frozen=True)
class AttemptPaths:
    directory: Path
    json: Path
    log: Path
    completion: Path


@dataclass(frozen=True)
class AttemptArtifact:
    json_path: Path
    returncode: int | None
    convergence_failed: bool | None

    @property
    def completed(self) -> bool:
        return self.returncode is not None and self.convergence_failed is not None

    @property
    def usable(self) -> bool:
        return (
            self.returncode == 0
            and self.json_path.exists()
            and self.convergence_failed is False
        )


@dataclass(frozen=True)
class RunArtifact:
    round_number: int
    side: str
    target: BenchmarkTarget
    attempts: tuple[AttemptArtifact, ...]
    results: dict[BenchmarkId, float]

    @property
    def selected_attempt(self) -> AttemptArtifact | None:
        for attempt in self.attempts:
            if attempt.usable:
                return attempt
        return None


@dataclass(frozen=True)
class Observation:
    round_number: int
    before: float
    after: float

    @property
    def delta(self) -> float:
        return self.after - self.before

    @property
    def pct(self) -> float:
        return percentage_change(self.before, self.after)


def percentage_change(before: float, after: float) -> float:
    time_floor_ns = 0.001  # Absolute floor in BenchmarkAdaptive.cpp::epsilonNs
    before = max(before, time_floor_ns)
    after = max(after, time_floor_ns)
    return 100.0 * (after - before) / before


def display_round(value: float) -> float:
    rounded = round(value, 1)
    # Collapse -0.0 so tiny improvements do not print as negative zero.
    return 0.0 if rounded == 0 else rounded


@dataclass(frozen=True)
class ComparisonSummary:
    before: float
    after: float

    @property
    def delta(self) -> float:
        return self.after - self.before

    @property
    def pct(self) -> float:
        return percentage_change(self.before, self.after)


@dataclass(frozen=True)
class Threshold:
    ns: float
    pct: float

    def met_by(
        self,
        change: Observation | ComparisonSummary,
        *,
        direction: int,
    ) -> bool:
        # Compare displayed values so a row cannot appear to miss the threshold
        # of the section containing it.
        return display_round(direction * change.delta) >= display_round(
            self.ns
        ) and display_round(direction * change.pct) >= display_round(self.pct)


@dataclass(frozen=True)
class NeedsAttention:
    round_number: int
    side: str
    target: BenchmarkTarget
    reason: str
    log_dir: Path


@dataclass(frozen=True)
class ComparisonRow:
    build_target: str
    benchmark: BenchmarkId
    observations: tuple[Observation, ...]


@dataclass(frozen=True)
class ReportSection:
    title: str
    classification: str
    direction: int
    threshold: Threshold
    rows: tuple[ComparisonRow, ...]


@dataclass(frozen=True)
class ComparisonReport:
    needs_attention: tuple[NeedsAttention, ...]
    unpaired_rows: dict[tuple[str, BenchmarkId, str], list[int]]
    benchmark_names_with_multiple_files: frozenset[tuple[str, str]]
    sections: tuple[ReportSection, ...]


# Each caller's type exposes only the Buck and checkout operations it needs.
class BuckQuery(Protocol):
    def query_buck(self, query: str) -> list[str]: ...


class BuckRunner(Protocol):
    def run_buck(
        self,
        arguments: Sequence[str],
        *,
        log_path: Path,
    ) -> int: ...


class Workspace(BuckQuery, BuckRunner, Protocol):
    def has_changes(self, *, include_untracked: bool) -> bool: ...

    def resolve_revision(self, revision: str) -> str: ...

    def checkout(self, revision: str) -> None: ...


### CLI and benchmark discovery


# Preserve docstring layout while showing only useful argparse defaults.
class HelpFormatter(argparse.RawDescriptionHelpFormatter):
    def _get_help_string(self, action: argparse.Action) -> str:
        help_text = action.help or ""
        if (
            action.default is not None
            and action.default is not False
            and action.default is not argparse.SUPPRESS
            and "%(default)" not in help_text
        ):
            help_text += " (default: %(default)s)"
        return help_text


def add_report_arguments(parser: argparse.ArgumentParser) -> None:
    def nonnegative_float(text: str) -> float:
        value = float(text)
        if not math.isfinite(value) or value < 0:
            raise argparse.ArgumentTypeError("must be finite and nonnegative")
        return value

    parser.add_argument(
        "--hi-ns",
        metavar="NS",
        type=nonnegative_float,
        default=1.0,
        help="absolute nanosecond threshold for high-priority sections",
    )
    parser.add_argument(
        "--hi-pct",
        metavar="PCT",
        type=nonnegative_float,
        default=10.0,
        help="percentage threshold for high-priority sections",
    )
    parser.add_argument(
        "--lo-ns",
        metavar="NS",
        type=nonnegative_float,
        default=0.5,
        help="absolute nanosecond threshold for low-priority sections",
    )
    parser.add_argument(
        "--lo-pct",
        metavar="PCT",
        type=nonnegative_float,
        default=5.0,
        help="percentage threshold for low-priority sections",
    )


def add_measure_parser(
    commands: "argparse._SubParsersAction[argparse.ArgumentParser]",
) -> None:
    measure = commands.add_parser(
        "measure",
        formatter_class=HelpFormatter,
        help="measure benchmarks and write a comparison",
    )
    measure.add_argument(
        "--before",
        metavar="REV",
        required=True,
        help='revision used for "before" runs',
    )
    measure.add_argument(
        "--after",
        metavar="REV",
        required=True,
        help='revision used for "after" runs',
    )
    measure.add_argument(
        "--mode",
        default="@mode/opt",
        help="Buck build mode passed before each benchmark target",
    )
    measure.add_argument(
        "--rounds",
        metavar="N",
        type=int,
        default=5,
        help="number of before/after rounds; nonpositive values only discover targets",
    )
    measure.add_argument(
        "--bm-max-secs",
        metavar="SECONDS",
        type=int,
        default=30,
        help="adaptive measurement time limit per benchmark",
    )
    measure.add_argument(
        "--max-run-attempts",
        metavar="N",
        type=int,
        default=3,
        help=(
            "maximum attempts for each benchmark run when the command fails "
            "or adaptive mode does not converge"
        ),
    )
    measure.add_argument(
        "--allow-untracked",
        action="store_true",
        help="allow untracked files; tracked working-copy changes are rejected",
    )
    measure.add_argument(
        "--out",
        metavar="DIR",
        type=Path,
        help="artifact directory to write; defaults to benchmark_ab_<timestamp>/",
    )
    measure.add_argument(
        "target_patterns",
        nargs="+",
        metavar="TARGET_PATTERN",
        help=(
            "Buck selector to search for benchmark targets, for example "
            "//folly/result/..."
        ),
    )
    add_report_arguments(measure)


def add_reanalyze_parser(
    commands: "argparse._SubParsersAction[argparse.ArgumentParser]",
) -> None:
    reanalyze = commands.add_parser(
        "reanalyze",
        formatter_class=HelpFormatter,
        help="reanalyze stored measurements and rewrite the comparison",
    )
    reanalyze.add_argument(
        "--out",
        metavar="DIR",
        type=Path,
        required=True,
        help="existing artifact directory to reanalyze",
    )
    add_report_arguments(reanalyze)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        formatter_class=HelpFormatter,
        description="\n\n".join(
            (
                __doc__ or "",
                CLI_DOC.rstrip(),
                RESULT_DOC.format(
                    pct=fmt_value(BM_TARGET_PERCENTILE),
                ).rstrip(),
            )
        ),
    )
    commands = parser.add_subparsers(dest="command", required=True)
    add_measure_parser(commands)
    add_reanalyze_parser(commands)
    return parser.parse_args(argv)


def validate_args(args: argparse.Namespace) -> None:
    if args.command == "measure":
        if args.rounds <= 0:
            return
        if args.max_run_attempts < 1:
            raise SystemExit("--max-run-attempts must be at least 1")
        if args.bm_max_secs < 1:
            raise SystemExit("--bm-max-secs must be at least 1")
    if args.lo_ns > args.hi_ns:
        raise SystemExit("--lo-ns must not exceed --hi-ns")
    if args.lo_pct > args.hi_pct:
        raise SystemExit("--lo-pct must not exceed --hi-pct")


def benchmark_query(pattern: str) -> str:
    if "//" not in pattern or re.fullmatch(r"[A-Za-z0-9_./:+-]+", pattern) is None:
        raise SystemExit(f"target pattern must be a Buck selector; got {pattern!r}")
    # TODO: Add `--folly-benchmark-target` if `//folly:benchmark` does not
    # resolve from the caller's Buck cell.
    return (
        "nattrfilter(labels, 'not_a_folly_benchmark', "
        f"kind('^cxx_(test|binary)$', rdeps({pattern}, //folly:benchmark)))"
    )


# Extracted as a helper for tests.
def make_benchmark_targets(
    build_targets: Iterable[str],
) -> tuple[BenchmarkTarget, ...]:
    targets = []
    for index, build_target in enumerate(sorted(build_targets), start=1):
        prefix = f"{index:03d}_"
        slug = re.sub(r"[^A-Za-z0-9_.-]+", "_", build_target).strip("_") or "target"
        # Leave room for side prefixes and file suffixes within NAME_MAX.
        max_slug_length = 200 - len(prefix)
        if len(slug) > max_slug_length:
            retained_length = max_slug_length - 3
            leading_length = (retained_length + 1) // 2
            trailing_length = retained_length - leading_length
            slug = f"{slug[:leading_length]}...{slug[-trailing_length:]}"
        targets.append(
            BenchmarkTarget(
                build_target=build_target,
                artifact_name=f"{prefix}{slug}",
            )
        )
    return tuple(targets)


def benchmark_targets(
    args: argparse.Namespace,
    buck: BuckQuery,
) -> tuple[BenchmarkTarget, ...]:
    build_targets: set[str] = set()
    for pattern in args.target_patterns:
        build_targets.update(buck.query_buck(benchmark_query(pattern)))
    if not build_targets:
        raise SystemExit(
            f"no benchmark targets found under: {', '.join(args.target_patterns)}"
        )

    targets = make_benchmark_targets(build_targets)
    displayed_build_targets = [target.build_target for target in targets]
    if len(displayed_build_targets) > 10:
        displayed_build_targets = [
            *displayed_build_targets[:5],
            "...",
            *displayed_build_targets[-5:],
        ]
    print(
        progress_line(f"found {len(targets)} benchmark targets:"),
        file=sys.stderr,
    )
    for build_target in displayed_build_targets:
        print(f"  {build_target}", file=sys.stderr)
    sys.stderr.flush()
    return targets


### Measurement manifest


def write_manifest(out_dir: Path, manifest: MeasurementManifest) -> None:
    data = {
        "before": manifest.before,
        "after": manifest.after,
        "mode": manifest.mode,
        "bm_max_secs": manifest.bm_max_secs,
        "bm_target_percentile": manifest.bm_target_percentile,
        "target_patterns": list(manifest.target_patterns),
        "targets": [
            {
                "build_target": target.build_target,
                "artifact_name": target.artifact_name,
            }
            for target in manifest.targets
        ],
    }
    (out_dir / MANIFEST_FILENAME).write_text(
        json.dumps(data, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def parse_manifest_targets(data: object) -> tuple[BenchmarkTarget, ...]:
    if not isinstance(data, list):
        raise ValueError("targets must be a list")
    targets: list[BenchmarkTarget] = []
    for raw_target in data:
        if not isinstance(raw_target, dict):
            raise ValueError("each target must be an object")
        build_target = raw_target.get("build_target")
        artifact_name = raw_target.get("artifact_name")
        if not isinstance(build_target, str) or not isinstance(artifact_name, str):
            raise ValueError("build_target and artifact_name must be strings")
        targets.append(
            BenchmarkTarget(
                build_target=build_target,
                artifact_name=artifact_name,
            )
        )
    return tuple(targets)


def load_manifest(out_dir: Path) -> MeasurementManifest:
    path = out_dir / MANIFEST_FILENAME
    if not path.is_file():
        raise SystemExit(f"measurement manifest does not exist: {path}")
    try:
        data: object = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(data, dict):
            raise ValueError("top level must be an object")

        before = data.get("before")
        after = data.get("after")
        mode = data.get("mode")
        bm_max_secs = data.get("bm_max_secs")
        bm_target_percentile = data.get("bm_target_percentile")
        target_patterns = data.get("target_patterns")
        if not isinstance(before, str) or not isinstance(after, str):
            raise ValueError("before and after revisions must be strings")
        if not isinstance(mode, str) or type(bm_max_secs) is not int:
            raise ValueError("mode must be a string and bm_max_secs must be an integer")
        if type(bm_target_percentile) is int:
            bm_target_percentile = float(bm_target_percentile)
        elif type(bm_target_percentile) is not float:
            raise ValueError("bm_target_percentile must be a number")
        if not isinstance(target_patterns, list) or not all(
            isinstance(pattern, str) for pattern in target_patterns
        ):
            raise ValueError("target_patterns must be a list of strings")
        return MeasurementManifest(
            before=before,
            after=after,
            mode=mode,
            bm_max_secs=bm_max_secs,
            bm_target_percentile=bm_target_percentile,
            target_patterns=tuple(target_patterns),
            targets=parse_manifest_targets(data.get("targets")),
        )
    except (TypeError, ValueError) as error:
        raise SystemExit(f"invalid measurement manifest {path}: {error}") from error


### Benchmark execution and artifact writing


@dataclass(frozen=True)
class SaplingWorkspace:
    root: Path
    buck_executable: Path

    @classmethod
    def discover(cls, buck_executable: Path, *, directory: Path) -> "SaplingWorkspace":
        root = Path(
            subprocess.check_output(
                [
                    str(buck_executable),
                    "root",
                    "--kind",
                    "cell",
                    "--dir",
                    str(directory),
                ],
                text=True,
            ).strip()
        ).resolve()
        return cls(root=root, buck_executable=buck_executable)

    def query_buck(self, query: str) -> list[str]:
        command = [str(self.buck_executable), "uquery", query]
        print(
            f"{progress_line('discovering benchmarks via:')}\n"
            f'  {command[0]} {command[1]} "{command[2]}"',
            file=sys.stderr,
            flush=True,
        )
        # Keep Buck diagnostics out of successful target output, but surface
        # them when the query fails.
        try:
            text = subprocess.check_output(
                command,
                cwd=self.root,
                stderr=subprocess.PIPE,
                text=True,
            )
        except subprocess.CalledProcessError as error:
            raise SystemExit(error.stderr or error.stdout) from error
        return text.splitlines()

    def run_buck(
        self,
        arguments: Sequence[str],
        *,
        log_path: Path,
    ) -> int:
        log_path.parent.mkdir(parents=True, exist_ok=True)
        with log_path.open("w", encoding="utf-8") as out:
            return subprocess.run(
                [str(self.buck_executable), *arguments],
                cwd=self.root,
                stdout=out,
                stderr=subprocess.STDOUT,
                text=True,
            ).returncode

    def has_changes(self, *, include_untracked: bool) -> bool:
        return bool(
            subprocess.check_output(
                ["sl", "status", "-mardu" if include_untracked else "-mard"],
                cwd=self.root,
                text=True,
            )
        )

    def resolve_revision(self, revision: str) -> str:
        return subprocess.check_output(
            ["sl", "log", "-r", revision, "-T", "{node}"],
            cwd=self.root,
            text=True,
        ).strip()

    def checkout(self, revision: str) -> None:
        result = subprocess.run(
            ["sl", "goto", revision],
            cwd=self.root,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        if result.returncode != 0:
            sys.stderr.write(result.stdout)
            raise RuntimeError(f"`sl goto {revision}` failed")


def duration_text(seconds: float) -> str:
    rounded = max(0, int(round(seconds)))
    minutes, seconds = divmod(rounded, 60)
    hours, minutes = divmod(minutes, 60)
    if hours:
        return f"{hours}:{minutes:02d}:{seconds:02d}"
    return f"{minutes}:{seconds:02d}"


def progress_line(message: str, *, remaining: str | None = None) -> str:
    timestamp = time.strftime("%Y-%m-%d %H:%M:%S")
    if remaining is None:
        return f"[{timestamp}] {message}"
    return f"[{timestamp}, {remaining} remaining] {message}"


def remaining_text(
    *,
    started_at: float,
    completed_rounds: int,
    total_rounds: int,
) -> str:
    elapsed = time.monotonic() - started_at
    return duration_text(elapsed * (total_rounds - completed_rounds) / completed_rounds)


def run_dir(
    out_dir: Path,
    round_number: int,
    side: str,
    target: BenchmarkTarget,
) -> Path:
    return out_dir / f"round_{round_number}" / f"{side}_{target.artifact_name}"


def attempt_paths(
    base_dir: Path,
    target: BenchmarkTarget,
    attempt: int,
) -> AttemptPaths:
    directory = base_dir / f"attempt_{attempt}"
    return AttemptPaths(
        directory=directory,
        json=directory / f"{target.artifact_name}.json",
        log=directory / f"{target.artifact_name}.log",
        completion=directory / "completion.json",
    )


def benchmark_arguments(
    args: argparse.Namespace,
    *,
    target: BenchmarkTarget,
    json_path: Path,
) -> list[str]:
    return [
        "run",
        args.mode,
        target.build_target,
        "--",
        "--benchmark",
        "--bm_mode=adaptive",
        f"--bm_target_percentile={BM_TARGET_PERCENTILE}",
        f"--bm_max_secs={args.bm_max_secs}",
        f"--bm_json_verbose={json_path}",
        "--bm_verbose",
    ]


def load_attempt_artifact(paths: AttemptPaths) -> AttemptArtifact:
    # completion.json is atomically renamed into place after the other attempt
    # files are written.
    if not paths.completion.exists():
        return AttemptArtifact(
            json_path=paths.json,
            returncode=None,
            convergence_failed=None,
        )
    completion: object = json.loads(paths.completion.read_text(encoding="utf-8"))
    if type(completion) is not dict:
        raise ValueError(f"invalid attempt completion file: {paths.completion}")
    returncode = completion.get("returncode")
    convergence_failed = completion.get("convergence_failed")
    if type(returncode) is not int or type(convergence_failed) is not bool:
        raise ValueError(f"invalid attempt completion file: {paths.completion}")
    return AttemptArtifact(
        json_path=paths.json,
        returncode=returncode,
        convergence_failed=convergence_failed,
    )


def write_attempt_completion(
    completion_path: Path,
    *,
    returncode: int,
    convergence_failed: bool,
) -> None:
    temporary_path = completion_path.with_suffix(".tmp")
    temporary_path.write_text(
        json.dumps(
            {
                "returncode": returncode,
                "convergence_failed": convergence_failed,
            },
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )
    temporary_path.replace(completion_path)  # load_attempt_artifact() relies on this.


def make_run_artifact(
    *,
    round_number: int,
    side: str,
    target: BenchmarkTarget,
    attempts: tuple[AttemptArtifact, ...],
) -> RunArtifact:
    selected_attempt = next(
        (attempt for attempt in attempts if attempt.usable),
        None,
    )
    return RunArtifact(
        round_number=round_number,
        side=side,
        target=target,
        attempts=attempts,
        results=load_results(selected_attempt.json_path) if selected_attempt else {},
    )


def attempt_problem_summary(attempt: AttemptArtifact) -> str:
    if not attempt.completed:
        return "did not finish"
    if attempt.returncode != 0:
        return f"failed with exit {attempt.returncode}"
    if not attempt.json_path.exists():
        return "did not write JSON"
    if attempt.convergence_failed:
        return "did not converge"
    return "was not usable"


def run_one_benchmark(
    args: argparse.Namespace,
    *,
    round_number: int,
    round_count: int,
    side: str,
    target: BenchmarkTarget,
    buck: BuckRunner,
) -> RunArtifact:
    base_dir = run_dir(args.out, round_number, side, target)
    attempts: list[AttemptArtifact] = []
    max_attempts = args.max_run_attempts
    print(
        progress_line(f"{round_number}/{round_count} {target.build_target} ({side})"),
        file=sys.stderr,
        flush=True,
    )
    for attempt in range(1, max_attempts + 1):
        paths = attempt_paths(base_dir, target, attempt)
        returncode = buck.run_buck(
            benchmark_arguments(
                args,
                target=target,
                json_path=paths.json,
            ),
            log_path=paths.log,
        )
        convergence_failed = log_has_convergence_failure(paths.log)
        artifact = AttemptArtifact(
            json_path=paths.json,
            returncode=returncode,
            convergence_failed=convergence_failed,
        )
        write_attempt_completion(
            paths.completion,
            returncode=returncode,
            convergence_failed=convergence_failed,
        )
        attempts.append(artifact)
        if artifact.usable:
            break
        retry = "; retrying" if attempt < max_attempts else ""
        print(
            f"  attempt {attempt}/{max_attempts} "
            f"{attempt_problem_summary(artifact)}{retry}",
            file=sys.stderr,
            flush=True,
        )
    return make_run_artifact(
        round_number=round_number,
        side=side,
        target=target,
        attempts=tuple(attempts),
    )


def check_working_copy(args: argparse.Namespace, workspace: Workspace) -> None:
    if workspace.has_changes(include_untracked=not args.allow_untracked):
        if args.allow_untracked:
            raise SystemExit(
                "working copy has tracked changes; commit or revert them before "
                "measuring"
            )
        raise SystemExit(
            "working copy has changes; commit tracked changes or pass --allow-untracked"
        )


def run_rounds(
    args: argparse.Namespace,
    manifest: MeasurementManifest,
    workspace: Workspace,
) -> None:
    rounds = args.rounds
    started_at = time.monotonic()
    for round_number in range(1, rounds + 1):
        for side, revision in (
            (BEFORE_SIDE, manifest.before),
            (AFTER_SIDE, manifest.after),
        ):
            workspace.checkout(revision)
            for target in manifest.targets:
                run_one_benchmark(
                    args,
                    round_number=round_number,
                    round_count=rounds,
                    side=side,
                    target=target,
                    buck=workspace,
                )
        print(
            progress_line(
                f"round {round_number}/{rounds} finished",
                remaining=remaining_text(
                    started_at=started_at,
                    completed_rounds=round_number,
                    total_rounds=rounds,
                ),
            ),
            file=sys.stderr,
            flush=True,
        )


### Stored artifact loading


def log_has_convergence_failure(log_path: Path) -> bool:
    # TODO: Extend //folly:benchmark's JSON contract to identify which rows
    # converged, so benchmark_ab can retain converged rows from a partial run.
    if not log_path.exists():
        return True
    return "Did not converge:" in log_path.read_text(encoding="utf-8", errors="replace")


def load_results(json_path: Path) -> dict[BenchmarkId, float]:
    if not json_path.exists():
        return {}
    data: object = json.loads(json_path.read_text(encoding="utf-8"))
    if not isinstance(data, list):
        raise ValueError(
            f"invalid Folly benchmark JSON; expected a top-level array: {json_path}"
        )

    # Coupled to the first three fields of
    # Benchmark.cpp::benchmarkResultsToDynamic().
    # TODO: Validate UserCounters if benchmark_ab starts consuming them.
    results: dict[BenchmarkId, float] = {}
    for row in data:
        if isinstance(row, list) and len(row) in {3, 4}:
            source_file, benchmark_name, time_ns = row[:3]
            if (
                type(source_file) is str
                and type(benchmark_name) is str
                and type(time_ns) in (int, float)
            ):
                results[BenchmarkId(file=source_file, name=benchmark_name)] = float(
                    time_ns
                )
                continue
        raise ValueError(
            "invalid Folly benchmark JSON row; expected "
            "[file: str, name: str, time_ns: number, optional counters]; "
            f"got {row!r} in {json_path}"
        )
    return results


def discover_numbered_directories(parent: Path, prefix: str) -> list[int]:
    if not parent.exists():
        return []
    numbers: list[int] = []
    for child in parent.iterdir():
        if not child.is_dir() or not child.name.startswith(prefix):
            continue
        suffix = child.name.removeprefix(prefix)
        if suffix.isdigit():
            numbers.append(int(suffix))
    return sorted(numbers)


def load_run_artifact(
    *,
    round_number: int,
    side: str,
    target: BenchmarkTarget,
    base_dir: Path,
) -> RunArtifact:
    return make_run_artifact(
        round_number=round_number,
        side=side,
        target=target,
        attempts=tuple(
            load_attempt_artifact(attempt_paths(base_dir, target, attempt))
            for attempt in discover_numbered_directories(base_dir, "attempt_")
        ),
    )


def load_artifacts(
    out_dir: Path,
    targets: tuple[BenchmarkTarget, ...],
    round_numbers: Iterable[int],
) -> dict[tuple[int, str, str], RunArtifact]:
    artifacts: dict[tuple[int, str, str], RunArtifact] = {}
    for round_number in round_numbers:
        for side in (BEFORE_SIDE, AFTER_SIDE):
            for target in targets:
                artifacts[(round_number, side, target.build_target)] = (
                    load_run_artifact(
                        round_number=round_number,
                        side=side,
                        target=target,
                        base_dir=run_dir(out_dir, round_number, side, target),
                    )
                )
    return artifacts


### Comparison analysis


def try_count_text(count: int) -> str:
    return "1 try" if count == 1 else f"{count} tries"


def needs_attention_for_run(
    artifact: RunArtifact, *, out_dir: Path
) -> NeedsAttention | None:
    if artifact.selected_attempt is not None:
        if artifact.results:
            return None
        reason = "Benchmark run produced no results"
    else:
        attempts = artifact.attempts
        count = len(attempts)
        if not attempts:
            reason = "No attempts found for this benchmark run"
        elif any(not attempt.completed for attempt in attempts):
            reason = f"Benchmark run did not finish after {try_count_text(count)}"
        elif any(attempt.returncode != 0 for attempt in attempts):
            reason = f"Benchmark command failed after {try_count_text(count)}"
        elif any(not attempt.json_path.exists() for attempt in attempts):
            reason = f"Benchmark run wrote no JSON after {try_count_text(count)}"
        else:
            reason = f"Benchmark run did not converge after {try_count_text(count)}"
    return NeedsAttention(
        round_number=artifact.round_number,
        side=artifact.side,
        target=artifact.target,
        reason=reason,
        log_dir=run_dir(
            out_dir,
            artifact.round_number,
            artifact.side,
            artifact.target,
        ),
    )


def paired_observations(
    artifacts: dict[tuple[int, str, str], RunArtifact],
    out_dir: Path,
    targets: tuple[BenchmarkTarget, ...],
) -> tuple[
    dict[tuple[str, BenchmarkId], list[Observation]],
    list[NeedsAttention],
    dict[tuple[str, BenchmarkId, str], list[int]],
]:
    rows: dict[tuple[str, BenchmarkId], list[Observation]] = {}
    needs_attention_runs: list[NeedsAttention] = []
    unpaired_rows: dict[tuple[str, BenchmarkId, str], list[int]] = {}
    for round_number in sorted({key[0] for key in artifacts}):
        for target in targets:
            before = artifacts[(round_number, BEFORE_SIDE, target.build_target)]
            after = artifacts[(round_number, AFTER_SIDE, target.build_target)]
            before_attention = needs_attention_for_run(before, out_dir=out_dir)
            after_attention = needs_attention_for_run(after, out_dir=out_dir)
            if before_attention is not None:
                needs_attention_runs.append(before_attention)
            if after_attention is not None:
                needs_attention_runs.append(after_attention)
            if before_attention is not None or after_attention is not None:
                continue
            before_benchmarks = set(before.results)
            after_benchmarks = set(after.results)
            for benchmark in sorted(before_benchmarks - after_benchmarks):
                unpaired_rows.setdefault(
                    (target.build_target, benchmark, BEFORE_SIDE), []
                ).append(round_number)
            for benchmark in sorted(after_benchmarks - before_benchmarks):
                unpaired_rows.setdefault(
                    (target.build_target, benchmark, AFTER_SIDE), []
                ).append(round_number)
            for benchmark in sorted(before_benchmarks & after_benchmarks):
                rows.setdefault((target.build_target, benchmark), []).append(
                    Observation(
                        round_number=round_number,
                        before=before.results[benchmark],
                        after=after.results[benchmark],
                    )
                )
    return rows, needs_attention_runs, unpaired_rows


# Classification compares median(after) with median(before). Per-round deltas
# are only a quick visual check of run-to-run consistency.
def comparison_summary(
    observations: tuple[Observation, ...],
) -> ComparisonSummary:
    return ComparisonSummary(
        before=statistics.median(obs.before for obs in observations),
        after=statistics.median(obs.after for obs in observations),
    )


def bucket_rows(
    rows: dict[tuple[str, BenchmarkId], list[Observation]],
    *,
    direction: int,
    threshold: Threshold,
    exclude_threshold: Threshold | None = None,
) -> tuple[ComparisonRow, ...]:
    selected: list[ComparisonRow] = []
    for (build_target, benchmark), observations in rows.items():
        summary = comparison_summary(tuple(observations))
        if display_round(direction * summary.delta) <= 0:
            continue
        if exclude_threshold is not None and exclude_threshold.met_by(
            summary, direction=direction
        ):
            continue
        if threshold.met_by(summary, direction=direction):
            selected.append(
                ComparisonRow(
                    build_target=build_target,
                    benchmark=benchmark,
                    observations=tuple(observations),
                )
            )

    return tuple(
        sorted(
            selected,
            key=lambda row: (
                comparison_summary(row.observations).delta,
                row.build_target,
                row.benchmark,
            ),
        )
    )


def report_section(
    rows: dict[tuple[str, BenchmarkId], list[Observation]],
    *,
    title: str,
    classification: str,
    direction: int,
    threshold: Threshold,
    exclude_threshold: Threshold | None = None,
) -> ReportSection:
    return ReportSection(
        title=title,
        classification=classification,
        direction=direction,
        threshold=threshold,
        rows=bucket_rows(
            rows,
            direction=direction,
            threshold=threshold,
            exclude_threshold=exclude_threshold,
        ),
    )


def analyze_report(
    artifacts: dict[tuple[int, str, str], RunArtifact],
    args: argparse.Namespace,
    targets: tuple[BenchmarkTarget, ...],
) -> ComparisonReport:
    rows, needs_attention_runs, unpaired_rows = paired_observations(
        artifacts, args.out, targets
    )
    target_and_name_to_files: dict[tuple[str, str], set[str]] = {}
    for artifact in artifacts.values():
        for benchmark in artifact.results:
            target_and_name_to_files.setdefault(
                (artifact.target.build_target, benchmark.name), set()
            ).add(benchmark.file)
    hi_threshold = Threshold(
        ns=args.hi_ns,
        pct=args.hi_pct,
    )
    lo_threshold = Threshold(
        ns=args.lo_ns,
        pct=args.lo_pct,
    )
    sections = (
        report_section(
            rows,
            title="High-priority wins",
            classification="win-hi-pri",
            direction=-1,
            threshold=hi_threshold,
        ),
        report_section(
            rows,
            title="Low-priority wins",
            classification="win-lo-pri",
            direction=-1,
            threshold=lo_threshold,
            exclude_threshold=hi_threshold,
        ),
        report_section(
            rows,
            title="Low-priority regressions",
            classification="loss-lo-pri",
            direction=1,
            threshold=lo_threshold,
            exclude_threshold=hi_threshold,
        ),
        report_section(
            rows,
            title="High-priority regressions",
            classification="loss-hi-pri",
            direction=1,
            threshold=hi_threshold,
        ),
    )
    return ComparisonReport(
        needs_attention=tuple(needs_attention_runs),
        unpaired_rows=unpaired_rows,
        benchmark_names_with_multiple_files=frozenset(
            target_and_name
            for target_and_name, files in target_and_name_to_files.items()
            if len(files) > 1
        ),
        sections=sections,
    )


### Report rendering


def fmt_value(value: float) -> str:
    return f"{display_round(value):.1f}"


def fmt_signed_value(value: float) -> str:
    return f"{display_round(value):+.1f}"


def threshold_rounding_warning(
    args: argparse.Namespace,
    *,
    markdown: bool,
) -> list[str]:
    if all(
        value == display_round(value)
        for value in (args.hi_ns, args.hi_pct, args.lo_ns, args.lo_pct)
    ):
        return []
    return [
        ("**Note:** " if markdown else "Note: ")
        + "Thresholds were rounded to one decimal place for classification.",
        "",
    ]


def summary_text(summary: ComparisonSummary, pct_min_before_ns: float) -> str:
    # TODO: Scale slower rows to us or s, including their per-run values.
    percentage = ""
    if summary.before > pct_min_before_ns:
        percentage = f" ({fmt_signed_value(summary.pct)}%)"
    return f"{fmt_value(summary.before)}{fmt_signed_value(summary.delta)}ns{percentage}"


def threshold_pairs(
    row: ComparisonRow,
    *,
    section: ReportSection,
    markdown: bool,
) -> list[str]:
    pairs = []
    for observation in sorted(
        row.observations,
        key=lambda observation: (observation.before, observation.round_number),
    ):
        text = f"{fmt_value(observation.before)}{fmt_signed_value(observation.delta)}"
        if not section.threshold.met_by(observation, direction=section.direction):
            text = f"*({text})*" if markdown else f"({text})"
        pairs.append(text)
    return pairs


def benchmark_text(
    report: ComparisonReport,
    build_target: str,
    benchmark: BenchmarkId,
) -> str:
    if (
        build_target,
        benchmark.name,
    ) in report.benchmark_names_with_multiple_files:
        return f"{benchmark.name} ({benchmark.file})"
    return benchmark.name


def section_table(report: ComparisonReport, section: ReportSection) -> list[str]:
    def markdown_row(cells: Iterable[str]) -> str:
        return "| " + " | ".join(cells) + " |"

    if not section.rows:
        return []
    lines = [f"## {section.title}", ""]
    lines.extend(
        [
            markdown_row(("median", "benchmark", "target", "before ± Δ")),
            markdown_row(("---:", "---", "---", "---:")),
        ]
    )
    for row in section.rows:
        lines.append(
            markdown_row(
                [
                    summary_text(
                        comparison_summary(row.observations),
                        PCT_MIN_BEFORE_NS,
                    ),
                    benchmark_text(report, row.build_target, row.benchmark),
                    row.build_target,
                    ", ".join(threshold_pairs(row, section=section, markdown=True)),
                ]
            )
        )
    lines.append("")
    return lines


def rounds_text(round_numbers: list[int] | tuple[int, ...]) -> str:
    if not round_numbers:
        return "none"
    if list(round_numbers) == list(range(round_numbers[0], round_numbers[-1] + 1)):
        return (
            str(round_numbers[0])
            if len(round_numbers) == 1
            else f"{round_numbers[0]}-{round_numbers[-1]}"
        )
    return ", ".join(str(round_number) for round_number in round_numbers)


def grouped_unpaired_rows(
    unpaired_rows: dict[tuple[str, BenchmarkId, str], list[int]],
    side: str,
) -> list[tuple[str, tuple[int, ...], list[BenchmarkId]]]:
    grouped: dict[tuple[str, tuple[int, ...]], list[BenchmarkId]] = {}
    for (target, benchmark, row_side), round_numbers in unpaired_rows.items():
        if row_side == side:
            grouped.setdefault((target, tuple(round_numbers)), []).append(benchmark)
    return [
        (target, round_numbers, sorted(benchmarks))
        for (target, round_numbers), benchmarks in sorted(grouped.items())
    ]


def grouped_needs_attention(
    report: ComparisonReport,
) -> list[tuple[str, list[NeedsAttention]]]:
    reason_to_items: dict[str, list[NeedsAttention]] = {}
    for item in report.needs_attention:
        reason_to_items.setdefault(item.reason, []).append(item)
    return list(reason_to_items.items())


def needs_attention_markdown(report: ComparisonReport, *, out_dir: Path) -> list[str]:
    lines: list[str] = []
    for reason, items in grouped_needs_attention(report):
        lines.extend([f"## Needs attention: {reason}", ""])
        for item in items:
            log_dir = item.log_dir.relative_to(out_dir)
            lines.append(
                f"- Round {item.round_number}, `{item.target.build_target}` "
                f"({item.side}); [see logs]({log_dir})."
            )
        lines.append("")
    return lines


def unpaired_markdown(report: ComparisonReport) -> list[str]:
    lines: list[str] = []
    for side in (AFTER_SIDE, BEFORE_SIDE):
        groups = grouped_unpaired_rows(report.unpaired_rows, side)
        if not groups:
            continue
        lines.extend([f'## Benchmarks present only in "{side}" runs', ""])
        for target, round_numbers, benchmarks in groups:
            names = ", ".join(
                f"`{benchmark_text(report, target, benchmark)}`"
                for benchmark in benchmarks
            )
            lines.append(f"- `{target}` (rounds {rounds_text(round_numbers)}): {names}")
        lines.append("")
    return lines


def render_markdown(
    report: ComparisonReport,
    args: argparse.Namespace,
    manifest: MeasurementManifest,
) -> str:
    targets = manifest.targets
    lines = [
        f"**Output:** `{args.out}`",
        "",
        (
            f"**Target patterns:** {', '.join(manifest.target_patterns)} "
            f"({len(targets)} target{'s' if len(targets) != 1 else ''})"
        ),
        "",
        f"**Before:** `{manifest.before}`",
        "",
        f"**After:** `{manifest.after}`",
        "",
        f"**Targets:** {', '.join(f'`{target.build_target}`' for target in targets)}",
        "",
        (
            f"**Options:** `{manifest.mode}`; adaptive "
            f"p{fmt_value(manifest.bm_target_percentile)} "
            f"({manifest.bm_max_secs}s max/benchmark); "
            "thresholds for the median difference:"
        ),
        "",
        f"- hi-pri `>={fmt_value(args.hi_ns)}ns` and `>={fmt_value(args.hi_pct)}%`",
        f"- lo-pri `>={fmt_value(args.lo_ns)}ns` and `>={fmt_value(args.lo_pct)}%`",
        "",
        *threshold_rounding_warning(args, markdown=True),
    ]
    lines.extend(needs_attention_markdown(report, out_dir=args.out))
    lines.extend(unpaired_markdown(report))
    lines.extend(
        [
            "# Results",
            "",
            *RESULT_DOC.format(
                pct=fmt_value(manifest.bm_target_percentile),
            ).splitlines(),
            "",
        ]
    )
    for section in report.sections:
        lines.extend(section_table(report, section))
    return "\n".join(lines)


def needs_attention_terminal(report: ComparisonReport) -> list[str]:
    lines: list[str] = []
    for reason, items in grouped_needs_attention(report):
        lines.append(f"Needs attention: {reason}")
        lines.extend(
            f"  round {item.round_number}, {item.target.build_target} ({item.side}); see logs."
            for item in items
        )
        lines.append("")
    return lines


def unpaired_terminal(report: ComparisonReport) -> list[str]:
    lines: list[str] = []
    for side in (AFTER_SIDE, BEFORE_SIDE):
        groups = grouped_unpaired_rows(report.unpaired_rows, side)
        if not groups:
            continue
        lines.append(f'Benchmarks present only in "{side}" runs:')
        for target, round_numbers, benchmarks in groups:
            lines.append(f"  {target} (rounds {rounds_text(round_numbers)}):")
            lines.extend(
                f"    {benchmark_text(report, target, benchmark)}"
                for benchmark in benchmarks
            )
        lines.append("")
    return lines


def section_terminal(
    report: ComparisonReport,
    section: ReportSection,
) -> list[str]:
    if not section.rows:
        return []
    lines = [f"{section.title}:", ""]
    for row in section.rows:
        lines.extend(
            [
                f"{summary_text(comparison_summary(row.observations), PCT_MIN_BEFORE_NS)}: "
                f"{benchmark_text(report, row.build_target, row.benchmark)}",
                f"  {row.build_target}",
                f"  {', '.join(threshold_pairs(row, section=section, markdown=False))}",
                "",
            ]
        )
    return lines


def render_terminal(
    report: ComparisonReport,
    args: argparse.Namespace,
    manifest: MeasurementManifest,
) -> str:
    targets = manifest.targets
    lines = [
        "Outputs:",
        f"  {args.out / 'comparison.md'}",
        f"  {args.out / 'comparison.tsv'}",
        f"  {args.out / 'round_*'}",
        "",
        (
            f"Target patterns: {', '.join(manifest.target_patterns)} "
            f"({len(targets)} target{'s' if len(targets) != 1 else ''})"
        ),
        f"Before: {manifest.before}",
        f"After: {manifest.after}",
        (
            f"Options: {manifest.mode}; adaptive "
            f"p{fmt_value(manifest.bm_target_percentile)} "
            f"({manifest.bm_max_secs}s max/benchmark)"
        ),
        "Thresholds for the median difference:",
        f"  hi-pri >={fmt_value(args.hi_ns)}ns and >={fmt_value(args.hi_pct)}%",
        f"  lo-pri >={fmt_value(args.lo_ns)}ns and >={fmt_value(args.lo_pct)}%",
        "",
        *threshold_rounding_warning(args, markdown=False),
    ]
    lines.extend(needs_attention_terminal(report))
    lines.extend(unpaired_terminal(report))
    lines.extend(
        [
            "Legend:",
            "",
            *RESULT_DOC.format(
                pct=fmt_value(manifest.bm_target_percentile),
            ).splitlines(),
            "",
        ]
    )
    for section in report.sections:
        lines.extend(section_terminal(report, section))
    return "\n".join(lines).rstrip()


def tsv_rows(
    report: ComparisonReport,
    *,
    out_dir: Path,
) -> list[dict[str, str]]:
    rows = [
        {
            "class": "needs-attention",
            "target": item.target.build_target,
            "details": (
                f"{item.side} round {item.round_number}; {item.reason}; "
                f"logs: {item.log_dir.relative_to(out_dir)}"
            ),
        }
        for item in report.needs_attention
    ]
    for side in (AFTER_SIDE, BEFORE_SIDE):
        for target, benchmark, row_side in sorted(report.unpaired_rows):
            if row_side == side:
                rows.append(
                    {
                        "class": f"only-{side}",
                        "benchmark": benchmark.name,
                        "benchmark_file": benchmark.file,
                        "target": target,
                        "details": f"rounds {rounds_text(report.unpaired_rows[(target, benchmark, row_side)])}",
                    }
                )
    for section in report.sections:
        for row in section.rows:
            summary = comparison_summary(row.observations)
            rows.append(
                {
                    "class": section.classification,
                    "change_between_medians_ns": fmt_value(summary.delta),
                    "change_between_medians_pct": fmt_value(summary.pct),
                    "median_before_ns": fmt_value(summary.before),
                    "median_after_ns": fmt_value(summary.after),
                    "benchmark": row.benchmark.name,
                    "benchmark_file": row.benchmark.file,
                    "target": row.build_target,
                    "before_Δ": ", ".join(
                        threshold_pairs(row, section=section, markdown=False)
                    ),
                }
            )
    return rows


def render_tsv(report: ComparisonReport, *, out_dir: Path) -> str:
    output = io.StringIO()
    writer = csv.DictWriter(
        output,
        delimiter="\t",
        lineterminator="\n",
        fieldnames=[
            "class",
            "change_between_medians_ns",
            "change_between_medians_pct",
            "median_before_ns",
            "median_after_ns",
            "benchmark",
            "benchmark_file",
            "target",
            "details",
            "before_Δ",
        ],
    )
    writer.writeheader()
    writer.writerows(tsv_rows(report, out_dir=out_dir))
    return output.getvalue()


### Entry point


def run_comparison(
    args: argparse.Namespace,
    manifest: MeasurementManifest,
    round_numbers: Iterable[int],
) -> int:
    artifacts = load_artifacts(args.out, manifest.targets, round_numbers)
    if not artifacts:
        raise SystemExit(f"no round_* directories found under: {args.out}")
    report = analyze_report(artifacts, args, manifest.targets)
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "comparison.md").write_text(
        render_markdown(report, args, manifest).rstrip() + "\n",
        encoding="utf-8",
    )
    (args.out / "comparison.tsv").write_text(
        render_tsv(report, out_dir=args.out),
        encoding="utf-8",
    )
    print(file=sys.stderr, flush=True)
    print(render_terminal(report, args, manifest))
    return int(bool(report.needs_attention))


def run_measurement(
    args: argparse.Namespace,
    workspace: Workspace,
) -> int:
    if (
        args.rounds > 0
        and args.out.exists()
        and (not args.out.is_dir() or any(args.out.iterdir()))
    ):
        raise SystemExit(f"measure output directory is not empty: {args.out}")
    check_working_copy(args, workspace)
    start_rev = workspace.resolve_revision(".")
    before = workspace.resolve_revision(args.before)
    after = workspace.resolve_revision(args.after)
    try:
        workspace.checkout(before)
        manifest = MeasurementManifest(
            before=before,
            after=after,
            mode=args.mode,
            bm_max_secs=args.bm_max_secs,
            bm_target_percentile=BM_TARGET_PERCENTILE,
            target_patterns=tuple(args.target_patterns),
            targets=benchmark_targets(args, workspace),
        )
        if args.rounds <= 0:
            return 0
        args.out.mkdir(parents=True, exist_ok=True)
        write_manifest(args.out, manifest)
        run_rounds(args, manifest, workspace)
    except KeyboardInterrupt:
        print("\nAborted. Restoring source tree via:", file=sys.stderr)
        print(f"  sl goto {start_rev}", file=sys.stderr, flush=True)
        raise
    finally:
        workspace.checkout(start_rev)
    return run_comparison(args, manifest, range(1, args.rounds + 1))


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    validate_args(args)
    args.out = (
        (args.out or Path("benchmark_ab_" + time.strftime("%Y-%m-%d_%H%M%S")))
        .expanduser()
        .resolve()
    )
    if args.command == "reanalyze" and not args.out.is_dir():
        raise SystemExit(f"reanalyze output directory does not exist: {args.out}")
    if args.command == "measure":
        buck = shutil.which("buck")
        if buck is None:
            raise SystemExit("`buck` not found in PATH")
        return run_measurement(
            args,
            SaplingWorkspace.discover(
                Path(buck).resolve(),
                directory=Path.cwd(),
            ),
        )
    return run_comparison(
        args,
        load_manifest(args.out),
        discover_numbered_directories(args.out, "round_"),
    )


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
