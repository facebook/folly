# Copyright (c) Facebook, Inc. and its affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from __future__ import absolute_import, division, print_function, unicode_literals

import os
import subprocess

from .envfuncs import Env
from .platform import is_windows


try:
    from shlex import quote as shellquote
except ImportError:
    from pipes import quote as shellquote


class RunCommandError(Exception):
    pass


def _print_env_diff(env):
    current_keys = set(os.environ.keys())
    wanted_env = set(env.keys())

    unset_keys = current_keys.difference(wanted_env)
    for k in sorted(unset_keys):
        print("+ unset %s" % k)

    added_keys = wanted_env.difference(current_keys)
    for k in wanted_env.intersection(current_keys):
        if os.environ[k] != env[k]:
            added_keys.add(k)

    for k in sorted(added_keys):
        if ("PATH" in k) and (os.pathsep in env[k]):
            print("+ %s=\\" % k)
            for elem in env[k].split(os.pathsep):
                print("+      %s%s\\" % (shellquote(elem), os.pathsep))
        else:
            print("+ %s=%s \\" % (k, shellquote(env[k])))


def run_cmd(cmd, env=None, cwd=None, allow_fail=False):
    print("---")
    try:
        cmd_str = " \\\n+      ".join(shellquote(arg) for arg in cmd)
    except TypeError:
        # eg: one of the elements is None
        raise RunCommandError("problem quoting cmd: %r" % cmd)

    if env:
        assert isinstance(env, Env)
        _print_env_diff(env)

        # Convert from our Env type to a regular dict.
        # This is needed because python3 looks up b'PATH' and 'PATH'
        # and emits an error if both are present.  In our Env type
        # we'll return the same value for both requests, but we don't
        # have duplicate potentially conflicting values which is the
        # spirit of the check.
        env = dict(env.items())

    if cwd:
        print("+ cd %s && \\" % shellquote(cwd))
        # Our long path escape sequence may confuse cmd.exe, so if the cwd
        # is short enough, strip that off.
        if is_windows() and (len(cwd) < 250) and cwd.startswith("\\\\?\\"):
            cwd = cwd[4:]

    print("+ %s" % cmd_str)

    if allow_fail:
        return subprocess.call(cmd, env=env, cwd=cwd)

    try:
        return subprocess.check_call(cmd, env=env, cwd=cwd)
    except (TypeError, ValueError, OSError) as exc:
        raise RunCommandError(
            "%s while running `%s` with env=%r\nos.environ=%r"
            % (str(exc), cmd_str, env, os.environ)
        )
