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
import sys
import unittest

from folly.iobuf import copy_from_buffer, IOBuf


class IOBufExtTest(unittest.TestCase):
    def test_iobuf_should_not_hang_when_outliving_asyncio_loop(self) -> None:
        """
        Regression test for the AsyncioExecutor::drop() hang triggered when an
        IOBuf created from Python bytes outlives the asyncio loop that built it.

        Before D100259471: `iobuf_ext.cpp` stored a raw `Executor*` ->
        use-after-free when the executor was destroyed before the IOBuf.

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

    def test_copy_from_buffer_bytes(self) -> None:
        """copy_from_buffer works with bytes input."""
        source = b"test_payload_12345"
        buf = copy_from_buffer(source, len(source))

        self.assertEqual(bytes(buf), source)
        self.assertIsInstance(buf, IOBuf)

        # IOBuf owns its own copy — deleting source doesn't affect it
        source_copy = source
        del source
        gc.collect()
        self.assertEqual(bytes(buf), source_copy)

    def test_copy_from_buffer_bytearray(self) -> None:
        """copy_from_buffer works with bytearray input."""
        source = bytearray(b"bytearray_data_xyz")
        buf = copy_from_buffer(source, len(source))

        self.assertEqual(bytes(buf), bytes(source))
        self.assertIsInstance(buf, IOBuf)

        # Mutating source doesn't affect the IOBuf (it's a copy)
        expected = bytes(source)
        source[:5] = b"\x00" * 5
        self.assertEqual(bytes(buf), expected)

    def test_copy_from_buffer_memoryview(self) -> None:
        """copy_from_buffer works with memoryview input."""
        source = b"memoryview_test_data"
        mv = memoryview(source)
        buf = copy_from_buffer(mv, len(mv))

        self.assertEqual(bytes(buf), source)
        self.assertIsInstance(buf, IOBuf)

    def test_copy_from_buffer_does_not_pin_source_object(self) -> None:
        """copy_from_buffer does NOT hold a reference to the source object."""
        source = bytearray(b"x" * 1024)
        initial_refcount = sys.getrefcount(source)

        buf = copy_from_buffer(source, len(source))

        self.assertEqual(bytes(buf), bytes(source))
        final_refcount = sys.getrefcount(source)
        self.assertEqual(final_refcount, initial_refcount)
