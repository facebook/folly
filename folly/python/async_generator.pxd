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

from folly.coro cimport cFollyCoroTask

cdef extern from "folly/python/async_generator.h" namespace "folly::coro":
    cdef cppclass cAsyncGenerator "folly::coro::AsyncGenerator"[T]:
        pass


cdef extern from "folly/python/async_generator.h" namespace "folly::python":
    cdef cppclass cNextResult "folly::python::NextResult"[T]:
        bint has_value()
        T value()

    cdef cppclass cAsyncGeneratorWrapper "folly::python::AsyncGeneratorWrapper"[T]:
        cAsyncGeneratorWrapper() except +
        cAsyncGeneratorWrapper(cAsyncGenerator[T]&&) except +
        cFollyCoroTask[cNextResult[T]] getNext()
