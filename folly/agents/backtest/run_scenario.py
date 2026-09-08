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

"""Run one fixed agent-rules backtest scenario."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import stat
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from datetime import date
from pathlib import Path, PurePosixPath
from typing import Any, Callable, Iterable, Sequence


CommandRunner = Callable[..., subprocess.CompletedProcess[bytes]]

CRITIC_ITERATE_RULE = PurePosixPath("critic-iterate.md")
CRITIC_ITERATE_SUPPORT_FILES = (
    PurePosixPath("critic-iterate/cold-review-preamble.md"),
    PurePosixPath("critic-iterate/fresh-review-preamble.md"),
    PurePosixPath("critic-iterate/auth-prompt.md"),
)
TOOL_FILES = {
    "codex-reviewer.py": PurePosixPath("critic-iterate/codex-reviewer.py"),
    "reformat-md": PurePosixPath("scripts/reformat-md"),
    "session_current_model_id.py": PurePosixPath(
        "critic-iterate/session_current_model_id.py"
    ),
}
RESERVED_INPUT_NAMES = {"AGENTS.md", "AGENTS.override.md"}
RULE_LOADING_INSTRUCTION = (
    "Read every rule listed in `rules/rules-inventory.md`, in order. Follow "
    "those rules for conditional loads; do not look for ambient rule files.\n\n"
)


class RunnerError(ValueError):
    pass


@dataclass(frozen=True)
class Mapping:
    source: PurePosixPath
    destination: PurePosixPath


@dataclass(frozen=True)
class Manifest:
    prompt: PurePosixPath
    inputs: tuple[Mapping, ...]
    rules: tuple[PurePosixPath, ...]


@dataclass(frozen=True)
class Run:
    root: Path
    workdir: Path
    codex_home: Path
    prompt: Path
    rules_root: Path


@dataclass(frozen=True)
class Checkout:
    root: Path
    command: str


def _relative_path(value: Any, field: str) -> PurePosixPath:
    assert type(value) is str
    assert value
    path = PurePosixPath(value)
    if not path.parts or path.is_absolute() or ".." in path.parts:
        raise RunnerError(f"{field} must be a relative path without '..': {value}")
    return path


def _is_development_doc(path: PurePosixPath) -> bool:
    return path.name in {"README.md", "CONTRIB.md"} or path.name.endswith(
        (".contrib.md", ".entrypoint.md")
    )


def _validate_input_destination(path: PurePosixPath) -> None:
    if path.parts[0] == "rules":
        raise RunnerError("input destinations may not use the reserved rules/")
    if path.name in RESERVED_INPUT_NAMES:
        raise RunnerError(f"input destination would be loaded as hidden policy: {path}")


def load_manifest(path: Path) -> Manifest:
    raw = json.loads(path.read_text())
    assert type(raw) is dict
    assert set(raw) == {"prompt", "inputs", "rules"}
    prompt = _relative_path(raw["prompt"], "prompt")
    raw_inputs = raw["inputs"]
    raw_rules = raw["rules"]
    assert type(raw_inputs) is list
    assert type(raw_rules) is list

    inputs = []
    for index, raw_input in enumerate(raw_inputs):
        assert type(raw_input) is dict
        assert set(raw_input) == {"source", "destination"}
        inputs.append(
            Mapping(
                _relative_path(raw_input["source"], f"inputs[{index}].source"),
                _relative_path(
                    raw_input["destination"], f"inputs[{index}].destination"
                ),
            )
        )
        _validate_input_destination(inputs[-1].destination)

    rules = tuple(
        _relative_path(rule, f"rules[{index}]") for index, rule in enumerate(raw_rules)
    )
    for rule in rules:
        if _is_development_doc(rule):
            raise RunnerError(f"rules may not include development material: {rule}")
    return Manifest(prompt, tuple(inputs), rules)


def _resolve_below(root: Path, relative: PurePosixPath, field: str) -> Path:
    root = root.resolve()
    path = (root / relative).resolve()
    try:
        path.relative_to(root)
    except ValueError as error:
        raise RunnerError(f"{field} resolves outside {root}: {relative}") from error
    return path


def _normalize_cpp_snapshot(path: PurePosixPath) -> PurePosixPath:
    if path.name.endswith((".h.txt", ".cpp.txt")):
        return path.with_name(path.name[:-4])
    return path


def _mapped_files(
    scenario: Path, mapping: Mapping
) -> Iterable[tuple[PurePosixPath, bytes]]:
    source = _resolve_below(scenario, mapping.source, "input source")
    if source.is_file():
        yield _normalize_cpp_snapshot(mapping.destination), source.read_bytes()
        return
    if not source.is_dir():
        raise RunnerError(f"input does not exist: {mapping.source}")
    for path in sorted(source.rglob("*")):
        if path.is_symlink():
            raise RunnerError(f"input trees may not contain symlinks: {path}")
        if not path.is_file():
            continue
        relative = _normalize_cpp_snapshot(
            PurePosixPath(path.relative_to(source).as_posix())
        )
        yield mapping.destination / relative, path.read_bytes()


def _find_checkout(path: Path) -> Checkout:
    start = path if path.is_dir() else path.parent
    for directory in (start, *start.parents):
        if (directory / ".hg").exists():
            return Checkout(directory, "sl")
        if (directory / ".git").exists():
            return Checkout(directory, "git")
    raise RunnerError(f"generation files are not in a source checkout: {path}")


def _run_source_control(
    checkout: Checkout,
    arguments: Sequence[str],
    command_runner: CommandRunner,
) -> str:
    result = command_runner(
        [checkout.command, *arguments],
        cwd=checkout.root,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode:
        error = result.stderr.decode(errors="replace").strip()
        raise RunnerError(
            f"{checkout.command} {' '.join(arguments)} failed"
            + (f": {error}" if error else "")
        )
    return result.stdout.decode(errors="replace").strip()


def _generation_sources(
    scenario: Path,
    manifest: Manifest,
    rules_root: Path,
    runner_path: Path,
) -> tuple[Path, ...]:
    sources = [
        runner_path.resolve(),
        (scenario / "scenario.json").resolve(),
        _resolve_below(scenario, manifest.prompt, "prompt"),
    ]
    sources.extend(
        _resolve_below(scenario, mapping.source, "input source")
        for mapping in manifest.inputs
    )
    support_files = (
        CRITIC_ITERATE_SUPPORT_FILES if CRITIC_ITERATE_RULE in manifest.rules else ()
    )
    sources.extend(
        _resolve_below(rules_root, path, "rule")
        for path in (*manifest.rules, *support_files, *TOOL_FILES.values())
    )
    return tuple(dict.fromkeys(sources))


def generation_revision(
    scenario: Path,
    manifest: Manifest,
    rules_root: Path,
    *,
    runner_path: Path | None = None,
    command_runner: CommandRunner = subprocess.run,
) -> str:
    if runner_path is None:
        runner_path = Path(__file__)
    sources = _generation_sources(scenario, manifest, rules_root, runner_path)
    checkout = _find_checkout(sources[0])
    relative_sources = []
    for source in sources:
        source_checkout = _find_checkout(source)
        if source_checkout != checkout:
            raise RunnerError("generation files must come from one source checkout")
        relative_sources.append(str(source.relative_to(checkout.root)))

    if checkout.command == "sl":
        status_arguments = [
            "status",
            "-mardui",
            "--root-relative",
            "--",
            *relative_sources,
        ]
        revision_arguments = ["log", "-r", ".", "-T", "{node}\\n"]
    else:
        status_arguments = [
            "status",
            "--porcelain=v1",
            "--untracked-files=all",
            "--ignored=matching",
            "--",
            *relative_sources,
        ]
        revision_arguments = ["rev-parse", "HEAD"]

    status = _run_source_control(checkout, status_arguments, command_runner)
    if status:
        raise RunnerError(
            "generation files contain local changes or untracked/ignored files; "
            "refusing to run.\nCommit or amend source files, remove ignored "
            "artifacts, or ask the user to authorize a commit containing only "
            "the listed source files, then retry:\n" + status
        )
    revision = _run_source_control(checkout, revision_arguments, command_runner)
    if not revision:
        raise RunnerError("source control returned an empty generation revision")
    return revision


def _claim(destinations: set[PurePosixPath], destination: PurePosixPath) -> None:
    if destination in destinations:
        raise RunnerError(f"two files map to {destination}")
    if any(parent in destinations for parent in destination.parents) or any(
        destination in existing.parents for existing in destinations
    ):
        raise RunnerError(f"file/directory destination collision at {destination}")
    destinations.add(destination)


def _rules_inventory(rules: tuple[PurePosixPath, ...]) -> bytes:
    listed_rules = "\n".join(
        f"{index}. `{rule.as_posix()}`" for index, rule in enumerate(rules, 1)
    )
    if not listed_rules:
        listed_rules = "No rule files were selected."
    return (
        "# Rules inventory\n\n"
        "Read these files in order, resolving each path relative to this file:\n\n"
        f"{listed_rules}\n\n"
        "When `critic-iterate.md` is selected, its reviewer support files are "
        "also staged. Read them only when that rule directs you to.\n"
    ).encode()


def stage(scenario: Path, manifest: Manifest, rules_root: Path, workdir: Path) -> None:
    rules_root = rules_root.resolve()
    if not rules_root.is_dir():
        raise RunnerError(f"rules root is not a directory: {rules_root}")
    plan: list[tuple[PurePosixPath, bytes]] = []
    destinations: set[PurePosixPath] = set()
    for mapping in manifest.inputs:
        for destination, contents in _mapped_files(scenario, mapping):
            _validate_input_destination(destination)
            _claim(destinations, destination)
            plan.append((destination, contents))

    inventory = PurePosixPath("rules/rules-inventory.md")
    _claim(destinations, inventory)
    plan.append((inventory, _rules_inventory(manifest.rules)))
    support_files = (
        CRITIC_ITERATE_SUPPORT_FILES if CRITIC_ITERATE_RULE in manifest.rules else ()
    )
    for rule in (*manifest.rules, *support_files):
        destination = PurePosixPath("rules") / rule
        _claim(destinations, destination)
        source = _resolve_below(rules_root, rule, "rule")
        if not source.is_file():
            raise RunnerError(f"rule file does not exist: {rule}")
        plan.append((destination, source.read_bytes()))

    for destination, contents in plan:
        path = workdir / destination
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(contents)
        path.chmod(0o644 if destination == PurePosixPath("output.md") else 0o444)


def prepare(
    scenario: Path,
    rules_root: Path,
    run_root: Path,
    model: str,
    effort: str,
    generation_revision: str,
) -> Run:
    scenario = scenario.resolve()
    if not scenario.is_dir():
        raise RunnerError(f"scenario is not a directory: {scenario}")
    rules_root = rules_root.resolve()
    run_root = run_root.resolve()
    if run_root.is_relative_to(scenario):
        raise RunnerError("run root may not be inside the scenario")
    if run_root.is_relative_to(rules_root):
        raise RunnerError("run root may not be inside the rules tree")
    manifest_path = scenario / "scenario.json"
    manifest = load_manifest(manifest_path)
    prompt = _resolve_below(scenario, manifest.prompt, "prompt")
    if not prompt.is_file():
        raise RunnerError(f"prompt does not exist: {manifest.prompt}")

    run_root.mkdir(parents=True, exist_ok=True)
    root = Path(
        tempfile.mkdtemp(prefix=f"{date.today():%Y%m%d}-{scenario.name}-", dir=run_root)
    )
    try:
        run = Run(
            root,
            root / "workdir",
            root / "codex-home",
            root / "author-prompt.md",
            rules_root,
        )
        run.workdir.mkdir()
        run.prompt.write_text(RULE_LOADING_INSTRUCTION + prompt.read_text())
        run.prompt.chmod(0o444)
        shutil.copyfile(manifest_path, root / "scenario.json")
        stage(scenario, manifest, run.rules_root, run.workdir)
        (root / "run.json").write_text(
            json.dumps(
                {
                    "model": model,
                    "reasoning_effort": effort,
                    "generation_revision": generation_revision,
                    "rules": [rule.as_posix() for rule in manifest.rules],
                    "scenario": str(scenario),
                    "status": "prepared",
                },
                indent=2,
                sort_keys=True,
            )
            + "\n"
        )
    except (OSError, RunnerError):
        shutil.rmtree(root, ignore_errors=True)
        raise
    return run


def _resolve_executable(value: str | Path, name: str) -> Path:
    resolved = shutil.which(os.fspath(value))
    if resolved is None:
        raise RunnerError(f"{name} is not executable: {value}")
    return Path(resolved).resolve()


def _install_tools(run: Run) -> tuple[Path, dict[str, Path]]:
    tool_bin = run.root / "bin"
    tool_bin.mkdir()
    lines = []
    tools = {
        name: _resolve_executable(
            _resolve_below(run.rules_root, relative, f"{name} tool"), name
        )
        for name, relative in TOOL_FILES.items()
    }
    for name, executable in tools.items():
        shim = tool_bin / name
        shim.symlink_to(executable)
        lines.extend(
            [
                f"host_executable(name={json.dumps(name)}, "
                f"paths={json.dumps([str(shim), str(executable)])})",
                f'prefix_rule(pattern=[{json.dumps(name)}], decision="allow", '
                'justification="Tool required by the staged rules.")',
            ]
        )
    policy = run.codex_home / "rules" / "default.rules"
    policy.parent.mkdir(parents=True)
    policy.write_text("\n".join(lines) + "\n")
    return tool_bin, tools


def _update_metadata(run: Run, **updates: object) -> None:
    path = run.root / "run.json"
    metadata = json.loads(path.read_text())
    metadata.update(updates)
    path.write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n")


def _preserve_output(run: Run) -> bool:
    output = run.workdir / "output.md"
    try:
        output_stat = output.lstat()
    except FileNotFoundError:
        return False
    if not stat.S_ISREG(output_stat.st_mode):
        raise RunnerError("output.md is not a regular file")
    shutil.copyfile(output, run.root / "output.md")
    return output_stat.st_size > 0


def _author_environment(run: Run, tool_bin: Path) -> dict[str, str]:
    environment = os.environ.copy()
    for name in tuple(environment):
        if name.startswith(("CODEX_", "PYTHON")) or name in {
            "BASH_ENV",
            "CRITIC_ITERATE_RULES_DIR",
            "ENV",
            "ZDOTDIR",
        }:
            environment.pop(name)
    inherited_path = environment.get("PATH")
    environment["CODEX_HOME"] = str(run.codex_home)
    environment["PATH"] = (
        os.pathsep.join([str(tool_bin), inherited_path])
        if inherited_path
        else str(tool_bin)
    )
    return environment


def _author_command(run: Run, codex: Path, model: str, effort: str) -> list[str]:
    return [
        str(codex),
        "-a",
        "never",
        "exec",
        "--skip-git-repo-check",
        "--json",
        "--model",
        model,
        "--config",
        f"model_reasoning_effort={json.dumps(effort)}",
        "--cd",
        str(run.workdir),
        "-",
    ]


def _finish_run(run: Run, returncode: int) -> int:
    if returncode:
        _update_metadata(run, status="failed", exit_code=returncode)
        try:
            _preserve_output(run)
        except (OSError, RunnerError) as error:
            _update_metadata(run, output_preservation_error=str(error))
        return returncode
    try:
        has_output = _preserve_output(run)
    except (OSError, RunnerError) as error:
        _update_metadata(
            run,
            status="invalid-output",
            exit_code=2,
            output_preservation_error=str(error),
        )
        return 2
    if not has_output:
        _update_metadata(run, status="missing-output", exit_code=2)
        return 2
    _update_metadata(run, status="complete", exit_code=0)
    return 0


def launch(
    run: Run,
    codex: str | Path,
    model: str,
    effort: str,
    command_runner: CommandRunner = subprocess.run,
) -> int:
    try:
        codex_path = _resolve_executable(codex, "codex")
        tool_bin, tools = _install_tools(run)
        executables = {"codex": codex_path, **tools}
        _update_metadata(
            run,
            executables={
                name: str(executable) for name, executable in executables.items()
            },
        )
    except (OSError, RunnerError):
        _update_metadata(run, status="launch-error")
        raise

    environment = _author_environment(run, tool_bin)
    command = _author_command(run, codex_path, model, effort)
    try:
        with (
            run.prompt.open("rb") as stdin,
            (run.root / "trace.jsonl").open("wb") as stdout,
            (run.root / "err.txt").open("wb") as stderr,
        ):
            result = command_runner(
                command,
                stdin=stdin,
                stdout=stdout,
                stderr=stderr,
                env=environment,
                check=False,
            )
    except OSError:
        _update_metadata(run, status="launch-error")
        raise
    return _finish_run(run, result.returncode)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "scenario", type=Path, help="directory containing scenario.json"
    )
    parser.add_argument(
        "--run-root",
        default=Path(tempfile.gettempdir()) / "folly-agents-backtest-runs",
        type=Path,
        help="run root (default: system temporary directory)",
    )
    parser.add_argument("--model", required=True, help="author model")
    parser.add_argument(
        "--reasoning-effort",
        required=True,
        choices=("none", "low", "medium", "high", "xhigh", "max", "ultra"),
        help="author reasoning effort",
    )
    parser.add_argument(
        "--prepare-only",
        action="store_true",
        help="stage inputs without starting Codex",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        agents_root = Path(__file__).resolve().parent.parent
        scenario = args.scenario.resolve()
        manifest = load_manifest(scenario / "scenario.json")
        revision = generation_revision(scenario, manifest, agents_root)
        run = prepare(
            scenario,
            agents_root,
            args.run_root,
            args.model,
            args.reasoning_effort,
            revision,
        )
        print(run.root)
        if args.prepare_only:
            return 0
        return launch(
            run,
            "codex",
            args.model,
            args.reasoning_effort,
        )
    except (RunnerError, json.JSONDecodeError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
