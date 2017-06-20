#!/usr/bin/env python3
#
# Copyright 2004-present Facebook, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
import os
import re
import signal
import subprocess
import unittest


class FatalTests(unittest.TestCase):
    def setUp(self):
        fatal_helper_env = os.environ.get('FOLLY_FATAL_HELPER')
        if fatal_helper_env:
            self.helper = fatal_helper_env
        else:
            build_dir = os.path.join(os.getcwd(), 'buck-out', 'gen')
            self.helper = os.path.join(build_dir, 'folly', 'experimental',
                                       'logging', 'test', 'fatal_helper')

    def run_helper(self, *args):
        '''
        Run the helper.
        Check that it crashes with SIGABRT and prints nothing on stdout.
        Returns the data printed to stderr.
        '''
        cmd = [self.helper]
        cmd.extend(args)
        p = subprocess.Popen(cmd,
                             stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE)
        out, err = p.communicate()
        status = p.returncode

        self.assertEqual(status, -signal.SIGABRT)
        self.assertEqual(out, b'')
        return err

    def glog_crash_regex(self):
        return re.compile(
            br'^C[0-9]{4} .* FatalHelper.cpp:[0-9]+\] test program crashing!$',
            re.MULTILINE)

    def test_async(self):
        err = self.run_helper('--handler_style=async')
        self.assertRegex(err, self.glog_crash_regex())

    def test_immediate(self):
        err = self.run_helper('--handler_style=immediate')
        self.assertRegex(err, self.glog_crash_regex())

    def test_none(self):
        # The fatal message should be printed directly to stderr when there
        # are no logging handlers configured.
        err = self.run_helper('--handler_style=none')
        return re.compile(
            br'^FATAL:.*/FatalHelper.cpp:[0-9]+: test program crashing!$',
            re.MULTILINE)
        self.assertRegex(err, self.glog_crash_regex())

    def test_other_category(self):
        err = self.run_helper('--category=foo.bar',
                              '--logging', '.=FATAL')
        regex = re.compile(
            br'^C[0-9]{4} .* FatalHelper.cpp:[0-9]+\] '
            br'crashing to category foo\.bar$',
            re.MULTILINE)
        self.assertRegex(err, regex)
