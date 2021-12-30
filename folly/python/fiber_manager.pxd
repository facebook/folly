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

from libcpp.memory cimport unique_ptr
from libcpp cimport bool
from folly.executor cimport cAsyncioExecutor

cdef extern from "folly/fibers/LoopController.h" namespace "folly::fibers":
    cdef cppclass cLoopController "folly::fibers::LoopController":
        pass

cdef extern from "folly/fibers/FiberManagerInternal.h" namespace "folly::fibers":
    cdef cppclass cFiberManagerOptions "folly::fibers::FiberManager::Options":
        pass
    cdef cppclass cFiberManager "folly::fibers::FiberManager":
        cFiberManager(unique_ptr[cLoopController], const cFiberManagerOptions&)
        void loopUntilNoReady()
        bool isRemoteScheduled() const

cdef extern from "folly/fibers/ExecutorLoopController.h" namespace "folly::fibers":
    cdef cppclass cAsyncioLoopController "folly::fibers::ExecutorLoopController"(cLoopController):
        cAsyncioLoopController(cAsyncioExecutor*)

cdef api cFiberManager* get_fiber_manager(const cFiberManagerOptions&)
