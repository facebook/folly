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
import subprocess
import tempfile
import unittest
from pathlib import Path
from typing import BinaryIO
from unittest import mock

from folly.agents.backtest import run_scenario as runner


REVISION = "a" * 40


def write(path: Path, contents: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(contents)


def executable(path: Path, contents: str = "tool") -> Path:
    write(path, contents)
    path.chmod(0o755)
    return path


def write_manifest(path: Path, contents: dict[str, object]) -> runner.Manifest:
    path.write_text(json.dumps(contents))
    return runner.load_manifest(path)


def make_tools(rules_root: Path) -> None:
    for relative in runner.TOOL_FILES.values():
        executable(rules_root / relative)


class RunScenarioTest(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.scenario = self.root / "scenario"
        self.rules_root = self.root / "agents"
        self.run_root = self.root / "runs"
        self.scenario.mkdir()
        self.rules_root.mkdir()
        write(self.scenario / "prompt.md", "prompt")

    def manifest(
        self,
        *,
        prompt: str = "prompt.md",
        no_rules_prompt: str | None = None,
        inputs: list[dict[str, str]] | None = None,
        rules: list[str] | None = None,
        scenario: Path | None = None,
    ) -> runner.Manifest:
        scenario = scenario or self.scenario
        contents: dict[str, object] = {
            "prompt": prompt,
            "inputs": inputs or [],
            "rules": rules or [],
        }
        if no_rules_prompt is not None:
            contents["no_rules_prompt"] = no_rules_prompt
        return write_manifest(scenario / "scenario.json", contents)

    def prepare(
        self, scenario: Path | None = None, *, install_rules: bool = True
    ) -> runner.Run:
        return runner.prepare(
            scenario or self.scenario,
            self.rules_root,
            self.run_root,
            "model",
            "high",
            REVISION,
            install_rules=install_rules,
        )

    def prepare_launch(self) -> tuple[runner.Run, Path]:
        make_tools(self.rules_root)
        codex = executable(self.root / "codex")
        return self.prepare(), codex

    @staticmethod
    def source_control(
        *, status: bytes = b"", revision: str = REVISION
    ) -> tuple[list[list[str]], runner.CommandRunner]:
        commands: list[list[str]] = []

        def run(
            command: list[str], **unused: object
        ) -> subprocess.CompletedProcess[bytes]:
            commands.append(command)
            stdout = status if command[1] == "status" else (revision + "\n").encode()
            return subprocess.CompletedProcess(command, 0, stdout, b"")

        return commands, run

    @staticmethod
    def fake_codex(
        run: runner.Run,
        *,
        returncode: int,
        output: str | None,
        trace: bytes = b"",
        errors: bytes = b"",
    ) -> tuple[list[list[str]], list[dict[str, str]], runner.CommandRunner]:
        commands: list[list[str]] = []
        environments: list[dict[str, str]] = []

        def execute(
            command: list[str],
            *,
            stdout: BinaryIO,
            stderr: BinaryIO,
            env: dict[str, str],
            **unused: object,
        ) -> subprocess.CompletedProcess[bytes]:
            commands.append(command)
            environments.append(env)
            if output is not None:
                (run.workdir / "output.md").write_text(output)
            stdout.write(trace)
            stderr.write(errors)
            return subprocess.CompletedProcess(command, returncode)

        return commands, environments, execute

    def test_parser_has_no_alternate_rules_root(self) -> None:
        args = runner._parser().parse_args(
            ["scenario", "--model", "model", "--reasoning-effort", "high"]
        )

        self.assertNotIn("rules_root", vars(args))
        self.assertFalse(args.no_rules)

    def test_parser_accepts_no_rules(self) -> None:
        args = runner._parser().parse_args(
            [
                "scenario",
                "--model",
                "model",
                "--reasoning-effort",
                "high",
                "--no-rules",
            ]
        )

        self.assertTrue(args.no_rules)

    def test_generation_revision_checks_the_files_that_define_a_run(self) -> None:
        (self.root / ".hg").mkdir()
        runner_path = self.root / "run_scenario.py"
        write(runner_path, "runner")
        write(self.scenario / "prompt.no-rules.md", "bare prompt")
        write(self.scenario / "input/data.md", "data")
        write(self.rules_root / "writing.md", "writing")
        make_tools(self.rules_root)
        manifest = self.manifest(
            no_rules_prompt="prompt.no-rules.md",
            inputs=[{"source": "input", "destination": "input"}],
            rules=["writing.md"],
        )
        commands, command_runner = self.source_control()

        self.assertEqual(
            runner.generation_revision(
                self.scenario,
                manifest,
                self.rules_root,
                runner_path=runner_path,
                command_runner=command_runner,
            ),
            REVISION,
        )
        status = commands[0]
        self.assertEqual(status[:4], ["sl", "status", "-mardui", "--root-relative"])
        self.assertTrue(
            {
                "run_scenario.py",
                "scenario/prompt.md",
                "scenario/scenario.json",
                "scenario/input",
                "agents/writing.md",
                "agents/critic-iterate/codex-reviewer.py",
            }.issubset(status)
        )
        self.assertNotIn("scenario/prompt.no-rules.md", status)

    def test_generation_revision_rejects_dirty_or_untracked_inputs(self) -> None:
        (self.root / ".hg").mkdir()
        runner_path = self.root / "run_scenario.py"
        write(runner_path, "runner")
        make_tools(self.rules_root)
        manifest = self.manifest()

        for marker in ("M", "?", "I"):
            status = f"{marker} scenario/prompt.md\n".encode()
            with self.subTest(status=status):
                _, command_runner = self.source_control(status=status)
                with self.assertRaises(runner.RunnerError) as raised:
                    runner.generation_revision(
                        self.scenario,
                        manifest,
                        self.rules_root,
                        runner_path=runner_path,
                        command_runner=command_runner,
                    )
                message = str(raised.exception)
                self.assertIn("refusing to run", message)
                self.assertIn(
                    "ask the user to authorize a commit containing only the listed "
                    "source files",
                    message,
                )
                self.assertIn("scenario/prompt.md", message)

    def test_generation_revision_supports_git_checkouts(self) -> None:
        (self.root / ".git").mkdir()
        runner_path = self.root / "run_scenario.py"
        write(runner_path, "runner")
        make_tools(self.rules_root)
        manifest = self.manifest()
        commands, command_runner = self.source_control()

        self.assertEqual(
            runner.generation_revision(
                self.scenario,
                manifest,
                self.rules_root,
                runner_path=runner_path,
                command_runner=command_runner,
            ),
            REVISION,
        )
        self.assertEqual(commands[0][0:3], ["git", "status", "--porcelain=v1"])
        self.assertIn("--untracked-files=all", commands[0])
        self.assertIn("--ignored=matching", commands[0])
        self.assertTrue(any("critic-iterate" in argument for argument in commands[0]))
        self.assertEqual(commands[1], ["git", "rev-parse", "HEAD"])

    def test_generation_revision_omits_rules_for_no_rules_mode(self) -> None:
        (self.root / ".git").mkdir()
        runner_path = self.root / "run_scenario.py"
        write(runner_path, "runner")
        write(self.scenario / "prompt.no-rules.md", "bare prompt")
        write(self.rules_root / "writing.md", "writing")
        make_tools(self.rules_root)
        manifest = self.manifest(
            no_rules_prompt="prompt.no-rules.md", rules=["writing.md"]
        )
        commands, command_runner = self.source_control()

        self.assertEqual(
            runner.generation_revision(
                self.scenario,
                manifest,
                self.rules_root,
                install_rules=False,
                runner_path=runner_path,
                command_runner=command_runner,
            ),
            REVISION,
        )
        status = commands[0]
        self.assertIn("scenario/prompt.no-rules.md", status)
        self.assertNotIn("scenario/prompt.md", status)
        self.assertFalse(any("writing.md" in argument for argument in status))
        self.assertFalse(any("codex-reviewer.py" in argument for argument in status))

    def test_generation_revision_rejects_multiple_checkouts(self) -> None:
        agents_root = self.root / "rules-checkout/folly/agents"
        scenario = self.root / "scenario-checkout/scenario"
        runner_path = agents_root / "backtest/run_scenario.py"
        (self.root / "rules-checkout/.git").mkdir(parents=True)
        (self.root / "scenario-checkout/.git").mkdir(parents=True)
        write(runner_path, "runner")
        write(scenario / "prompt.md", "prompt")
        make_tools(agents_root)
        manifest = write_manifest(
            scenario / "scenario.json",
            {"prompt": "prompt.md", "inputs": [], "rules": []},
        )

        with self.assertRaisesRegex(runner.RunnerError, "one source checkout"):
            runner.generation_revision(
                scenario,
                manifest,
                agents_root,
                runner_path=runner_path,
            )

    def test_manifest_rejects_unsafe_paths_and_development_rules(self) -> None:
        cases: tuple[tuple[str, dict[str, object], str], ...] = (
            (
                "input escape",
                {
                    "prompt": "prompt.md",
                    "inputs": [{"source": "../secret", "destination": "secret"}],
                    "rules": [],
                },
                "without '..'",
            ),
            (
                "development rule",
                {"prompt": "prompt.md", "inputs": [], "rules": ["writing.contrib.md"]},
                "development material",
            ),
            (
                "reserved rules destination",
                {
                    "prompt": "prompt.md",
                    "inputs": [
                        {"source": "evidence.md", "destination": "rules/injected.md"}
                    ],
                    "rules": [],
                },
                "reserved rules",
            ),
            (
                "hidden Codex policy",
                {
                    "prompt": "prompt.md",
                    "inputs": [
                        {
                            "source": "evidence.md",
                            "destination": "evidence/AGENTS.md",
                        }
                    ],
                    "rules": [],
                },
                "hidden policy",
            ),
        )
        for name, contents, error in cases:
            with self.subTest(name), self.assertRaisesRegex(runner.RunnerError, error):
                write_manifest(self.scenario / "scenario.json", contents)

    def test_prepare_rejects_a_run_root_inside_inputs_or_rules(self) -> None:
        self.manifest()
        for run_root, error in (
            (self.scenario / "runs", "inside the scenario"),
            (self.rules_root / "runs", "inside the rules"),
        ):
            with self.subTest(error), self.assertRaisesRegex(runner.RunnerError, error):
                runner.prepare(
                    self.scenario,
                    self.rules_root,
                    run_root,
                    "model",
                    "high",
                    REVISION,
                )

    def test_prepare_removes_a_partial_run_when_staging_fails(self) -> None:
        self.manifest(rules=["missing.md"])

        with self.assertRaisesRegex(runner.RunnerError, "does not exist"):
            self.prepare()

        self.assertEqual(list(self.run_root.iterdir()), [])

    def test_prepare_prefixes_the_rule_loading_instruction(self) -> None:
        write(self.scenario / "prompt.md", "Do the task.\n")
        write(self.scenario / "prompt.no-rules.md", "Do the bare task.\n")
        self.manifest(no_rules_prompt="prompt.no-rules.md")

        run = self.prepare()

        self.assertEqual(
            run.prompt.read_text(),
            "Read every rule listed in `rules/rules-inventory.md`, in order. "
            "Follow those rules for conditional loads; do not look for ambient "
            "rule files.\n\nDo the task.\n",
        )
        self.assertTrue((run.workdir / "rules/rules-inventory.md").is_file())
        metadata = json.loads((run.root / "run.json").read_text())
        self.assertEqual(metadata["generation_revision"], REVISION)
        self.assertNotIn("no_rules", metadata)

    def test_prepare_without_rules_stages_bare_inputs(self) -> None:
        write(self.scenario / "prompt.md", "Do the task.\n")
        write(self.scenario / "prompt.no-rules.md", "Do the bare task.\n")
        write(self.scenario / "input.md", "input")
        write(self.rules_root / "writing.md", "writing")
        self.manifest(
            no_rules_prompt="prompt.no-rules.md",
            inputs=[{"source": "input.md", "destination": "input.md"}],
            rules=["writing.md"],
        )

        run = self.prepare(install_rules=False)

        self.assertEqual(run.prompt.read_text(), "Do the bare task.\n")
        self.assertEqual({path.name for path in run.workdir.iterdir()}, {"input.md"})
        self.assertFalse(run.install_rules)
        metadata = json.loads((run.root / "run.json").read_text())
        self.assertEqual(metadata["rules"], [])
        self.assertTrue(metadata["no_rules"])

    def test_staging_rejects_symlinks_outside_declared_roots(self) -> None:
        write(self.root / "outside.md", "outside")
        (self.scenario / "prompt.md").unlink()
        (self.scenario / "input.md").symlink_to(self.root / "outside.md")
        self.manifest(
            prompt="input.md",
            inputs=[{"source": "input.md", "destination": "input.md"}],
        )
        with self.assertRaisesRegex(runner.RunnerError, "resolves outside"):
            self.prepare()

        input_scenario = self.root / "input-scenario"
        input_scenario.mkdir()
        write(input_scenario / "prompt.md", "prompt")
        (input_scenario / "input.md").symlink_to(self.root / "outside.md")
        input_manifest = self.manifest(
            inputs=[{"source": "input.md", "destination": "input.md"}],
            scenario=input_scenario,
        )
        with self.assertRaisesRegex(runner.RunnerError, "resolves outside"):
            runner.stage(
                input_scenario,
                input_manifest,
                self.rules_root,
                self.root / "input-workdir",
            )

        tree_scenario = self.root / "tree-scenario"
        tree_scenario.mkdir()
        write(tree_scenario / "prompt.md", "prompt")
        (tree_scenario / "source").mkdir()
        (tree_scenario / "source/link.md").symlink_to(self.root / "outside.md")
        tree_manifest = self.manifest(
            inputs=[{"source": "source", "destination": "source"}],
            scenario=tree_scenario,
        )
        with self.assertRaisesRegex(runner.RunnerError, "may not contain symlinks"):
            runner.stage(
                tree_scenario,
                tree_manifest,
                self.rules_root,
                self.root / "tree-workdir",
            )

        (self.rules_root / "writing.md").symlink_to(self.root / "outside.md")
        rule_manifest = self.manifest(rules=["writing.md"], scenario=input_scenario)
        with self.assertRaisesRegex(runner.RunnerError, "resolves outside"):
            runner.stage(
                input_scenario,
                rule_manifest,
                self.rules_root,
                self.root / "rule-workdir",
            )

    def test_stage_copies_only_declared_inputs_and_rules(self) -> None:
        workdir = self.root / "workdir"
        workdir.mkdir()
        write(self.scenario / "draft.md", "draft")
        write(self.scenario / "source/Api.h.txt", "header")
        write(self.scenario / "samples/1/output.md", "prior output")
        write(self.rules_root / "writing.md", "writing")
        write(self.rules_root / runner.CRITIC_ITERATE_RULE, "critic")
        for support in runner.CRITIC_ITERATE_SUPPORT_FILES:
            write(self.rules_root / support, support.name)
        write(self.rules_root / "writing.contrib.md", "decoy")
        manifest = self.manifest(
            inputs=[
                {"source": "draft.md", "destination": "output.md"},
                {"source": "source", "destination": "source"},
            ],
            rules=["writing.md", "critic-iterate.md"],
        )

        runner.stage(self.scenario, manifest, self.rules_root, workdir)

        self.assertEqual((workdir / "source/Api.h").read_text(), "header")
        self.assertTrue((workdir / "output.md").stat().st_mode & 0o200)
        self.assertFalse((workdir / "source/Api.h").stat().st_mode & 0o200)
        self.assertFalse((workdir / "rules/writing.contrib.md").exists())
        self.assertFalse((workdir / "samples").exists())
        for support in runner.CRITIC_ITERATE_SUPPORT_FILES:
            self.assertTrue((workdir / "rules" / support).is_file())
        inventory = (workdir / "rules/rules-inventory.md").read_text()
        self.assertIn("1. `writing.md`", inventory)
        self.assertIn("2. `critic-iterate.md`", inventory)

    def test_stage_rejects_hidden_policy_inside_an_input_tree(self) -> None:
        workdir = self.root / "workdir"
        workdir.mkdir()
        write(self.scenario / "source/AGENTS.md", "hidden policy")
        manifest = self.manifest(inputs=[{"source": "source", "destination": "source"}])

        with self.assertRaisesRegex(runner.RunnerError, "hidden policy"):
            runner.stage(self.scenario, manifest, self.rules_root, workdir)
        self.assertEqual(list(workdir.iterdir()), [])

    def test_stage_rejects_suffix_collision_before_writing(self) -> None:
        workdir = self.root / "workdir"
        workdir.mkdir()
        write(self.scenario / "source/Api.h.txt", "snapshot")
        write(self.scenario / "source/Api.h", "native")
        manifest = self.manifest(inputs=[{"source": "source", "destination": "source"}])

        with self.assertRaisesRegex(runner.RunnerError, "map to source/Api.h"):
            runner.stage(self.scenario, manifest, self.rules_root, workdir)
        self.assertEqual(list(workdir.iterdir()), [])

    def test_launch_uses_isolated_tools_and_preserves_output(self) -> None:
        write(self.scenario / "draft.md", "draft")
        self.manifest(inputs=[{"source": "draft.md", "destination": "output.md"}])
        run, codex = self.prepare_launch()
        commands, environments, command_runner = self.fake_codex(
            run,
            returncode=0,
            output="complete",
            trace=b"trace\n",
            errors=b"diagnostic\n",
        )
        forbidden_environment = {
            "CODEX_REVIEWER",
            "CODEX_FRESH_REVIEW",
            "CRITIC_ITERATE_RULES_DIR",
            "CODEX_UNRELATED",
            "PYTHONHOME",
            "PYTHONINSPECT",
            "PYTHONPATH",
            "PYTHONWARNINGS",
            "BASH_ENV",
            "ENV",
            "ZDOTDIR",
        }

        with mock.patch.dict(os.environ, dict.fromkeys(forbidden_environment, "set")):
            result = runner.launch(
                run, codex, "model", "high", command_runner=command_runner
            )

        self.assertEqual(result, 0)
        self.assertEqual(
            commands,
            [
                [
                    str(codex.resolve()),
                    "-a",
                    "never",
                    "exec",
                    "--skip-git-repo-check",
                    "--json",
                    "--model",
                    "model",
                    "--config",
                    'model_reasoning_effort="high"',
                    "--cd",
                    str(run.workdir),
                    "-",
                ]
            ],
        )
        (environment,) = environments
        self.assertTrue(
            forbidden_environment.isdisjoint(environment),
            forbidden_environment & environment.keys(),
        )
        tool_bin = run.root / "bin"
        self.assertEqual(environment["PATH"].split(os.pathsep, 1)[0], str(tool_bin))
        for name, relative in runner.TOOL_FILES.items():
            self.assertEqual(
                (tool_bin / name).resolve(), (self.rules_root / relative).resolve()
            )
        policy = (run.codex_home / "rules/default.rules").read_text()
        for name in runner.TOOL_FILES:
            self.assertIn(f'host_executable(name="{name}"', policy)
            self.assertIn(f'prefix_rule(pattern=["{name}"]', policy)
        self.assertEqual((run.root / "output.md").read_text(), "complete")
        self.assertEqual((run.root / "trace.jsonl").read_text(), "trace\n")
        self.assertEqual((run.root / "err.txt").read_text(), "diagnostic\n")
        metadata = json.loads((run.root / "run.json").read_text())
        self.assertEqual(metadata["status"], "complete")
        self.assertEqual(metadata["exit_code"], 0)
        self.assertEqual(metadata["executables"]["codex"], str(codex.resolve()))

    def test_failed_launch_preserves_partial_output_and_diagnostics(self) -> None:
        write(self.scenario / "draft.md", "draft")
        self.manifest(inputs=[{"source": "draft.md", "destination": "output.md"}])
        run, codex = self.prepare_launch()
        _, _, command_runner = self.fake_codex(
            run,
            returncode=7,
            output="partial",
            trace=b"trace\n",
            errors=b"failure\n",
        )

        result = runner.launch(
            run, codex, "model", "high", command_runner=command_runner
        )

        self.assertEqual(result, 7)
        self.assertEqual((run.root / "output.md").read_text(), "partial")
        self.assertEqual((run.root / "trace.jsonl").read_text(), "trace\n")
        self.assertEqual((run.root / "err.txt").read_text(), "failure\n")
        metadata = json.loads((run.root / "run.json").read_text())
        self.assertEqual(metadata["status"], "failed")
        self.assertEqual(metadata["exit_code"], 7)
        self.assertEqual(metadata["executables"]["codex"], str(codex.resolve()))

    def test_finish_run_preserves_failure_with_invalid_partial_output(self) -> None:
        self.manifest()
        run = self.prepare()
        (run.workdir / "output.md").mkdir()

        self.assertEqual(runner._finish_run(run, 7), 7)

        metadata = json.loads((run.root / "run.json").read_text())
        self.assertEqual(metadata["status"], "failed")
        self.assertEqual(metadata["exit_code"], 7)
        self.assertEqual(
            metadata["output_preservation_error"],
            "output.md is not a regular file",
        )

    def test_launch_rejects_a_helper_outside_the_rules_tree(self) -> None:
        self.manifest()
        run, codex = self.prepare_launch()
        outside = executable(self.root / "outside-reviewer")
        reviewer = self.rules_root / runner.TOOL_FILES["codex-reviewer.py"]
        reviewer.unlink()
        reviewer.symlink_to(outside)

        with self.assertRaisesRegex(runner.RunnerError, "resolves outside"):
            runner.launch(run, codex, "model", "high")

        self.assertEqual(
            json.loads((run.root / "run.json").read_text())["status"],
            "launch-error",
        )

    def test_success_without_output_is_runner_failure(self) -> None:
        self.manifest()
        run, codex = self.prepare_launch()
        _, _, command_runner = self.fake_codex(run, returncode=0, output=None)

        result = runner.launch(
            run, codex, "model", "high", command_runner=command_runner
        )

        self.assertEqual(result, 2)
        metadata = json.loads((run.root / "run.json").read_text())
        self.assertEqual(metadata["status"], "missing-output")
        self.assertEqual(metadata["exit_code"], 2)

    def test_success_rejects_non_file_output(self) -> None:
        self.manifest()
        make_tools(self.rules_root)
        codex = executable(self.root / "codex")
        for kind in ("directory", "symlink"):
            with self.subTest(kind):
                run = self.prepare()

                def fake_codex(
                    command: list[str],
                    *,
                    output_kind: str = kind,
                    active_run: runner.Run = run,
                    **unused: object,
                ) -> subprocess.CompletedProcess[bytes]:
                    output = active_run.workdir / "output.md"
                    if output_kind == "directory":
                        output.mkdir()
                    else:
                        write(self.root / "outside.md", "outside")
                        output.symlink_to(self.root / "outside.md")
                    return subprocess.CompletedProcess(command, 0)

                result = runner.launch(
                    run,
                    codex,
                    "model",
                    "high",
                    command_runner=fake_codex,
                )

                self.assertEqual(result, 2)
                self.assertFalse((run.root / "output.md").exists())
                metadata = json.loads((run.root / "run.json").read_text())
                self.assertEqual(metadata["status"], "invalid-output")
                self.assertEqual(metadata["exit_code"], 2)
                self.assertEqual(
                    metadata["output_preservation_error"],
                    "output.md is not a regular file",
                )

    def test_launch_without_rules_preserves_the_ambient_path(self) -> None:
        self.manifest()
        codex = executable(self.root / "codex")
        run = self.prepare(install_rules=False)
        _, environments, command_runner = self.fake_codex(
            run, returncode=0, output="complete"
        )

        result = runner.launch(
            run, codex, "model", "high", command_runner=command_runner
        )

        self.assertEqual(result, 0)
        self.assertEqual((run.root / "output.md").read_text(), "complete")
        self.assertEqual(environments[0].get("PATH"), os.environ.get("PATH"))
        self.assertFalse((run.root / "bin").exists())
        self.assertFalse((run.codex_home / "rules/default.rules").exists())
        metadata = json.loads((run.root / "run.json").read_text())
        self.assertEqual(metadata["status"], "complete")
        self.assertEqual(metadata["exit_code"], 0)
        self.assertEqual(metadata["executables"], {"codex": str(codex.resolve())})
