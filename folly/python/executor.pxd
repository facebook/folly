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

# distutils: language = c++

from libcpp.memory cimport unique_ptr
from folly cimport cFollyExecutor

cdef extern from "folly/python/AsyncioExecutor.h" namespace "folly::python":
    cdef cppclass cAsyncioExecutor "folly::python::AsyncioExecutor"(cFollyExecutor):
        void drive()
        void driveNoDiscard()

    cdef cppclass cNotificationQueueAsyncioExecutor "folly::python::NotificationQueueAsyncioExecutor"(cAsyncioExecutor):
        int fileno()

cdef class AsyncioExecutor:
    cdef cAsyncioExecutor* _executor

cdef class NotificationQueueAsyncioExecutor(AsyncioExecutor):
    cdef unique_ptr[cNotificationQueueAsyncioExecutor] cQ

cdef api cAsyncioExecutor* get_executor()
cdef api int set_executor_for_loop(loop, cAsyncioExecutor* executor)
cdef api cAsyncioExecutor* get_running_executor(bint running)
