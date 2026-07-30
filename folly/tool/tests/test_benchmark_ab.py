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

# pyre-strict

import contextlib
import importlib.resources
import io
import json
import sys
import tempfile
import unittest
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path

from folly.tool import benchmark_ab


class CheckoutTrackingWorkspace:
    def __init__(self) -> None:
        self.current_revision = "start"

    def query_buck(self, query: str) -> list[str]:
        raise AssertionError("Buck query should not run")

    def run_buck(
        self,
        arguments: Sequence[str],
        *,
        log_path: Path,
    ) -> int:
        raise AssertionError("Buck should not run")

    def has_changes(self, *, include_untracked: bool) -> bool:
        return False

    def resolve_revision(self, revision: str) -> str:
        if revision == ".":
            return self.current_revision
        return f"{revision}_node"

    def checkout(self, revision: str) -> None:
        self.current_revision = revision


class RelativeRevisionWorkspace(CheckoutTrackingWorkspace):
    def __init__(self, build_target: str) -> None:
        super().__init__()
        self.build_target = build_target
        self.resolve_calls: list[tuple[str, str]] = []
        self.query_revisions: list[str] = []

    def resolve_revision(self, revision: str) -> str:
        self.resolve_calls.append((revision, self.current_revision))
        return super().resolve_revision(revision)

    def query_buck(self, query: str) -> list[str]:
        self.query_revisions.append(self.current_revision)
        return [self.build_target]


@dataclass
class FakeBuckRunner:
    expected_target: str
    nonconverged_runs: int = 0
    run_count: int = 0

    def run_buck(
        self,
        arguments: Sequence[str],
        *,
        log_path: Path,
    ) -> int:
        self.run_count += 1
        if (
            list(arguments[:3]) != ["run", "@mode/opt", self.expected_target]
            or "--bm_target_percentile=33.3" not in arguments
        ):
            return 2
        json_path = Path(
            next(
                argument.removeprefix("--bm_json_verbose=")
                for argument in arguments
                if argument.startswith("--bm_json_verbose=")
            )
        )
        log_path.parent.mkdir(parents=True, exist_ok=True)
        # Simulate non-converged runs followed by a successful run.
        log_path.write_text(
            "Did not converge:\n" if self.run_count <= self.nonconverged_runs else "",
            encoding="utf-8",
        )
        # Distinguish runs so tests can verify which attempt supplied the result.
        json_path.write_text(
            json.dumps([["fixture.cpp", "measured", float(self.run_count)]]),
            encoding="utf-8",
        )
        return 0


