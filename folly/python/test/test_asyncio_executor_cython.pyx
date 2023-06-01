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
import multiprocessing
import sys
from socket import socketpair
from ctypes import c_int
from libcpp cimport bool

from folly.executor cimport ProactorExecutor, IocpQueue


cdef class TestExecutor(ProactorExecutor):
    cdef:
        bool called

    def drive(self):
        self.called = True
        # stop the test once the callback has been called
        asyncio.get_event_loop().stop()

class ReaderQueue:
    def __init__(self):
        self.rsock, self.wsock = socketpair()

    def __del__(self):
        self.rsock.close()
        self.wsock.close()

    def notify(self):
        self.wsock.send("\n".encode())



def test_add_callback_to_event_loop(test):
    loop = asyncio.new_event_loop()
    if sys.platform == "win32":
        executor = TestExecutor(loop._proactor._iocp)
        queue = IocpQueue(executor)
        queue.swap(loop)
    else:
        executor = TestExecutor(0)
        queue = ReaderQueue()
        loop.add_reader(queue.rsock, executor.drive)

    test.assertFalse(executor.called)
    queue.notify()
    try:
        loop.run_forever()
    finally:
        loop.close()
        test.assertTrue(executor.called)
