# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# pyre-unsafe


import sys
import unittest

from ..load import load_all_manifests, patch_loader
from ..manifest import ManifestParser


class ManifestTest(unittest.TestCase):
    def test_missing_section(self) -> None:
        with self.assertRaisesRegex(
            Exception, "manifest file test is missing required section manifest"
        ):
            ManifestParser("test", "")

    def test_missing_name(self) -> None:
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

    def test_minimal(self) -> None:
        p = ManifestParser(
            "test",
            """
[manifest]
name = test
""",
        )
        self.assertEqual(p.name, "test")
        self.assertEqual(p.fbsource_path, None)

    def test_minimal_with_fbsource_path(self) -> None:
        p = ManifestParser(
            "test",
            """
[manifest]
name = test
fbsource_path = fbcode/wat
""",
        )
        self.assertEqual(p.name, "test")
        self.assertEqual(p.fbsource_path, "fbcode/wat")

    def test_unknown_field(self) -> None:
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
name = test
invalid.field = woot
""",
            )

    def test_invalid_section_name(self) -> None:
        with self.assertRaisesRegex(
            Exception, "manifest file test contains unknown section 'invalid.section'"
        ):
            ManifestParser(
                "test",
                """
[manifest]
name = test

[invalid.section]
foo = bar
""",
            )

    def test_value_in_dependencies_section(self) -> None:
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
name = test

[dependencies]
foo = bar
""",
            )

    def test_invalid_conditional_section_name(self) -> None:
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
name = test

[dependencies.=]
""",
            )

    def test_section_as_args(self) -> None:
        p = ManifestParser(
            "test",
            """
[manifest]
name = test

[dependencies]
a
b
c

[dependencies.test=on]
foo
""",
        )
        self.assertEqual(p.get_section_as_args("dependencies"), ["a", "b", "c"])
        self.assertEqual(
            p.get_section_as_args("dependencies", {"test": "off"}), ["a", "b", "c"]
        )
        self.assertEqual(
            p.get_section_as_args("dependencies", {"test": "on"}),
            ["a", "b", "c", "foo"],
        )

        p2 = ManifestParser(
            "test",
            """
[manifest]
name = test

[autoconf.args]
--prefix=/foo
--with-woot
""",
        )
        self.assertEqual(
            p2.get_section_as_args("autoconf.args"), ["--prefix=/foo", "--with-woot"]
        )

    def test_section_as_dict(self) -> None:
        p = ManifestParser(
            "test",
            """
[manifest]
name = test

[cmake.defines]
foo = bar

[cmake.defines.test=on]
foo = baz
""",
        )
        self.assertEqual(p.get_section_as_dict("cmake.defines", {}), {"foo": "bar"})
        self.assertEqual(
            p.get_section_as_dict("cmake.defines", {"test": "on"}), {"foo": "baz"}
        )

        p2 = ManifestParser(
            "test",
            """
[manifest]
name = test

[cmake.defines.test=on]
foo = baz

[cmake.defines]
foo = bar
""",
        )
        self.assertEqual(
            p2.get_section_as_dict("cmake.defines", {"test": "on"}),
            {"foo": "bar"},
            msg="sections cascade in the order they appear in the manifest",
        )

    def test_parse_common_manifests(self) -> None:
        patch_loader(__name__)
        manifests = load_all_manifests(None)
        self.assertNotEqual(0, len(manifests), msg="parsed some number of manifests")

    def test_mismatch_name(self) -> None:
        with self.assertRaisesRegex(
            Exception,
            "filename of the manifest 'foo' does not match the manifest name 'bar'",
        ):
            ManifestParser(
                "foo",
                """
[manifest]
name = bar
""",
            )

    def test_duplicate_manifest(self) -> None:
        patch_loader(__name__, "fixtures/duplicate")

        with self.assertRaisesRegex(Exception, "found duplicate manifest 'foo'"):
            load_all_manifests(None)

    if sys.version_info < (3, 2):

        def assertRaisesRegex(self, *args, **kwargs):
            return self.assertRaisesRegexp(*args, **kwargs)
