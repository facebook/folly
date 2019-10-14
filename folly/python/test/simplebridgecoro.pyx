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
from folly.coro cimport cFollyCoroTask, bridgeCoroTask
from folly cimport cFollyTry
from libc.stdint cimport uint64_t
from cpython.ref cimport PyObject
from cython.operator cimport dereference as deref

cdef extern from "folly/python/test/simplecoro.h" namespace "folly::python::test":
    cdef cFollyCoroTask[uint64_t] coro_getValueX5(uint64_t val)


def get_value_x5_coro(int val):
    loop = asyncio.get_event_loop()
    fut = loop.create_future()
    bridgeCoroTask[uint64_t](
        coro_getValueX5(val),
        handle_uint64_t,
        <PyObject *>fut
    )
    return fut


cdef void handle_uint64_t(cFollyTry[uint64_t]&& res, PyObject* userData):
    future = <object> userData
    if res.hasException():
        try:
            res.exception().throw_exception()
        except Exception as ex:
            future.set_exception(ex)
    else:
        future.set_result(res.value())
