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
from sys import platform

from . import simplebridge, simplebridgecoro


@unittest.skipIf(platform.startswith("win"), "Broken on Windows.")
class Teardown(unittest.TestCase):
    """
    The lifetimes of the native AsyncioExecutor/FiberManager objects
    are bound to that of the python event loop.

    If the loop is destroyed with pending work in the fiber-manager,
    there may be a race condition where the fiber manager is destroyed
    before being drained.

    These tests ensure that both objects are cleanly driven and destroyed
    before the process exits, irrespective of the order in which these
    objects are destroyed.
    """

    def test_fiber_manager_tear_down(self):
        simplebridge.get_value_x5_semifuture(1)
        simplebridge.get_value_x5_fibers(1)

    def test_drive_on_teardown(self):
        """
        A test to ensure that any pending keep-alives are respected when an
        AsyncioExecutor instance is dropped
        """

        async def test() -> None:
            # Sanity check
            self.assertEqual(123, await simplebridgecoro.sleep_then_echo(1, 123))

            # Schedule a sleep for 1s and the immediately exit
            asyncio.ensure_future(simplebridgecoro.sleep_then_echo(10, 0))

        loop = asyncio.new_event_loop()
        loop.run_until_complete(test())
        loop.close()

        # Once the loop goes out of scope, AsyncioExecutor's destructor must drive
        # until all keep-alives are released (otherwise the test should crash with a UB)
