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
import signal
import subprocess
import unittest


class ExceptionTracerUncaughtTest(unittest.TestCase):
    def setUp(self):
        self.helper = os.environ.get("EXCEPTION_TRACER_TEST_BIN")
        self.assertIsNotNone(
            self.helper, "EXCEPTION_TRACER_TEST_BIN env var must be set"
        )

    def test_uncaught_exception(self):
        p = subprocess.Popen(
            [self.helper], stdout=subprocess.PIPE, stderr=subprocess.PIPE
        )
        out, err = p.communicate()
        err_str = err.decode("utf-8", errors="replace")

        # Process should be killed by SIGABRT (terminate called for uncaught exception)
        self.assertEqual(
            p.returncode,
            -signal.SIGABRT,
            f"Expected SIGABRT (-{signal.SIGABRT}), got {p.returncode}.\nstderr:\n{err_str}",
        )

        # Verify exception tracer output
        self.assertIn("Exception type: std::runtime_error", err_str)
        self.assertIn("foo()", err_str)
        self.assertIn("main", err_str)
