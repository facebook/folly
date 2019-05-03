#!/usr/bin/env python
# Copyright (c) 2019-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

from __future__ import absolute_import, division, print_function, unicode_literals

import unittest

from ..expr import parse_expr


class ExprTest(unittest.TestCase):
    def test_equal(self):
        e = parse_expr("foo=bar")
        self.assertTrue(e.eval({"foo": "bar"}))
        self.assertFalse(e.eval({"foo": "not-bar"}))
        self.assertFalse(e.eval({"not-foo": "bar"}))

    def test_not_equal(self):
        e = parse_expr("not(foo=bar)")
        self.assertFalse(e.eval({"foo": "bar"}))
        self.assertTrue(e.eval({"foo": "not-bar"}))

    def test_bad_not(self):
        with self.assertRaises(Exception):
            parse_expr("foo=not(bar)")

    def test_all(self):
        e = parse_expr("all(foo = bar, baz = qux)")
        self.assertTrue(e.eval({"foo": "bar", "baz": "qux"}))
        self.assertFalse(e.eval({"foo": "bar", "baz": "nope"}))
        self.assertFalse(e.eval({"foo": "nope", "baz": "nope"}))

    def test_any(self):
        e = parse_expr("any(foo = bar, baz = qux)")
        self.assertTrue(e.eval({"foo": "bar", "baz": "qux"}))
        self.assertTrue(e.eval({"foo": "bar", "baz": "nope"}))
        self.assertFalse(e.eval({"foo": "nope", "baz": "nope"}))
