# Copyright (c) Facebook, Inc. and its affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from __future__ import absolute_import, division, print_function, unicode_literals

import os
import select
import subprocess
import sys

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


def run_cmd(cmd, env=None, cwd=None, allow_fail=False, log_file=None):
    if log_file is not None:
        with open(log_file, "a") as log:

            def log_function(msg):
                log.write(msg)
                sys.stdout.write(msg)

            _run_cmd(cmd, env=env, cwd=cwd, allow_fail=allow_fail, log_fn=log_function)
    else:
        _run_cmd(cmd, env=env, cwd=cwd, allow_fail=allow_fail, log_fn=sys.stdout.write)


def _run_cmd(cmd, env, cwd, allow_fail, log_fn):
    log_fn("---\n")
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
        log_fn("+ cd %s && \\\n" % shellquote(cwd))
        # Our long path escape sequence may confuse cmd.exe, so if the cwd
        # is short enough, strip that off.
        if is_windows() and (len(cwd) < 250) and cwd.startswith("\\\\?\\"):
            cwd = cwd[4:]

    log_fn("+ %s\n" % cmd_str)

    try:
        p = subprocess.Popen(
            cmd, env=env, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT
        )
    except (TypeError, ValueError, OSError) as exc:
        log_fn("error running `%s`: %s" % (cmd_str, exc))
        raise RunCommandError(
            "%s while running `%s` with env=%r\nos.environ=%r"
            % (str(exc), cmd_str, env, os.environ)
        )

    _pipe_output(p, log_fn)

    p.wait()
    if p.returncode != 0 and not allow_fail:
        raise subprocess.CalledProcessError(p.returncode, cmd)

    return p.returncode


if hasattr(select, "poll"):

    def _pipe_output(p, log_fn):
        """Read output from p.stdout and call log_fn() with each chunk of data as it
        becomes available."""
        # Perform non-blocking reads
        import fcntl

        fcntl.fcntl(p.stdout.fileno(), fcntl.F_SETFL, os.O_NONBLOCK)
        poll = select.poll()
        poll.register(p.stdout.fileno(), select.POLLIN)

        buffer_size = 4096
        while True:
            poll.poll()
            data = p.stdout.read(buffer_size)
            if not data:
                break
            # log_fn() accepts arguments as str (binary in Python 2, unicode in
            # Python 3).  In Python 3 the subprocess output will be plain bytes,
            # and need to be decoded.
            if not isinstance(data, str):
                data = data.decode("utf-8", errors="surrogateescape")
            log_fn(data)


else:

    def _pipe_output(p, log_fn):
        """Read output from p.stdout and call log_fn() with each chunk of data as it
        becomes available."""
        # Perform blocking reads.  Use a smaller buffer size to avoid blocking
        # for very long when data is available.
        buffer_size = 64
        while True:
            data = p.stdout.read(buffer_size)
            if not data:
                break
            # log_fn() accepts arguments as str (binary in Python 2, unicode in
            # Python 3).  In Python 3 the subprocess output will be plain bytes,
            # and need to be decoded.
            if not isinstance(data, str):
                data = data.decode("utf-8", errors="surrogateescape")
            log_fn(data)
