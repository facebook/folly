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

"""Launch agent CLIs without ambient user or repository configuration.

`Agent` owns the shared workspace and environment lifecycle. `AgentImpl`
supplies the engine-specific isolation boundary, command, and session parsing.
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO, Callable, Mapping, Protocol


CommandRunner = Callable[..., subprocess.CompletedProcess[bytes]]
RESERVED_ENVIRONMENT_PREFIXES = (
    "CLAUDE_",
    "CODEX_",
    "GIT_",
    "PYTHON",
)
RESERVED_ENVIRONMENT_NAMES = {
    "BASH_ENV",
    "CRITIC_ITERATE_RULES_DIR",
    "ENV",
    "HOME",
    "TMPDIR",
    "W",
    "XDG_CACHE_HOME",
    "XDG_CONFIG_HOME",
    "XDG_DATA_HOME",
    "ZDOTDIR",
}
INHERITED_ENVIRONMENT_PREFIXES = (*RESERVED_ENVIRONMENT_PREFIXES, "FOLLY_BACKTEST_")


class IsolationError(ValueError):
    pass


@dataclass(frozen=True)
class Workspace:
    """Private launch layout with engine state outside the writable workspace."""

    root: Path
    workspace_root: Path
    task: Path
    home: Path
    temporary: Path


@dataclass(frozen=True)
class Request:
    """One turn, optionally resuming a session or capturing its chat response.

    `resume_session_id` is extracted from a prior turn's event trace.
    `response_path` is separate from artifacts an agent writes in the task.
    """

    model: str | None
    effort: str
    access: str = "default"
    resume_session_id: str | None = None
    ephemeral: bool = False
    response_path: Path | None = None


class AgentImpl(Protocol):
    """Engine-specific operations behind `Agent`'s isolation lifecycle."""

    name: str
    executable_name: str

    def initialize(
        self,
        workspace: Workspace,
        environment: Mapping[str, str],
        command_runner: CommandRunner,
    ) -> None: ...

    def validate(self, workspace: Workspace) -> None: ...

    def command(
        self, workspace: Workspace, executable: Path, request: Request
    ) -> list[str]: ...

    def configure_environment(
        self,
        environment: dict[str, str],
        workspace: Workspace,
        request: Request | None,
    ) -> None: ...

    def extract_session_id(self, trace: Path) -> str | None: ...


class CodexImpl:
    name = "codex"
    executable_name = "codex"

    def initialize(
        self,
        workspace: Workspace,
        environment: Mapping[str, str],
        command_runner: CommandRunner,
    ) -> None:
        # Codex discovers project configuration from its Git root through its
        # CWD. Other CLIs need their own boundary; Claude has --safe-mode.
        result = command_runner(
            ["git", "init", "--quiet", str(workspace.workspace_root)],
            check=False,
            env=environment,
            stderr=subprocess.PIPE,
            stdout=subprocess.PIPE,
        )
        if result.returncode:
            error = result.stderr.decode(errors="replace").strip()
            raise IsolationError(
                "could not create private Codex Git root"
                + (f": {error}" if error else "")
            )

    def command(
        self, workspace: Workspace, executable: Path, request: Request
    ) -> list[str]:
        command = [str(executable), "-a", "never"]
        if request.access != "default":
            command.extend(("-s", request.access))
        command.extend(("-C", str(workspace.workspace_root), "exec"))
        if request.resume_session_id is not None:
            command.append("resume")
        command.extend(("--ignore-user-config", "--json"))
        if request.ephemeral:
            command.append("--ephemeral")
        if request.model is not None:
            command.extend(("--model", request.model))
        command.extend(
            ("--config", f"model_reasoning_effort={json.dumps(request.effort)}")
        )
        if request.response_path is not None:
            command.extend(("--output-last-message", str(request.response_path)))
        if request.resume_session_id is not None:
            command.append(request.resume_session_id)
        command.append("-")
        return command

    def validate(self, workspace: Workspace) -> None:
        git_directory = workspace.workspace_root / ".git"
        if git_directory.is_symlink() or not git_directory.is_dir():
            raise IsolationError("isolated Codex workspace has no Git boundary")

    def configure_environment(
        self,
        environment: dict[str, str],
        workspace: Workspace,
        request: Request | None,
    ) -> None:
        environment["CODEX_HOME"] = str(workspace.home)
        environment["GIT_CONFIG_GLOBAL"] = os.devnull
        environment["GIT_CONFIG_NOSYSTEM"] = "1"
        if request is not None and request.model is not None:
            environment["CODEX_REVIEW_MODEL"] = request.model

    def extract_session_id(self, trace: Path) -> str | None:
        session_id = None
        for line in trace.read_text(encoding="utf-8").splitlines():
            try:
                event = json.loads(line)
            except json.JSONDecodeError:
                continue
            if type(event) is not dict:
                continue
            if event.get("type") == "thread.started":
                session_id = event.get("thread_id")
        return session_id if type(session_id) is str else None


