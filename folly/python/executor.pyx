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
import sys

from folly.executor cimport cAsyncioExecutor, cNotificationQueueAsyncioExecutor, cProactorExecutor
from libcpp.memory cimport make_unique, unique_ptr
from cython.operator cimport dereference as deref
from weakref import WeakKeyDictionary
from cpython.ref cimport PyObject

# asyncio Loops to AsyncioExecutor
loop_to_q = WeakKeyDictionary()

_RaiseKeyError = object()

cdef class AsyncioExecutor:
    pass


cdef class NotificationQueueAsyncioExecutor(AsyncioExecutor):
    cdef unique_ptr[cNotificationQueueAsyncioExecutor, cDeleter] cQ
    cdef bool driveBeforeDealloc

    def __cinit__(self, driveBeforeDealloc = False):
        self.cQ = cNotificationQueueAsyncioExecutor.create()
        self._executor = self.cQ.get()
        self.driveBeforeDealloc = driveBeforeDealloc

    def fileno(NotificationQueueAsyncioExecutor self):
        return deref(self.cQ).fileno()

    def drive(NotificationQueueAsyncioExecutor self):
        deref(self.cQ).drive()

    def __dealloc__(NotificationQueueAsyncioExecutor self):
        if self.driveBeforeDealloc:
            self.drive()
        # We explicitly reset here, otherwise it is possible
        # that self.cQ destructor runs after python finalizes
        # Cython deletes these after __dealloc__ returns.
        self.cQ.reset()


cdef class ProactorExecutor(AsyncioExecutor):

    def __cinit__(self, uint64_t iocp):
        self.cQ = cProactorExecutor.create(<uint64_t&&>iocp)
        self._executor = self.cQ.get()

    def execute(ProactorExecutor self, uintptr_t  address):
        return deref(self.cQ).execute(address)

    def __dealloc__(ProactorExecutor self):
        # We explicitly reset here, otherwise it is possible
        # that self.cQ destructor runs after python finalizes
        # Cython deletes these after __dealloc__ returns.
        self.cQ.reset()


cdef class IocpQueue(dict):
    """
    Extends ProactoEventLoop's queue (a bare dictionary) to invoke ProactorExecutor::execute()
    when notified via the proactor's IOCP
    """
    def __init__(IocpQueue self, ProactorExecutor executor):
        self._executor = executor

    def swap(self, loop: asyncio.AbstractEventLoop):
        """
        Replace ProactorEventLoop's queue with self
        """
        if isinstance(loop, asyncio.ProactorEventLoop):
            self.update(loop._proactor._cache)
            loop._proactor._cache = self
        else:
            raise NotImplementedError("IocpQueue can only be used with ProactorExecutor")

    def pop(self, k, default = _RaiseKeyError):
        if self._executor.execute(k):
            f = asyncio.Future()
            f.set_result(None)
            return (f, None, None, None)
        if default == _RaiseKeyError:
            return super().pop(k)
        return super().pop(k, default)


# TODO: fried this is a stop gap, we really should not bind things to
# the default eventloop if its not running. As it may never be run.
# modern python asyncio suggests never using the "default" event loop
# I don't believe we will be able to force the behavior that
# get_executor() should always be run from a running eventloop in a single
# diff. But ultimately we will want to remove this function and
# go back to just get_executor() that only binds to a running loop.
cdef cAsyncioExecutor* get_running_executor(bint running):
    return get_running_executor_drive(running, False)

cdef cAsyncioExecutor* get_running_executor_drive(
    bint running, bint driveBeforeDealloc):
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
        if sys.platform == "win32":
            executor = ProactorExecutor(loop._proactor._iocp)
            queue = IocpQueue(executor)
            queue.swap(loop)
        else:
            executor = NotificationQueueAsyncioExecutor(driveBeforeDealloc)
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
