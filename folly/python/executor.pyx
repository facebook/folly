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

from folly.executor cimport cAsyncioExecutor
from libcpp.memory cimport make_unique, unique_ptr
from cython.operator cimport dereference as deref
from weakref import WeakKeyDictionary

# asyncio Loops to AsyncioExecutor
loop_to_q = WeakKeyDictionary()


cdef class AsyncioExecutor:
    def __cinit__(self):
        self.cQ = make_unique[cAsyncioExecutor]()

    def fileno(AsyncioExecutor self):
        return deref(self.cQ).fileno()

    def drive(AsyncioExecutor self):
        deref(self.cQ).drive()

    def __dealloc__(AsyncioExecutor self):
        # We drive it one last time
        deref(self.cQ).drive()
        # We are Explicitly reset here, otherwise it is possible
        # that self.cQ dstor runs after python finalize
        # Cython deletes these after __dealloc__ returns.
        self.cQ.reset()


cdef cAsyncioExecutor* get_executor():
    try:
        loop = asyncio.get_event_loop()
    except RuntimeError:
        return NULL
    try:
        Q = <AsyncioExecutor>(loop_to_q[loop])
    except KeyError:
        Q = AsyncioExecutor()
        loop.add_reader(Q.fileno(), Q.drive)
        loop_to_q[loop] = Q
    return Q.cQ.get()
