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
from contextvars import copy_context
from functools import partial
from unittest import IsolatedAsyncioTestCase

from folly import request_context


async def get_Context(pass_ctx) -> request_context.Context | None:
    ctx = request_context.get_from_contextvar()
    assert pass_ctx == ctx, f"Expected {pass_ctx} but got {ctx}"
    for _ in range(25):
        await asyncio.sleep(0)
        assert ctx == request_context.get_from_contextvar()
    return request_context.get_from_contextvar()


class RequestContextTest(IsolatedAsyncioTestCase):
    async def test_request_context(self) -> None:
        with request_context.active() as ctx1:
            # Create task used copy_context() during its creation
            # So as soon as the task is created we can "Restore" the
            # Previous folly context

            # Will active, save and get_from_contextvar() should equal
            self.assertEqual(
                request_context.get_from_contextvar(), request_context.save()
            )
            task1 = asyncio.create_task(get_Context(ctx1))

        with request_context.active() as ctx2:
            task2 = asyncio.create_task(get_Context(ctx2))

        with request_context.active() as ctx3:
            task3 = asyncio.create_task(get_Context(ctx3))

        # Lets have a task with the NULL RequestContext
        task4 = asyncio.create_task(get_Context(request_context.get_from_contextvar()))

        await asyncio.wait([task1, task2, task3, task4])

        self.assertEqual(await task1, ctx1)
        self.assertEqual(await task2, ctx2)
        self.assertEqual(await task3, ctx3)
        # Insure that the NULL ctx is not in any of the saved RequestContexts
        self.assertNotIn(request_context.get_from_contextvar(), [ctx1, ctx2, ctx3])
        self.assertEqual(await task4, request_context.get_from_contextvar())

    async def test_callbacks(self) -> None:
        fut1 = asyncio.Future()
        fut2 = asyncio.Future()
        fut3 = asyncio.Future()

        def callback(
            ctx: request_context.Context, fut: asyncio.Future, task: asyncio.Task
        ) -> None:
            try:
                current_ctx = request_context.save()
                self.assertEqual(ctx, current_ctx)
                fut.set_result(current_ctx)
            except Exception as e:
                fut.set_exception(e)

        ctx = request_context.get_from_contextvar()
        with request_context.active() as ctx1:
            task1 = asyncio.create_task(get_Context(ctx1))
            # Callbacks execut with the context at the time they are added, or the context passed in.
            task1.add_done_callback(partial(callback, ctx1, fut1))
            context = copy_context()

        expected = request_context.get_from_contextvar()
        self.assertNotEqual(expected, ctx1)
        self.assertEqual(expected, ctx)
        # Since this one was added out here it should execute with this context
        task1.add_done_callback(partial(callback, ctx, fut2))
        # We want it to execute with the context that we had active above
        task1.add_done_callback(partial(callback, ctx1, fut3), context=context)
        self.assertEqual(await task1, ctx1)
        self.assertEqual(await fut1, ctx1)
        self.assertEqual(await fut2, ctx)
        self.assertEqual(await fut3, ctx1)
