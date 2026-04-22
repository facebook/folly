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
import gc
import unittest

from folly.iobuf import IOBuf


class IOBufExtTest(unittest.TestCase):
    def test_iobuf_should_not_hang_when_outliving_asyncio_loop(self) -> None:
        """
        Regression test for the AsyncioExecutor::drop() hang triggered when an
        IOBuf created from Python bytes outlives the asyncio loop that built it.

        Before D100259471: `iobuf_ext.cpp` stored a raw `Executor*` ->
        use-after-free (S646339) when the executor was destroyed before the
        IOBuf.

        After D100259471 (KeepAlive fix): IOBuf holds an `Executor::KeepAlive<>`
        -> AsyncioExecutor::drop() busy-spins in
        `while (keepAliveCounter_ > 0) { drive(); }` because the IOBuf is a
        long-lived ref holder, not a queued task.

        This reproduces the second failure mode: a single IOBuf returned from
        an asyncio coroutine, held in test scope while the loop is GC'd, hangs
        the process at exit. Buck reports the test as timing out at the
        wall-clock cap.
        """

        async def make_iobuf() -> IOBuf:
            return IOBuf(b"hello")

        loop = asyncio.new_event_loop()
        try:
            buf = loop.run_until_complete(make_iobuf())
        finally:
            loop.close()

        del loop
        gc.collect()

        self.assertEqual(bytes(buf), b"hello")


if __name__ == "__main__":
    unittest.main()
