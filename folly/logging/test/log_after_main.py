#!/usr/bin/env python3
# Copyright (c) Facebook, Inc. and its affiliates.
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
import subprocess
import unittest


class LogAfterMain(unittest.TestCase):
    def find_helper(self, name, env_var):
        path = os.environ.get(env_var)
        if path:
            if not os.access(path, os.X_OK):
                raise Exception(
                    "path specified by $%s does not exist: %r" % (env_var, path)
                )
            return path

        helper_subdir = os.path.join("folly", "logging", "test", "helpers")
        buck_build_dir = os.path.join(os.getcwd(), "buck-out", "gen")
        candidate_dirs = (
            os.path.join(buck_build_dir, helper_subdir),
            helper_subdir,
            os.path.join(os.getcwd(), "helpers"),
        )
        for d in candidate_dirs:
            path = os.path.join(d, name)
            if os.access(path, os.X_OK):
                return path
        raise Exception("unable to find helper program %r" % (name,))

    def run_helper(self, cmd):
        return subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            encoding="utf-8",
            errors="surrogateescape",
        )

    def test_log_after_main(self):
        helper = self.find_helper("log_after_main", "FOLLY_LOG_AFTER_MAIN_HELPER")
        proc = self.run_helper([helper])
        self.assertEqual(proc.stdout, "")
        self.assertIn("main running", proc.stderr)
        self.assertEqual(proc.returncode, 0, "stderr: %s" % (proc.stderr,))

    def test_log_after_main_no_init(self):
        helper = self.find_helper(
            "log_after_main_no_init", "FOLLY_LOG_AFTER_MAIN_NO_INIT_HELPER"
        )
        proc = self.run_helper([helper])
        self.assertEqual(proc.stdout, "")
        self.assertEqual(proc.returncode, 0, "stderr: %s" % (proc.stderr,))
