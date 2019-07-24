#!/usr/bin/env python
# Copyright (c) 2019-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

from __future__ import absolute_import, division, print_function, unicode_literals

import sys
import unittest

import pkg_resources

from ..load import load_all_manifests, patch_loader
from ..manifest import ManifestParser


class ManifestTest(unittest.TestCase):
    def test_missing_section(self):
        with self.assertRaisesRegex(
            Exception, "manifest file test is missing required section manifest"
        ):
            ManifestParser("test", "")

    def test_missing_name(self):
        with self.assertRaisesRegex(
            Exception,
            "manifest file test section 'manifest' is missing required field 'name'",
        ):
            ManifestParser(
                "test",
                """
[manifest]
""",
            )

    def test_minimal(self):
        p = ManifestParser(
            "test",
            """
[manifest]
name = foo
""",
        )
        self.assertEqual(p.name, "foo")
        self.assertEqual(p.fbsource_path, None)

    def test_minimal_with_fbsource_path(self):
        p = ManifestParser(
            "test",
            """
[manifest]
name = foo
fbsource_path = fbcode/wat
""",
        )
        self.assertEqual(p.name, "foo")
        self.assertEqual(p.fbsource_path, "fbcode/wat")

    def test_unknown_field(self):
        with self.assertRaisesRegex(
            Exception,
            (
                "manifest file test section 'manifest' contains "
                "unknown field 'invalid.field'"
            ),
        ):
            ManifestParser(
                "test",
                """
[manifest]
name = foo
invalid.field = woot
""",
            )

    def test_invalid_section_name(self):
        with self.assertRaisesRegex(
            Exception, "manifest file test contains unknown section 'invalid.section'"
        ):
            ManifestParser(
                "test",
                """
[manifest]
name = foo

[invalid.section]
foo = bar
""",
            )

    def test_value_in_dependencies_section(self):
        with self.assertRaisesRegex(
            Exception,
            (
                "manifest file test section 'dependencies' has "
                "'foo = bar' but this section doesn't allow "
                "specifying values for its entries"
            ),
        ):
            ManifestParser(
                "test",
                """
[manifest]
name = foo

[dependencies]
foo = bar
""",
            )

    def test_invalid_conditional_section_name(self):
        with self.assertRaisesRegex(
            Exception,
            (
                "manifest file test section 'dependencies.=' "
                "has invalid conditional: expected "
                "identifier found ="
            ),
        ):
            ManifestParser(
                "test",
                """
[manifest]
name = foo

[dependencies.=]
""",
            )

    def test_section_as_args(self):
        p = ManifestParser(
            "test",
            """
[manifest]
name = foo

[dependencies]
a
b
c

[dependencies.foo=bar]
foo
""",
        )
        self.assertEqual(p.get_section_as_args("dependencies"), ["a", "b", "c"])
        self.assertEqual(
            p.get_section_as_args("dependencies", {"foo": "not-bar"}), ["a", "b", "c"]
        )
        self.assertEqual(
            p.get_section_as_args("dependencies", {"foo": "bar"}),
            ["a", "b", "c", "foo"],
        )

        p2 = ManifestParser(
            "test",
            """
[manifest]
name = foo

[autoconf.args]
--prefix=/foo
--with-woot
""",
        )
        self.assertEqual(
            p2.get_section_as_args("autoconf.args"), ["--prefix=/foo", "--with-woot"]
        )

    def test_section_as_dict(self):
        p = ManifestParser(
            "test",
            """
[manifest]
name = foo

[cmake.defines]
foo = bar

[cmake.defines.bar=baz]
foo = baz
""",
        )
        self.assertEqual(p.get_section_as_dict("cmake.defines"), {"foo": "bar"})
        self.assertEqual(
            p.get_section_as_dict("cmake.defines", {"bar": "baz"}), {"foo": "baz"}
        )

        p2 = ManifestParser(
            "test",
            """
[manifest]
name = foo

[cmake.defines.bar=baz]
foo = baz

[cmake.defines]
foo = bar
""",
        )
        self.assertEqual(
            p2.get_section_as_dict("cmake.defines", {"bar": "baz"}),
            {"foo": "bar"},
            msg="sections cascade in the order they appear in the manifest",
        )

    def test_parse_common_manifests(self):
        patch_loader(__name__)
        manifests = load_all_manifests(None)
        self.assertNotEqual(0, len(manifests), msg="parsed some number of manifests")

    if sys.version_info < (3, 2):

        def assertRaisesRegex(self, *args, **kwargs):
            return self.assertRaisesRegexp(*args, **kwargs)
