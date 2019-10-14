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

import asyncio
from cpython.ref cimport PyObject

from folly cimport cFollyTry
from folly.coro cimport bridgeCoroTaskWith
from folly.executor cimport cAsyncioExecutor, get_executor
from folly.async_generator cimport cAsyncGenerator, cAsyncGeneratorWrapper, cNextResult


cdef extern from "folly/python/test/simplegenerator.h" namespace "folly::python::test":
    cAsyncGenerator[int] make_generator_empty()
    cAsyncGenerator[int] make_generator()
    cAsyncGenerator[int] make_generator_error()


ctypedef cAsyncGeneratorWrapper[int] cIntGenerator


cdef extern from "<utility>" namespace "std" nogil:
    cdef cIntGenerator move "std::move"(cIntGenerator)


cdef class SimpleGenerator:

    cdef cIntGenerator generator
    cdef cAsyncioExecutor* executor

    def __cinit__(self, flavor):
        if flavor == "normal":
            self.generator = move(cIntGenerator(make_generator()))
        elif flavor == "empty":
            self.generator = move(cIntGenerator(make_generator_empty()))
        elif flavor == "error":
            self.generator = move(cIntGenerator(make_generator_error()))


    @staticmethod
    cdef void callback(
        cFollyTry[cNextResult[int]]&& res,
        PyObject* py_future,
    ):
        future = <object> py_future
        if res.hasException():
            try:
                res.exception().throw_exception()
            except Exception as ex:
                future.set_exception(ex)
        else:
            if res.value().has_value():
                future.set_result(res.value().value())
            else:
                future.set_exception(StopAsyncIteration())

    def __aiter__(self):
        return self

    def __anext__(self):
        loop = asyncio.get_event_loop()
        future = loop.create_future()
        bridgeCoroTaskWith[cNextResult[int]](
            get_executor(),
            self.generator.getNext(),
            SimpleGenerator.callback,
            <PyObject *> future,
        )
        return future
