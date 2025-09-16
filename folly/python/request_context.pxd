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

from libcpp.memory cimport shared_ptr

cdef extern from "folly/io/async/Request.h" namespace "folly":
    cdef cppclass RequestContext:
        @staticmethod
        shared_ptr[RequestContext] setContext(shared_ptr[RequestContext])
        @staticmethod
        shared_ptr[RequestContext] saveContext()

cdef class Context:
    cdef shared_ptr[RequestContext] _ptr

cpdef Context create() noexcept
cpdef Context set(Context ctx) noexcept
cpdef Context save() noexcept
cpdef Context get_from_contextvar() noexcept
