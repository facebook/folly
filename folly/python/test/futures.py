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

import asyncio
import unittest

from . import simplebridge


class Futures(unittest.TestCase):
    def test_bridge(self):
        val = 1337
        loop = asyncio.get_event_loop()
        res = loop.run_until_complete(simplebridge.get_value_x5(val))
        self.assertEqual(val * 5, res)

    def test_bridge_semifuture(self):
        val = 1337
        loop = asyncio.get_event_loop()
        res = loop.run_until_complete(simplebridge.get_value_x5_semifuture(val))
        self.assertEqual(val * 5, res)

    def test_bridge_exception(self):
        loop = asyncio.get_event_loop()
        with self.assertRaises(ValueError, msg="0 is not allowed"):
            loop.run_until_complete(simplebridge.get_value_x5(0))

    def test_bridge_fibers(self):
        val = 1337
        loop = asyncio.get_event_loop()
        res = loop.run_until_complete(simplebridge.get_value_x5_fibers(val))
        self.assertEqual(val * 5, res)
