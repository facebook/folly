# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# pyre-unsafe


import unittest

from ..platform import HostType


class PlatformTest(unittest.TestCase):
    def test_create(self) -> None:
        p = HostType()
        self.assertNotEqual(p.ostype, None, msg="probed and returned something")

        tuple_string = p.as_tuple_string()
        round_trip = HostType.from_tuple_string(tuple_string)
        self.assertEqual(round_trip, p)

    def test_rendering_of_none(self) -> None:
        p = HostType(ostype="foo")
        self.assertEqual(p.as_tuple_string(), "foo-none-none")

    def test_is_methods(self) -> None:
        p = HostType(ostype="windows")
        self.assertTrue(p.is_windows())
        self.assertFalse(p.is_darwin())
        self.assertFalse(p.is_linux())

        p = HostType(ostype="darwin")
        self.assertFalse(p.is_windows())
        self.assertTrue(p.is_darwin())
        self.assertFalse(p.is_linux())

        p = HostType(ostype="linux")
        self.assertFalse(p.is_windows())
        self.assertFalse(p.is_darwin())
        self.assertTrue(p.is_linux())
