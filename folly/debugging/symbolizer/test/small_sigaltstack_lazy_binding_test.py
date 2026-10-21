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

import os
import re
import resource
import signal
import subprocess
import unittest

BINDING = re.compile(r"binding file .* normal symbol `([^']+)'")


class SmallSigAltStackLazyBindingTest(unittest.TestCase):
    def test_stack_trace_does_not_lazily_bind_symbols(self) -> None:
        helper = os.environ["SMALL_SIGALTSTACK_CRASH"]
        env = {k: v for k, v in os.environ.items() if k != "LD_BIND_NOW"}
        env["LD_DEBUG"] = "bindings"
        p = subprocess.run(
            [helper],
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0)),
        )
        err = p.stderr.decode("utf-8", errors="replace")
        self.assertEqual(p.returncode, -signal.SIGILL, err)

        _, header, trace = err.partition("*** Signal 4 (SIGILL)")
        self.assertTrue(header, err)

        bound = [m.group(1) for m in BINDING.finditer(trace.partition("\n")[2])]
        self.assertEqual(bound, [])
