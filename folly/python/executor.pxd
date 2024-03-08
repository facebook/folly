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
from libcpp cimport bool
from libc.stdint cimport uint64_t, uintptr_t


cdef extern from "folly/python/AsyncioExecutor.h" namespace "folly::python":
    cdef cppclass cAsyncioExecutor "folly::python::AsyncioExecutor"(cFollyExecutor):
        void drive()

    cdef cppclass cDeleter "folly::python::NotificationQueueAsyncioExecutor::Deleter":
        pass

    cdef cppclass cNotificationQueueAsyncioExecutor "folly::python::NotificationQueueAsyncioExecutor"(cAsyncioExecutor):
        int fileno()
        @staticmethod
        unique_ptr[cNotificationQueueAsyncioExecutor, cDeleter] create()

cdef extern from "folly/python/ProactorExecutor.h" namespace "folly::python":
    cdef cppclass cProactorExecutorDeleter "folly::python::ProactorExecutor::Deleter":
        pass

    cdef cppclass cProactorExecutor "folly::python::ProactorExecutor"(cAsyncioExecutor):
        bool execute(uint64_t address)
        void notify()
        @staticmethod
        unique_ptr[cProactorExecutor, cProactorExecutorDeleter] create[T](T iocp)

cdef class AsyncioExecutor:
    cdef cAsyncioExecutor* _executor

cdef class ProactorExecutor(AsyncioExecutor):
    cdef unique_ptr[cProactorExecutor, cProactorExecutorDeleter] cQ


cdef class IocpQueue(dict):
    cdef ProactorExecutor _executor

cdef api cAsyncioExecutor* get_executor()
cdef api int set_executor_for_loop(loop, cAsyncioExecutor* executor)
cdef api cAsyncioExecutor* get_running_executor(bint running)
cdef api cAsyncioExecutor* get_running_executor_drive(
    bint running, bint driveBeforeDealloc)