def _is_reserved_environment(name: str) -> bool:
    return name in RESERVED_ENVIRONMENT_NAMES or name.startswith(
        RESERVED_ENVIRONMENT_PREFIXES
    )


def _drop_inherited_environment(name: str) -> bool:
    return name in RESERVED_ENVIRONMENT_NAMES or name.startswith(
        INHERITED_ENVIRONMENT_PREFIXES
    )


def _base_environment(
    additions: Mapping[str, str] | None = None,
) -> dict[str, str]:
    result = {
        name: value
        for name, value in os.environ.items()
        if not _drop_inherited_environment(name)
    }
    if additions:
        reserved = sorted(name for name in additions if _is_reserved_environment(name))
        if reserved:
            raise IsolationError(
                "reserved environment override: " + ", ".join(reserved)
            )
        result.update(additions)
    return result


def _workspace(root: Path) -> Workspace:
    root = root.resolve()
    return Workspace(
        root=root,
        workspace_root=root / "workspace",
        task=root / "workspace" / "task",
        home=root / "agent-home",
        temporary=root / "workspace" / "tmp",
    )


def _require_directory(path: Path, parent: Path) -> None:
    if path.is_symlink() or not path.is_dir():
        raise IsolationError(f"isolated workspace is incomplete: {path}")
    try:
        path.resolve().relative_to(parent.resolve())
    except ValueError as error:
        raise IsolationError(f"isolated workspace escapes {parent}: {path}") from error


@dataclass(frozen=True)
class Agent:
    """An engine-bound launcher with a shared isolation and workspace contract."""

    impl: AgentImpl

    @property
    def name(self) -> str:
        return self.impl.name

    def environment(
        self,
        workspace: Workspace,
        request: Request | None = None,
        *,
        additions: Mapping[str, str] | None = None,
    ) -> dict[str, str]:
        result = _base_environment(additions)
        result["HOME"] = str(workspace.home)
        result["TMPDIR"] = str(workspace.temporary)
        result["W"] = str(workspace.task)
        # Inherited XDG roots are removed above, so their standard fallbacks
        # remain under the private HOME without separate directories or state.
        self.impl.configure_environment(result, workspace, request)
        return result

    def prepare(
        self,
        root: Path,
        *,
        initialize_engine: bool = True,
        command_runner: CommandRunner = subprocess.run,
    ) -> Workspace:
        result = _workspace(root)
        result.root.mkdir(mode=0o700, parents=True, exist_ok=True)
        for path in (result.workspace_root, result.home):
            path.mkdir(mode=0o700)
        for path in (result.task, result.temporary):
            path.mkdir(mode=0o700, parents=True)
        if initialize_engine:
            self.initialize(result, command_runner=command_runner)
        return result

    def initialize(
        self,
        workspace: Workspace,
        *,
        command_runner: CommandRunner = subprocess.run,
    ) -> None:
        self.impl.initialize(
            workspace,
            self.environment(workspace),
            command_runner,
        )
        self.impl.validate(workspace)

    def load(self, root: Path) -> Workspace:
        result = _workspace(root)
        for path, parent in (
            (result.workspace_root, result.root),
            (result.task, result.workspace_root),
            (result.temporary, result.workspace_root),
            (result.home, result.root),
        ):
            _require_directory(path, parent)
        self.impl.validate(result)
        return result

    def resolve_executable(self) -> Path:
        executable = shutil.which(self.impl.executable_name)
        if executable is None:
            raise IsolationError(f"{self.impl.executable_name} is not executable")
        return Path(executable).resolve()

    def run(
        self,
        workspace: Workspace,
        request: Request,
        *,
        stdin: BinaryIO,
        stdout: BinaryIO,
        stderr: BinaryIO,
        additions: Mapping[str, str] | None = None,
        executable: Path | None = None,
        command_runner: CommandRunner = subprocess.run,
    ) -> subprocess.CompletedProcess[bytes]:
        executable = executable or self.resolve_executable()
        return command_runner(
            self.impl.command(workspace, executable, request),
            check=False,
            # Task-local rules are below this CWD, outside Codex's ancestor scan.
            cwd=workspace.workspace_root,
            env=self.environment(workspace, request, additions=additions),
            stderr=stderr,
            stdin=stdin,
            stdout=stdout,
        )

    def extract_session_id(self, trace: Path) -> str | None:
        return self.impl.extract_session_id(trace)


CODEX = Agent(CodexImpl())
