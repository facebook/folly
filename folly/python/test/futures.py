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

# pyre-unsafe

import unittest
from sys import platform

# pyre-fixme[21]: Could not find name `simplebridge` in `folly.python.test`.
from folly.python.test import simplebridge


class Futures(unittest.IsolatedAsyncioTestCase):
    async def test_bridge(self):
        val = 1337
        res = await simplebridge.get_value_x5(val)
        self.assertEqual(val * 5, res)

    async def test_bridge_semifuture(self):
        val = 1337
        res = await simplebridge.get_value_x5_semifuture(val)
        self.assertEqual(val * 5, res)

    async def test_bridge_exception(self):
        with self.assertRaises(ValueError, msg="0 is not allowed"):
            await simplebridge.get_value_x5(0)

    @unittest.skipIf(platform.startswith("win"), "Broken on Windows.")
    async def test_bridge_fibers(self):
        val = 1337
        res = await simplebridge.get_value_x5_fibers(val)
        self.assertEqual(val * 5, res)
