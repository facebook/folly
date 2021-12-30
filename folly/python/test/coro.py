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

import asyncio
import unittest

from . import simplebridgecoro


class Futures(unittest.TestCase):
    def test_bridge_coro(self):
        val = 1337
        loop = asyncio.get_event_loop()
        res = loop.run_until_complete(simplebridgecoro.get_value_x5_coro(val))
        self.assertEqual(val * 5, res)

    def test_cancellation(self):
        loop = asyncio.get_event_loop()
        res = loop.run_until_complete(self._return_five_after_cancelled())
        self.assertEqual(5, res)

    async def _return_five_after_cancelled(self):
        task = asyncio.create_task(simplebridgecoro.return_five_after_cancelled())
        await asyncio.sleep(0.1)
        self.assertFalse(task.done())
        task.cancel()
        return await task
