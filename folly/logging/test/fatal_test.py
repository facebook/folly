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
import re
import signal
import subprocess
import sys
import unittest


class FatalTests(unittest.TestCase):
    def setUp(self):
        fatal_helper_env = os.environ.get("FOLLY_FATAL_HELPER")
        if fatal_helper_env:
            self.helper = fatal_helper_env
        else:
            build_dir = os.path.join(os.getcwd(), "buck-out", "gen")
            self.helper = os.path.join(
                build_dir, "folly", "logging", "test", "helpers", "fatal_helper"
            )

    def run_helper(self, *args, **kwargs):
        """Run the helper and verify it crashes.

        Check that it crashes with SIGABRT and prints nothing on stdout.
        Returns the data printed to stderr.
        """
        returncode, out, err = self.run_helper_nochecks(*args, **kwargs)
        self.assertEqual(returncode, -signal.SIGABRT)
        self.assertEqual(out, b"")
        return err

    def run_helper_nochecks(self, *args, **kwargs):
        """Run the helper.

        Returns a tuple of [returncode, stdout_output, stderr_output]
        """
        env = kwargs.pop("env", None)
        if kwargs:
            raise TypeError("unexpected keyword arguments: %r" % (list(kwargs.keys())))

        cmd = [self.helper]
        cmd.extend(args)
        p = subprocess.Popen(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env
        )
        out, err = p.communicate()
        return p.returncode, out, err

    def is_debug_build(self):
        returncode, out, err = self.run_helper_nochecks("--check_debug")
        self.assertEqual(b"", err)
        self.assertEqual(0, returncode)
        if out.strip() == b"DEBUG=1":
            return True
        elif out.strip() == b"DEBUG=0":
            return False
        else:
            self.fail("unexpected output from --check_debug: {}".format(out))

    def get_crash_regex(self, msg=b"test program crashing!", glog=True):
        if glog:
            prefix = br"^F[0-9]{4} .* FatalHelper.cpp:[0-9]+\] "
        else:
            prefix = br"^FATAL:.*FatalHelper.cpp:[0-9]+: "
        regex = prefix + re.escape(msg) + b"$"
        return re.compile(regex, re.MULTILINE)

    def test_no_crash(self):
        # Simple sanity check that the program runs without
        # crashing when requested
        returncode, out, err = self.run_helper_nochecks("--crash=no")
        self.assertEqual(0, returncode)
        self.assertEqual(b"", out)
        self.assertEqual(b"", err)

    def test_async(self):
        handler_setings = "default=stream:stream=stderr,async=true"
        err = self.run_helper("--logging=;" + handler_setings)
        self.assertRegex(err, self.get_crash_regex())

    def test_immediate(self):
        handler_setings = "default=stream:stream=stderr,async=false"
        err = self.run_helper("--logging=;" + handler_setings)
        self.assertRegex(err, self.get_crash_regex())

    def test_none(self):
        # The fatal message should be printed directly to stderr when there
        # are no logging handlers configured.
        err = self.run_helper("--logging=ERR:")
        self.assertRegex(err, self.get_crash_regex(glog=False))

    def test_other_category(self):
        err = self.run_helper("--category=foo.bar", "--logging", ".=FATAL")
        regex = re.compile(
            br"^F[0-9]{4} .* FatalHelper.cpp:[0-9]+\] "
            br"crashing to category foo\.bar$",
            re.MULTILINE,
        )
        self.assertRegex(err, regex)

    def test_static_init(self):
        err = self.run_helper(env={"CRASH_DURING_INIT": "1"})
        regex = self.get_crash_regex(br"crashing during static initialization")
        self.assertRegex(err, regex)

    def test_static_destruction(self):
        err = self.run_helper("--crash=no", env={"CRASH_DURING_INIT": "shutdown"})
        # When crashing during static destruction we may or may not see a
        # glog-formatted message.  This depends on whether the crashing
        # destructor runs before or after the code that uninstalls the log
        # handlers, and it is valid for that to occur in either order.
        regex = re.compile(
            br"^(FATAL|C[0-9]{4}).*FatalHelper.cpp:.* "
            br"crashing during static destruction$",
            re.MULTILINE,
        )
        self.assertRegex(err, regex)

    def test_fatal_xlog_if(self):
        # Specify --crash=no to ensure that the XLOG_IF() check is actually what
        # triggers the crash.
        err = self.run_helper("--fail_fatal_xlog_if", "--crash=no")
        self.assertRegex(err, self.get_crash_regex(b"--fail_fatal_xlog_if specified!"))

    def test_dfatal_xlog_if(self):
        returncode, out, err = self.run_helper_nochecks(
            "--fail_dfatal_xlog_if", "--crash=no"
        )
        # The "--fail_dfatal_xlog_if" message should be logged regardless of which build
        # type we are using.  However, in debug builds it will not trigger a crash.
        self.assertRegex(err, self.get_crash_regex(b"--fail_dfatal_xlog_if specified!"))
        self.assertEqual(b"", out)
        if self.is_debug_build():
            self.assertEqual(-signal.SIGABRT, returncode)
        else:
            self.assertEqual(0, returncode)

    def test_xcheck(self):
        # Specify --crash=no to ensure that the XCHECK() is actually what triggers the
        # crash.
        err = self.run_helper("--fail_xcheck", "--crash=no")
        self.assertRegex(
            err,
            self.get_crash_regex(
                b"Check failed: !FLAGS_fail_xcheck : --fail_xcheck specified!"
            ),
        )

    def test_xcheck_nomsg(self):
        err = self.run_helper("--fail_xcheck_nomsg", "--crash=no")
        self.assertRegex(
            err, self.get_crash_regex(b"Check failed: !FLAGS_fail_xcheck_nomsg ")
        )

    def test_xdcheck(self):
        returncode, out, err = self.run_helper_nochecks("--fail_xdcheck", "--crash=no")
        self.assertEqual(b"", out)
        if self.is_debug_build():
            self.assertRegex(
                err,
                self.get_crash_regex(
                    b"Check failed: !FLAGS_fail_xdcheck : --fail_xdcheck specified!"
                ),
            )
            self.assertEqual(-signal.SIGABRT, returncode)
        else:
            self.assertEqual(b"", err)
            self.assertEqual(0, returncode)

    def test_xcheck_comparisons(self):
        self._test_xcheck_cmp("xcheck_eq0", 1, "==")
        self._test_xcheck_cmp("xcheck_ne0", 0, "!=")
        self._test_xcheck_cmp("xcheck_lt0", 0, "<")
        self._test_xcheck_cmp("xcheck_le0", 9, "<=")
        self._test_xcheck_cmp("xcheck_gt0", 0, ">")
        self._test_xcheck_cmp("xcheck_ge0", -3, ">=")

        self._test_xcheck_cmp("xcheck_eq0", 0, "==", expect_failure=False)
        self._test_xcheck_cmp("xcheck_ne0", 123, "!=", expect_failure=False)
        self._test_xcheck_cmp("xcheck_lt0", -12, "<", expect_failure=False)
        self._test_xcheck_cmp("xcheck_le0", 0, "<=", expect_failure=False)
        self._test_xcheck_cmp("xcheck_gt0", 123, ">", expect_failure=False)
        self._test_xcheck_cmp("xcheck_ge0", 123, ">=", expect_failure=False)

        is_debug = self.is_debug_build()
        self._test_xcheck_cmp("xdcheck_eq0", 1, "==", expect_failure=is_debug)
        self._test_xcheck_cmp("xdcheck_ne0", 0, "!=", expect_failure=is_debug)
        self._test_xcheck_cmp("xdcheck_lt0", 0, "<", expect_failure=is_debug)
        self._test_xcheck_cmp("xdcheck_le0", 9, "<=", expect_failure=is_debug)
        self._test_xcheck_cmp("xdcheck_gt0", 0, ">", expect_failure=is_debug)
        self._test_xcheck_cmp("xdcheck_ge0", -3, ">=", expect_failure=is_debug)

    def test_xcheck_eval_once(self):
        err = self.run_helper("--test_xcheck_eq_evalutates_once")
        expected_msg = "Check failed: ++x == 7 (6 vs. 7)"
        self.assertRegex(err, self.get_crash_regex(expected_msg.encode("utf-8")))

    def test_xcheck_custom_object(self):
        err = self.run_helper("--xcheck_eq_custom_struct")
        if sys.byteorder == "little":
            obj1_hexdump = "01 00 00 00 02 00 00 00"
            obj2_hexdump = "01 00 00 00 ef cd ab 12"
        else:
            obj1_hexdump = "00 00 00 01 00 00 00 02"
            obj2_hexdump = "00 00 00 01 12 ab cd ef"

        expected_msg = (
            "Check failed: MyStruct(1, 2) == m ("
            f"[MyStruct of size 8: {obj1_hexdump}] vs. "
            f"[MyStruct of size 8: {obj2_hexdump}]"
            ")"
        )
        self.assertRegex(err, self.get_crash_regex(expected_msg.encode("utf-8")))

    def _test_xcheck_cmp(
        self, flag, value, op, extra_msg=" extra user args", expect_failure=True
    ):
        args = ["--crash=no", "--" + flag, str(value)]
        if expect_failure:
            err = self.run_helper(*args)
            expected_msg = "Check failed: FLAGS_%s %s 0 (%s vs. 0)%s" % (
                flag,
                op,
                value,
                extra_msg,
            )
            self.assertRegex(err, self.get_crash_regex(expected_msg.encode("utf-8")))
        else:
            returncode, out, err = self.run_helper_nochecks(*args)
            self.assertEqual(b"", err)
            self.assertEqual(b"", out)
            self.assertEqual(0, returncode)
