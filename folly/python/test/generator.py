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

from .simplegenerator import SimpleGenerator


class GeneratorTest(unittest.TestCase):
    def test_iter_generator(self) -> None:
        loop = asyncio.get_event_loop()

        async def wrapper() -> None:
            gen = SimpleGenerator("normal")
            expected = 1
            async for v in gen:
                self.assertEqual(v, expected)
                expected += 1
            self.assertEqual(expected, 6)

        loop.run_until_complete(wrapper())

    def test_iter_generator_empty(self) -> None:
        loop = asyncio.get_event_loop()

        async def wrapper() -> None:
            gen = SimpleGenerator("empty")
            async for _ in gen:  # noqa: F841
                self.assertFalse(True, "this should never run")
            else:
                self.assertTrue(
                    True, "this will be run when generator is empty, as expected"
                )

        loop.run_until_complete(wrapper())

    def test_iter_generator_error(self) -> None:
        loop = asyncio.get_event_loop()

        async def wrapper() -> None:
            gen = SimpleGenerator("error")
            async for v in gen:
                self.assertEqual(v, 42)
                break
            with self.assertRaises(RuntimeError):
                async for v in gen:
                    pass

        loop.run_until_complete(wrapper())
