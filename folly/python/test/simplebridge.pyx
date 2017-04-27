import asyncio
from folly.futures cimport bridgeFuture
from folly cimport cFollyFuture, cFollyTry
from libc.stdint cimport uint64_t
from cpython.ref cimport PyObject
from cython.operator cimport dereference as deref

cdef extern from "folly/python/test/simple.h" namespace "folly::python::test":
    cdef cFollyFuture[uint64_t] future_getValueX5(uint64_t val)


def get_value_x5(int val):
    loop = asyncio.get_event_loop()
    fut = loop.create_future()
    bridgeFuture[uint64_t](
        future_getValueX5(val),
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
