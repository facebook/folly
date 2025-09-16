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

cimport cython
from contextlib import contextmanager
from libcpp.memory cimport make_shared
from cpython.object cimport PyObject
from cpython.contextvars cimport get_value, PyContextVar_Set, PyContextVar_Reset, PyContextVar_New, PyContext_CheckExact


_RequestContext = PyContextVar_New("_RequestContext", NULL)


@cython.auto_pickle(False)
cdef class Context:
    def __eq__(self, Context other):
        return self._ptr.get() == other._ptr.get()

    def __hash__(self):
        return <unsigned long>(self._ptr.get())

    def __repr__(self):
        return f"<{self.__class__!r}: {<unsigned long>self._ptr.get()}>"


cpdef Context create() noexcept:
    """ Create a new context """
    cdef Context ctx = Context.__new__(Context)
    ctx._ptr = make_shared[RequestContext]()
    return ctx


cpdef Context set(Context ctx) noexcept:
    """ Set the current context, Return the previous context """
    cdef Context prev = Context.__new__(Context)
    prev._ptr = RequestContext.setContext(ctx._ptr)
    return prev


cpdef Context save() noexcept:
    """ Return the current setcontext """
    cdef Context ctx = Context.__new__(Context)
    ctx._ptr = RequestContext.saveContext()
    return ctx


cpdef Context get_from_contextvar() noexcept:
    """ Return the current context from the contextvar """
    return get_value(_RequestContext)


@contextmanager
def active():
    """ Create a Context and shove it into the python contextvar """
    ctx = create()
    prev = set(ctx)
    token = PyContextVar_Set(_RequestContext, ctx)
    try:
        yield ctx
    finally:
        existing = set(prev)
        assert existing == ctx
        PyContextVar_Reset(_RequestContext, token)


cdef extern from "folly/python/request_context.h":
    ctypedef enum PyContextEvent:
        Py_CONTEXT_SWITCHED = 1

    cdef int FOLLY_PYTHON_PyContext_AddWatcher(
        int(*PyContext_WatchCallback)(PyContextEvent, PyObject* pycontext)
    )


cdef int _watcher(PyContextEvent event, PyObject* pycontext):
    if pycontext is NULL or not PyContext_CheckExact(<object>pycontext) or event != PyContextEvent.Py_CONTEXT_SWITCHED:
        return 0

    ctx = get_value(_RequestContext)
    if ctx is not None:
        set(ctx)

    return 0


FOLLY_PYTHON_PyContext_AddWatcher(_watcher)
