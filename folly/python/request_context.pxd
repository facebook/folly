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
        @staticmethod
        RequestContext* try_get()

cdef extern from "folly/python/request_context_capsule.h" namespace "folly::python":
    cdef object RequestContextToPyCapsule(shared_ptr[RequestContext] ptr)
    cdef shared_ptr[RequestContext] PyCapsuleToRequestContext(object ptr)

cdef class Context:
    cdef shared_ptr[RequestContext] _ptr

cdef object set_PyContext(shared_ptr[RequestContext]& ptr)
cdef int reset_PyContext(object token) except -1

cpdef Context save() noexcept
cpdef Context get_from_contextvar() noexcept
