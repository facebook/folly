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
from unittest import IsolatedAsyncioTestCase

from folly import request_context


async def get_Context(pass_ctx) -> request_context.Context | None:
    ctx = request_context.get_from_contextvar()
    assert pass_ctx is ctx
    for _ in range(25):
        await asyncio.sleep(0)
        assert ctx is request_context.get_from_contextvar()
    return request_context.get_from_contextvar()


class RequestContextTest(IsolatedAsyncioTestCase):
    async def test_request_context(self) -> None:
        with request_context.active() as ctx1:
            # Create task used copy_context() during its creation
            # So as soon as the task is created we can "Restore" the
            # Previous folly context
            task1 = asyncio.create_task(get_Context(ctx1))

        with request_context.active() as ctx2:
            task2 = asyncio.create_task(get_Context(ctx2))

        with request_context.active() as ctx3:
            task3 = asyncio.create_task(get_Context(ctx3))

        await asyncio.wait([task1, task2, task3])

        self.assertEqual(await task1, ctx1)
        self.assertEqual(await task2, ctx2)
        self.assertEqual(await task3, ctx3)
