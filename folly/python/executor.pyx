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

from folly.executor cimport cAsyncioExecutor, cNotificationQueueAsyncioExecutor
from libcpp.memory cimport make_unique, unique_ptr
from cython.operator cimport dereference as deref
from weakref import WeakKeyDictionary

# asyncio Loops to AsyncioExecutor
loop_to_q = WeakKeyDictionary()


cdef class AsyncioExecutor:
    pass


cdef class NotificationQueueAsyncioExecutor(AsyncioExecutor):
    def __cinit__(self):
        self.cQ = make_unique[cNotificationQueueAsyncioExecutor]()
        self._executor = self.cQ.get()

    def fileno(NotificationQueueAsyncioExecutor self):
        return deref(self.cQ).fileno()

    def drive(NotificationQueueAsyncioExecutor self):
        deref(self.cQ).drive()

    def __dealloc__(NotificationQueueAsyncioExecutor self):
        # We drive it one last time
        deref(self.cQ).drive()
        # We are Explicitly reset here, otherwise it is possible
        # that self.cQ dstor runs after python finalize
        # Cython deletes these after __dealloc__ returns.
        self.cQ.reset()


# TODO: fried this is a stop gap, we really should not bind things to
# the default eventloop if its not running. As it may never be run.
# modern python asyncio suggests never using the "default" event loop
# I don't believe we will be able to force the behavior that
# get_executor() should always be run from a running eventloop in a single
# diff. But ultimately we will want to remove this function and
# go back to just get_executor() that only binds to a running loop.
cdef cAsyncioExecutor* get_running_executor(bint running):
    try:
        if running:
            loop = asyncio.get_running_loop()
        else:
            loop = asyncio.get_event_loop()
    except RuntimeError:
        return NULL
    try:
        executor = <AsyncioExecutor>(loop_to_q[loop])
    except KeyError:
        executor = NotificationQueueAsyncioExecutor()
        loop.add_reader(executor.fileno(), executor.drive)
        loop_to_q[loop] = executor
    return executor._executor

cdef int set_executor_for_loop(loop, cAsyncioExecutor* c_executor):
    if c_executor == NULL:
        del loop_to_q[loop]
        return 0

    if loop in loop_to_q:
        return -1

    executor = AsyncioExecutor()
    executor._executor = c_executor
    loop_to_q[loop] = executor

    return 0

cdef cAsyncioExecutor* get_executor():
    return get_running_executor(False)
