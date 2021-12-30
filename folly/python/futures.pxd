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
from folly cimport cFollyTry, cFollyFuture, cFollyExecutor, cFollySemiFuture

cdef extern from "folly/python/futures.h" namespace "folly::python":
    void bridgeFuture[T](
        cFollyFuture[T]&& fut,
        void(*)(cFollyTry[T]&&, PyObject*),
        PyObject* pyFuture
    )
    # No clue but cython overloading is getting confused so we alias
    void bridgeFutureWith "folly::python::bridgeFuture"[T](
        cFollyExecutor* executor,
        cFollyFuture[T]&& fut,
        void(*)(cFollyTry[T]&&, PyObject*),
        PyObject* pyFuture
    )
    void bridgeSemiFuture[T](
        cFollySemiFuture[T]&& fut,
        void(*)(cFollyTry[T]&&, PyObject*),
        PyObject* pyFuture
    )
    # No clue but cython overloading is getting confused so we alias
    void bridgeSemiFutureWith "folly::python::bridgeSemiFuture"[T](
        cFollyExecutor* executor,
        cFollySemiFuture[T]&& fut,
        void(*)(cFollyTry[T]&&, PyObject*),
        PyObject* pyFuture
    )
