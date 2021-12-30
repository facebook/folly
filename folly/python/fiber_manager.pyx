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
from cython.operator cimport dereference as deref
from cpython.weakref cimport PyWeakref_NewRef, PyWeakref_GetObject
from cpython cimport PyObject
from libcpp.memory cimport unique_ptr
from folly.executor cimport get_executor
from libcpp.cast cimport (
    dynamic_cast,
)
from folly.executor cimport cAsyncioExecutor
from folly.fiber_manager cimport (
    cFiberManager,
    cLoopController,
    cAsyncioLoopController,
    cFiberManagerOptions)
from weakref import WeakKeyDictionary


#asynico Loops to FiberManager
loop_to_controller = WeakKeyDictionary()

# weak reference to the last seen event loop
last_loop = None
# FiberManager for the last seen event loop
last_manager = None

# cleanup callback that would clean last_manager
# if object referenced by last_loop is collected
def clean_last_manager(_wr):
    global last_manager
    last_manager = None

cdef class FiberManager:
    cdef unique_ptr[cFiberManager] cManager
    cdef cAsyncioExecutor* cExecutor

    # Lazy constructor, as __cinit__ doesn't support C types
    cdef init(self, const cFiberManagerOptions& opts):
        self.cExecutor = get_executor()
        self.cManager.reset(new cFiberManager(
            unique_ptr[cLoopController](new cAsyncioLoopController(
                self.cExecutor)),
            opts));

    def __dealloc__(FiberManager self):
        while deref(self.cManager).isRemoteScheduled():
            self.cExecutor.driveNoDiscard()

        deref(self.cManager).loopUntilNoReady()

        # Explicitly reset here, otherwise it is possible
        # that self.cManager dstor runs after python finalizes
        # Cython deletes these after __dealloc__ returns.
        self.cManager.reset()


cdef cFiberManager* get_fiber_manager(const cFiberManagerOptions& opts):
    global last_loop, last_manager

    loop = asyncio.get_event_loop()
    cdef FiberManager manager = None

    if last_loop is not None and <PyObject*>loop is PyWeakref_GetObject(last_loop):
        manager = last_manager
    if manager is None:
        try:
            manager = <FiberManager>(loop_to_controller[loop])
        except KeyError:
            manager = FiberManager()
            manager.init(opts)
            loop_to_controller[loop] = manager
        last_loop = PyWeakref_NewRef(loop, clean_last_manager)
        last_manager = manager
    return manager.cManager.get()
