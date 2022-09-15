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

from libcpp cimport bool as cbool

cdef extern from "folly/ExceptionWrapper.h" namespace "folly":
    cdef cppclass cFollyExceptionWrapper "folly::exception_wrapper":
        void throw_exception() except +
        T* get_exception[T]()


cdef extern from "folly/Try.h" namespace "folly" nogil:
    cdef cppclass cFollyTry "folly::Try"[T]:
        T value()
        cbool hasException[T]()
        cbool hasException()
        cFollyExceptionWrapper exception()

cdef extern from "folly/futures/Future.h" namespace "folly" nogil:
    cdef cppclass cFollyFuture "folly::Future"[T]:
        T get()
        cbool hasValue()
        cbool isReady()
        #TODO add via and then

    cdef cppclass cFollySemiFuture "folly::SemiFuture"[T]:
        T get()
        pass

cdef extern from "folly/Unit.h" namespace "folly":
    struct cFollyUnit "folly::Unit":
        pass

    cFollyUnit c_unit "folly::unit"

cdef extern from "folly/futures/Promise.h" namespace "folly":
    cdef cppclass cFollyPromise "folly::Promise"[T]:
        cFollyPromise()
        cFollyPromise(cFollyPromise&&)
        void setValue[M](M& value)
        void setException[E](E& value)
        cFollyFuture[T] getFuture()

        @staticmethod
        cFollyPromise[T] makeEmpty()

cdef extern from "folly/Executor.h" namespace "folly":
    cdef cppclass cFollyExecutor "folly::Executor":
        pass
