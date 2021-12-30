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

from cpython.ref cimport PyObject
from folly cimport cFollyExecutor, cFollyTry
from libcpp cimport bool

cdef extern from "folly/python/coro.h" namespace "folly::coro" nogil:
    cdef cppclass cFollyCoroTask "folly::coro::Task"[T]:
        pass

cdef extern from "folly/CancellationToken.h" namespace "folly" nogil:
    cdef cppclass cFollyCancellationToken "folly::CancellationToken":
        pass

    cdef cppclass cFollyCancellationSource "folly::CancellationSource":
        cFollyCancellationSource() except +
        cFollyCancellationToken getToken()
        bool requestCancellation()

cdef extern from "folly/python/coro.h" namespace "folly::python":
    void bridgeCoroTask[T](
        cFollyCoroTask[T]&& fut,
        void(*)(cFollyTry[T]&&, PyObject*),
        PyObject* pyFuture
    )
    # No clue but cython overloading is getting confused so we alias
    void bridgeCoroTaskWith "folly::python::bridgeCoroTask"[T](
        cFollyExecutor* executor,
        cFollyCoroTask[T]&& fut,
        void(*)(cFollyTry[T]&&, PyObject*),
        PyObject* pyFuture
    )
    void bridgeCoroTaskWithCancellation "folly::python::bridgeCoroTask"[T](
        cFollyExecutor* executor,
        cFollyCoroTask[T]&& fut,
        void(*)(cFollyTry[T]&&, PyObject*),
        PyObject* pyFuture,
        cFollyCancellationToken&& cancelToken,
    )
