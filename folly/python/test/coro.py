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

import asyncio
import unittest

# pyre-fixme[21]: Could not find name `simplebridgecoro` in `folly.python.test`.
from folly.python.test import simplebridgecoro


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

    async def _test_executor_stats(self, task_count, block_ms, drive_count):
        initial_stats = simplebridgecoro.get_executor_stats()
        # Some compilation mode (libcxx) injects a different executor
        if initial_stats is None:
            return
        initial_count = initial_stats.drive_count

        # Run multiple coroutines
        tasks = []
        expected = []
        for i in range(task_count):
            expected.append(i)
            tasks.append(simplebridgecoro.blocking_task(block_ms, i))
        results = await asyncio.gather(*tasks)
        self.assertEqual(results, expected)

        final_stats = simplebridgecoro.get_executor_stats()
        self.assertEqual(final_stats.drive_count, initial_count + drive_count)

    def test_executor_stats(self):
        loop = asyncio.get_event_loop()
        task_count = 4
        # 4 * 1, less than 5ms default
        block_ms = 1
        # Drive called once
        drive_count = 1
        loop.run_until_complete(
            self._test_executor_stats(task_count, block_ms, drive_count)
        )

    def test_executor_stats_timeslice_0(self):
        simplebridgecoro.set_drive_time_slice_ms(0)
        loop = asyncio.get_event_loop()
        task_count = 4
        # Irrelevant, no time slice
        block_ms = 1
        # Drive called twice per task (schedule, process result)
        drive_count = 8
        loop.run_until_complete(
            self._test_executor_stats(task_count, block_ms, drive_count)
        )

    def test_executor_stats_blocking(self):
        loop = asyncio.get_event_loop()
        task_count = 3
        # More than 5ms default
        block_ms = 6
        # Drive called many times because we time slice
        drive_count = task_count
        loop.run_until_complete(
            self._test_executor_stats(task_count, block_ms, drive_count)
        )