class BenchmarkAbTest(unittest.TestCase):
    maxDiff: int | None = None
    target: benchmark_ab.BenchmarkTarget

    def setUp(self) -> None:
        self.target = benchmark_ab.BenchmarkTarget(
            build_target="fbcode//folly/test:bench",
            artifact_name="bench",
        )

    @staticmethod
    def _golden_text(name: str) -> str:
        return (
            importlib.resources.files("folly.tool.tests.testdata")
            .joinpath(name)
            .read_text(encoding="utf-8")
        )

    @staticmethod
    def _benchmark(name: str) -> benchmark_ab.BenchmarkId:
        return benchmark_ab.BenchmarkId(file="fixture.cpp", name=name)

    def _write_attempt_artifact(
        self,
        out: Path,
        round_number: int,
        side: str,
        attempt: int,
        results: dict[benchmark_ab.BenchmarkId, float] | None,
    ) -> None:
        paths = benchmark_ab.attempt_paths(
            out / f"round_{round_number}" / f"{side}_{self.target.artifact_name}",
            self.target,
            attempt,
        )
        paths.directory.mkdir(parents=True)
        convergence_failed = results is None
        paths.json.write_text(
            json.dumps(
                []
                if results is None
                else [
                    [benchmark.file, benchmark.name, value]
                    for benchmark, value in results.items()
                ]
            )
            + "\n",
            encoding="utf-8",
        )
        paths.log.write_text(
            "did not converge\n" if convergence_failed else "",
            encoding="utf-8",
        )
        benchmark_ab.write_attempt_completion(
            paths.completion,
            returncode=0,
            convergence_failed=convergence_failed,
        )

    def _materialize_artifacts(self, out: Path) -> None:
        benchmark_to_round_pairs = {
            self._benchmark("below_threshold"): (
                (10.0, 10.1),
                (10.0, 10.2),
                (10.0, 10.3),
            ),
            benchmark_ab.BenchmarkId(file="loss.cpp", name="same_name"): (
                (1.0, 3.0),
                (1.5, 3.0),
                (2.0, 2.2),
            ),
            benchmark_ab.BenchmarkId(file="win.cpp", name="same_name"): (
                (20.0, 19.5),
                (10.0, 8.0),
                (15.0, 13.0),
            ),
            self._benchmark("low_loss"): (
                (20.0, 20.8),
                (10.0, 10.8),
                (15.0, 15.8),
            ),
            self._benchmark("low_win"): (
                (20.0, 19.2),
                (10.0, 9.2),
                (15.0, 14.2),
            ),
        }
        rounds: list[
            dict[
                str,
                tuple[dict[benchmark_ab.BenchmarkId, float] | None, ...],
            ]
        ] = [
            {
                "before": (None,),
                "after": ({self._benchmark("unpaired_due_to_failed_run"): 1.0},),
            }
        ]
        for round_index in range(3):
            before = {self._benchmark("only_before"): 1.0}
            after = {self._benchmark("only_after"): 2.0}
            for benchmark, round_pairs in benchmark_to_round_pairs.items():
                before[benchmark], after[benchmark] = round_pairs[round_index]
            rounds.append(
                {
                    "before": (None, before) if round_index == 0 else (before,),
                    "after": (after,),
                }
            )
        for round_number, revisions in enumerate(rounds, start=1):
            for side, attempts in revisions.items():
                for attempt, results in enumerate(attempts, start=1):
                    self._write_attempt_artifact(
                        out,
                        round_number,
                        side,
                        attempt,
                        results,
                    )

    def _manifest(self) -> benchmark_ab.MeasurementManifest:
        return benchmark_ab.MeasurementManifest(
            before="1" * 40,
            after="2" * 40,
            mode="@mode/dev",
            bm_max_secs=17,
            bm_target_percentile=33.3,
            target_patterns=("//folly/test/...",),
            targets=(self.target,),
        )

    def test_reanalyze_matches_golden_outputs(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp)
            self._materialize_artifacts(out)
            benchmark_ab.write_manifest(out, self._manifest())
            stdout = io.StringIO()
            stderr = io.StringIO()
            with (
                contextlib.redirect_stdout(stdout),
                contextlib.redirect_stderr(stderr),
            ):
                self.assertEqual(
                    1,
                    benchmark_ab.main(
                        [
                            "reanalyze",
                            "--out",
                            str(out),
                        ]
                    ),
                )

            self.assertEqual("\n", stderr.getvalue())
            self.assertEqual(
                self._golden_text("benchmark_ab_expected.stdout"),
                stdout.getvalue().replace(str(out), "<OUT>"),
            )
            self.assertEqual(
                self._golden_text("benchmark_ab_expected.md"),
                (out / "comparison.md")
                .read_text(encoding="utf-8")
                .replace(str(out), "<OUT>"),
            )
            self.assertEqual(
                self._golden_text("benchmark_ab_expected.tsv"),
                (out / "comparison.tsv").read_text(encoding="utf-8"),
            )

            # Measurement artifacts retain results below report thresholds, so
            # reanalysis can reveal smaller changes without rerunning benchmarks.
            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                self.assertEqual(
                    1,
                    benchmark_ab.main(
                        [
                            "reanalyze",
                            "--out",
                            str(out),
                            "--lo-ns=0.05",
                            "--lo-pct=0.5",
                        ]
                    ),
                )
            self.assertIn("below_threshold", stdout.getvalue())

    def test_measurement_restores_starting_revision_after_discovery_failure(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temp:
            args = benchmark_ab.parse_args(
                [
                    "measure",
                    "--before=before",
                    "--after=after",
                    "--out",
                    str(Path(temp) / "out"),
                    "not-a-buck-selector",
                ]
            )
            workspace = CheckoutTrackingWorkspace()

            with self.assertRaisesRegex(SystemExit, "must be a Buck selector"):
                benchmark_ab.run_measurement(
                    args,
                    workspace,
                )

            self.assertEqual("start", workspace.current_revision)

    def test_measurement_resolves_relative_revisions_before_checkout(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            args = benchmark_ab.parse_args(
                [
                    "measure",
                    "--before=.^",
                    "--after=.",
                    "--rounds=0",
                    "--out",
                    str(Path(temp) / "out"),
                    "//folly/test/...",
                ]
            )
            workspace = RelativeRevisionWorkspace(self.target.build_target)

            self.assertEqual(0, benchmark_ab.run_measurement(args, workspace))
            # `--after=.` must name the starting revision, not the "before"
            # revision checked out for discovery. Resolve both args first.
            self.assertEqual(2, workspace.resolve_calls.count((".", "start")))
            # Target discovery itself runs at the resolved "before" revision.
            self.assertEqual([".^_node"], workspace.query_revisions)
            self.assertEqual("start", workspace.current_revision)

    def test_measurement_rejects_nonempty_output_before_running(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp)
            (out / "stale").write_text("stale\n", encoding="utf-8")
            args = benchmark_ab.parse_args(
                [
                    "measure",
                    "--before=before",
                    "--after=after",
                    "--out",
                    str(out),
                    "//folly/test/...",
                ]
            )

            with self.assertRaises(SystemExit) as raised:
                benchmark_ab.run_measurement(
                    args,
                    CheckoutTrackingWorkspace(),
                )
            self.assertEqual(
                f"measure output directory is not empty: {out}",
                str(raised.exception),
            )

    def test_validate_args_rejects_dangerous_measurement_values(self) -> None:
        for options, error in (
            (("--bm-max-secs=0",), "--bm-max-secs must be at least 1"),
            (
                ("--max-run-attempts=0",),
                "--max-run-attempts must be at least 1",
            ),
            (("--hi-ns=1", "--lo-ns=2"), "--lo-ns must not exceed --hi-ns"),
            (
                ("--hi-pct=10", "--lo-pct=20"),
                "--lo-pct must not exceed --hi-pct",
            ),
        ):
            with self.subTest(options=options):
                args = benchmark_ab.parse_args(
                    [
                        "measure",
                        "--before=before",
                        "--after=after",
                        *options,
                        "//folly/test/...",
                    ]
                )
                with self.assertRaises(SystemExit) as raised:
                    benchmark_ab.validate_args(args)
                self.assertEqual(error, str(raised.exception))

    def test_report_thresholds_must_be_nonnegative_and_finite(self) -> None:
        for option in ("--hi-ns", "--hi-pct", "--lo-ns", "--lo-pct"):
            for value in ("-1", "nan"):
                with (
                    self.subTest(option=option, value=value),
                    contextlib.redirect_stderr(io.StringIO()),
                    self.assertRaises(SystemExit),
                ):
                    benchmark_ab.parse_args(
                        ["reanalyze", "--out=out", f"{option}={value}"]
                    )

    def test_zero_thresholds_still_separate_wins_and_regressions(self) -> None:
        rows = {
            (self.target.build_target, self._benchmark("faster")): [
                benchmark_ab.Observation(1, 10.0, 9.0)
            ],
            (self.target.build_target, self._benchmark("slower")): [
                benchmark_ab.Observation(1, 10.0, 11.0)
            ],
            (self.target.build_target, self._benchmark("displayed_zero")): [
                benchmark_ab.Observation(1, 10.0, 10.04)
            ],
        }
        threshold = benchmark_ab.Threshold(ns=0.0, pct=0.0)

        # With zero thresholds, direction at report precision is the only guard
        # against reporting slowdowns as wins and speedups as regressions.
        wins = benchmark_ab.bucket_rows(rows, direction=-1, threshold=threshold)
        regressions = benchmark_ab.bucket_rows(rows, direction=1, threshold=threshold)

        self.assertEqual(
            ["faster"],
            [row.benchmark.name for row in wins],
        )
        self.assertEqual(
            ["slower"],
            [row.benchmark.name for row in regressions],
        )

    def test_classification_matches_report_precision(self) -> None:
        summary = benchmark_ab.ComparisonSummary(
            before=9.0,
            after=9.96,
            delta=0.96,
        )

        self.assertEqual(
            "9.0+1.0ns (+10.7%)",
            benchmark_ab.summary_text(summary, pct_min_before_ns=2.0),
        )
        self.assertTrue(
            benchmark_ab.Threshold(ns=1.0, pct=10.0).met_by(
                summary,
                direction=1,
            ),
        )

        args = benchmark_ab.parse_args(["reanalyze", "--out=out", "--hi-ns=0.96"])
        report = benchmark_ab.analyze_report({}, args, (self.target,))
        for rendered in (
            benchmark_ab.render_markdown(report, args, self._manifest()),
            benchmark_ab.render_terminal(report, args, self._manifest()),
        ):
            self.assertIn(">=1.0ns", rendered)
            self.assertIn(
                "Thresholds were rounded to one decimal place for classification.",
                rendered,
            )

    def test_target_artifact_names_are_unique_and_bounded(self) -> None:
        self.assertEqual(
            ["001_fbcode_foo_a_bench", "002_fbcode_foo_a_bench"],
            [
                target.artifact_name
                for target in benchmark_ab.make_benchmark_targets(
                    ("fbcode//foo/a:bench", "fbcode//foo_a:bench")
                )
            ],
        )

        long_name = benchmark_ab.make_benchmark_targets(
            (f"//{'a' * 250}:important_benchmark",)
        )[0].artifact_name
        self.assertEqual(200, len(long_name))
        self.assertTrue(long_name.endswith("important_benchmark"))

    def test_run_one_benchmark_retries_until_result_is_usable(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            args = benchmark_ab.parse_args(
                [
                    "measure",
                    "--before=before",
                    "--after=after",
                    "--max-run-attempts=2",
                    "--out",
                    str(Path(temp) / "out"),
                    "//folly/test/...",
                ]
            )
            buck = FakeBuckRunner(
                self.target.build_target,
                nonconverged_runs=1,
            )

            artifact = benchmark_ab.run_one_benchmark(
                args,
                round_number=1,
                round_count=1,
                side=benchmark_ab.BEFORE_SIDE,
                target=self.target,
                buck=buck,
            )

            self.assertEqual(2, buck.run_count)
            self.assertEqual(
                [True, False],
                [attempt.convergence_failed for attempt in artifact.attempts],
            )
            self.assertEqual(
                {self._benchmark("measured"): 2.0},
                artifact.results,
            )

    def test_workspace_combines_buck_stderr_with_log(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            log_path = root / "buck.log"
            workspace = benchmark_ab.SaplingWorkspace(
                root=root,
                buck_executable=Path(sys.executable),
            )

            # Folly writes the convergence marker to stderr. If it escapes the
            # log, run_one_benchmark() can accept a non-converged result.
            returncode = workspace.run_buck(
                ["-c", "import sys; print('Did not converge:', file=sys.stderr)"],
                log_path=log_path,
            )

            self.assertEqual(0, returncode)
            self.assertTrue(benchmark_ab.log_has_convergence_failure(log_path))

    def test_workspace_queries_buck_from_discovered_cell(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            cell = root / "cell"
            cell.mkdir()
            tool_dir = root / "tool"
            tool_dir.mkdir()
            buck = root / "buck"
            # The fake maps the caller's directory to a distinct cell, then
            # accepts a query only from that cell.
            buck.write_text(
                f"""#!{sys.executable}
import pathlib
import sys

if sys.argv[1:] == ["root", "--kind", "cell", "--dir", {str(tool_dir)!r}]:
    print({str(cell)!r})
elif sys.argv[1] == "uquery" and pathlib.Path.cwd() == pathlib.Path({str(cell)!r}):
    print("other//folly/test:bench")
else:
    sys.exit(2)
""",
                encoding="utf-8",
            )
            buck.chmod(0o755)

            self.assertEqual(
                ["other//folly/test:bench"],
                benchmark_ab.SaplingWorkspace.discover(
                    buck,
                    directory=tool_dir,
                ).query_buck("//folly/test:bench"),
            )

    def test_results_distinguish_same_name_in_different_files(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "results.json"
            path.write_text(
                json.dumps(
                    [
                        ["one.cpp", "same_name", 1.0],
                        [
                            "two.cpp",
                            "same_name",
                            2.0,
                            {"items": {"value": 2, "type": 2}},
                        ],
                    ]
                ),
                encoding="utf-8",
            )

            self.assertEqual(
                {
                    benchmark_ab.BenchmarkId(file="one.cpp", name="same_name"): 1.0,
                    benchmark_ab.BenchmarkId(file="two.cpp", name="same_name"): 2.0,
                },
                benchmark_ab.load_results(path),
            )

    def test_results_reject_google_benchmark_json(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "results.json"
            path.write_text(
                '{"benchmarks": [{"time_unit": "ns"}]}',
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ValueError, "benchmark JSON"):
                benchmark_ab.load_results(path)

    def test_reanalyze_does_not_use_incomplete_attempt(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            base_dir = Path(temp)
            paths = benchmark_ab.attempt_paths(base_dir, self.target, 1)
            paths.directory.mkdir()
            # An interruption can leave valid output without recording completion.
            paths.json.write_text(
                '[["fixture.cpp", "measured", 1.0]]', encoding="utf-8"
            )
            paths.log.write_text("", encoding="utf-8")

            artifact = benchmark_ab.load_run_artifact(
                round_number=1,
                side=benchmark_ab.BEFORE_SIDE,
                target=self.target,
                base_dir=base_dir,
            )

            self.assertFalse(artifact.attempts[0].completed)
            self.assertEqual({}, artifact.results)

    def test_benchmark_text_adds_file_only_for_same_target_collision(self) -> None:
        report = benchmark_ab.ComparisonReport(
            needs_attention=(),
            unpaired_rows={},
            benchmark_names_with_multiple_files=frozenset(
                {(self.target.build_target, "duplicate")}
            ),
            sections=(),
        )

        for build_target, benchmark, expected in (
            (self.target.build_target, self._benchmark("unique"), "unique"),
            (
                self.target.build_target,
                self._benchmark("duplicate"),
                "duplicate (fixture.cpp)",
            ),
            ("fbcode//other:bench", self._benchmark("duplicate"), "duplicate"),
        ):
            with self.subTest(build_target=build_target, benchmark=benchmark):
                self.assertEqual(
                    expected,
                    benchmark_ab.benchmark_text(report, build_target, benchmark),
                )

    def test_convergence_check_uses_adaptive_failure_marker(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            log_path = Path(temp) / "benchmark.log"

            # Verbose logs include benchmark names, so a name containing this
            # word must not turn a successful run into a retry.
            log_path.write_text(
                "unconverged_benchmark\ndid not converge:\n",
                encoding="utf-8",
            )
            self.assertFalse(benchmark_ab.log_has_convergence_failure(log_path))

            log_path.write_text("Did not converge:\n", encoding="utf-8")
            self.assertTrue(benchmark_ab.log_has_convergence_failure(log_path))

    def test_reanalyze_requires_a_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp)
            with self.assertRaises(SystemExit) as raised:
                benchmark_ab.main(["reanalyze", "--out", str(out)])

            self.assertEqual(
                f"measurement manifest does not exist: {out / benchmark_ab.MANIFEST_FILENAME}",
                str(raised.exception),
            )

    def test_manifest_rejects_target_collisions_and_escape(self) -> None:
        cases = (
            (
                (
                    benchmark_ab.BenchmarkTarget(
                        build_target="target", artifact_name="one"
                    ),
                    benchmark_ab.BenchmarkTarget(
                        build_target="target", artifact_name="two"
                    ),
                ),
                "build targets must be unique",
            ),
            (
                (
                    benchmark_ab.BenchmarkTarget(
                        build_target="one", artifact_name="same"
                    ),
                    benchmark_ab.BenchmarkTarget(
                        build_target="two", artifact_name="same"
                    ),
                ),
                "target artifact names must be unique",
            ),
            (
                (
                    benchmark_ab.BenchmarkTarget(
                        build_target="target", artifact_name="../escape"
                    ),
                ),
                "single path component",
            ),
        )
        for targets, error in cases:
            with self.subTest(error=error), self.assertRaisesRegex(ValueError, error):
                benchmark_ab.MeasurementManifest(
                    before="before",
                    after="after",
                    mode="@mode/opt",
                    bm_max_secs=30,
                    bm_target_percentile=33.3,
                    target_patterns=("//folly/test/...",),
                    targets=targets,
                )

    def test_load_manifest_rejects_malformed_data(self) -> None:
        valid_data: dict[str, object] = {
            "before": "before",
            "after": "after",
            "mode": "@mode/opt",
            "bm_max_secs": 30,
            "bm_target_percentile": 33.3,
            "target_patterns": ["//folly/test/..."],
            "targets": [{"build_target": "target", "artifact_name": "target"}],
        }
        cases = (
            ([], "top level must be an object"),
            (
                {
                    **valid_data,
                    "targets": [{"build_target": 1, "artifact_name": "target"}],
                },
                "build_target and artifact_name must be strings",
            ),
        )
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / benchmark_ab.MANIFEST_FILENAME
            for data, error in cases:
                with self.subTest(error=error):
                    path.write_text(
                        json.dumps(data),
                        encoding="utf-8",
                    )
                    with self.assertRaisesRegex(SystemExit, error):
                        benchmark_ab.load_manifest(path.parent)

    def test_missing_attempts_need_attention(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp)
            artifact = benchmark_ab.load_run_artifact(
                round_number=1,
                side=benchmark_ab.BEFORE_SIDE,
                target=self.target,
                base_dir=out / "missing",
            )

            attention = benchmark_ab.needs_attention_for_run(
                artifact,
                out_dir=out,
            )
            self.assertIsNotNone(attention)
            self.assertEqual(
                "No attempts found for this benchmark run",
                attention.reason,
            )

    def test_empty_results_need_attention(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp)
            self._write_attempt_artifact(
                out,
                round_number=1,
                side=benchmark_ab.BEFORE_SIDE,
                attempt=1,
                results={},
            )
            attention = benchmark_ab.needs_attention_for_run(
                benchmark_ab.load_run_artifact(
                    round_number=1,
                    side=benchmark_ab.BEFORE_SIDE,
                    target=self.target,
                    base_dir=out / "round_1" / "before_bench",
                ),
                out_dir=out,
            )
            self.assertIsNotNone(attention)
            self.assertEqual("Benchmark run produced no results", attention.reason)

    def test_summary_uses_all_cross_side_differences(self) -> None:
        summary = benchmark_ab.comparison_summary(
            (
                benchmark_ab.Observation(1, 10.0, 1010.0),
                benchmark_ab.Observation(2, 100.0, 150.0),
                benchmark_ab.Observation(3, 1000.0, 1200.0),
            )
        )

        self.assertEqual(100.0, summary.before)
        self.assertEqual(1010.0, summary.after)
        # The marginal medians differ by 910ns, while the Hodges-Lehmann
        # estimate reflects the typical difference across all combinations.
        self.assertEqual(200.0, summary.delta)
        self.assertEqual(200.0, summary.pct)

    def test_percentage_floors_sub_picosecond_timings(self) -> None:
        # Adaptive baseline subtraction can produce zero; sub-picosecond
        # differences should remain noise rather than create an infinite ratio.
        self.assertEqual(0.0, benchmark_ab.Observation(1, 0.0, 0.0).pct)
        self.assertEqual(0.0, benchmark_ab.Observation(1, 0.0, 0.0005).pct)
        self.assertEqual(99_900.0, benchmark_ab.Observation(1, 0.0, 1.0).pct)
        self.assertEqual(
            0.0,
            benchmark_ab.comparison_summary(
                (benchmark_ab.Observation(1, 0.0, 0.0005),)
            ).pct,
        )

    def test_bucket_omits_mixed_directions_with_zero_estimated_delta(self) -> None:
        rows = {
            (self.target.build_target, self._benchmark("mixed_directions")): [
                benchmark_ab.Observation(round_number, 10.0, after)
                for round_number, after in enumerate(
                    (8.0, 8.0, 8.0, 12.0, 12.0, 12.0),
                    start=1,
                )
            ]
        }
        threshold = benchmark_ab.Threshold(ns=1.0, pct=10.0)

        self.assertEqual(
            (),
            benchmark_ab.bucket_rows(rows, direction=-1, threshold=threshold),
        )
        self.assertEqual(
            (),
            benchmark_ab.bucket_rows(rows, direction=1, threshold=threshold),
        )

    def test_bucket_classifies_by_cross_side_estimate_not_round_votes(self) -> None:
        rows = {
            (self.target.build_target, self._benchmark("regression")): [
                benchmark_ab.Observation(round_number, before, after)
                for round_number, (before, after) in enumerate(
                    (
                        (1.0, 101.0),
                        (2.0, 1.5),
                        (3.0, 2.5),
                        (4.0, 3.5),
                        (100.0, 5.0),
                    ),
                    start=1,
                )
            ]
        }

        # Four paired deltas are negative; inclusion proves that classification
        # uses the cross-side estimate rather than a per-round vote.
        self.assertEqual(
            ["regression"],
            [
                row.benchmark.name
                for row in benchmark_ab.bucket_rows(
                    rows,
                    direction=1,
                    threshold=benchmark_ab.Threshold(ns=0.1, pct=10.0),
                )
            ],
        )

    def test_bucket_sorts_by_estimated_delta(self) -> None:
        rows = {
            (self.target.build_target, self._benchmark(benchmark)): [
                benchmark_ab.Observation(round_number, before, after)
                for round_number, (before, after) in enumerate(pairs, start=1)
            ]
            for benchmark, pairs in (
                ("large_win", ((20.0, 19.5), (10.0, 9.5), (15.0, 2.0))),
                ("small_win", ((20.0, 15.0), (10.0, 8.0), (15.0, 14.0))),
                ("large_loss", ((20.0, 20.5), (10.0, 10.5), (15.0, 28.0))),
                ("small_loss", ((20.0, 25.0), (10.0, 12.0), (15.0, 16.0))),
            )
        }
        threshold = benchmark_ab.Threshold(ns=0.1, pct=0.1)

        self.assertEqual(
            ["large_win", "small_win"],
            [
                row.benchmark.name
                for row in benchmark_ab.bucket_rows(
                    rows, direction=-1, threshold=threshold
                )
            ],
        )
        self.assertEqual(
            ["small_loss", "large_loss"],
            [
                row.benchmark.name
                for row in benchmark_ab.bucket_rows(
                    rows, direction=1, threshold=threshold
                )
            ],
        )
