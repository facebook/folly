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
from cpython.pycapsule cimport PyCapsule_CheckExact


_RequestContext = PyContextVar_New("_RequestContext", NULL)


cdef object set_PyContext(shared_ptr[RequestContext]& ptr):
    return PyContextVar_Set(_RequestContext, RequestContextToPyCapsule(ptr))


cdef int reset_PyContext(object token) except -1:
    return PyContextVar_Reset(_RequestContext, token)


@cython.auto_pickle(False)
cdef class Context:
    def __eq__(self, Context other):
        return self._ptr.get() == other._ptr.get()

    def __hash__(self):
        return <unsigned long>(self._ptr.get())

    def __repr__(self):
        return f"<{self.__class__!r}: {<unsigned long>self._ptr.get()}>"

    def __bool__(self):
        return self._ptr.get() != NULL


cpdef Context save() noexcept:
    """ Return the current setcontext """
    cdef Context ctx = Context.__new__(Context)
    ctx._ptr = RequestContext.saveContext()
    return ctx


cpdef Context get_from_contextvar() noexcept:
    """ Return the current context from the contextvar """
    cdef Context ctx = Context.__new__(Context)

    ctx_var = get_value(_RequestContext)
    if ctx_var is not None and PyCapsule_CheckExact(ctx_var):
        ctx._ptr = PyCapsuleToRequestContext(ctx_var)
    return ctx


@contextmanager
def active():
    """ Create a Context and shove it into the python contextvar """
    cdef shared_ptr[RequestContext] rctx = make_shared[RequestContext]()
    prev_rctx = RequestContext.setContext(rctx)
    token = set_PyContext(rctx)
    cdef Context ctx = Context.__new__(Context)
    ctx._ptr = rctx
    try:
        yield ctx
    finally:
        assert RequestContext.setContext(prev_rctx) == rctx
        reset_PyContext(token)


cdef extern from "folly/python/request_context.h":
    ctypedef enum PyContextEvent:
        Py_CONTEXT_SWITCHED = 1

    cdef int FOLLY_PYTHON_PyContext_AddWatcher(
        int(*PyContext_WatchCallback)(PyContextEvent, PyObject* pycontext)
    )


cdef int _watcher(PyContextEvent event, PyObject* pycontext):
    if pycontext is NULL or not PyContext_CheckExact(<object>pycontext) or event != PyContextEvent.Py_CONTEXT_SWITCHED:
        return 0

    cdef shared_ptr[RequestContext] empty_ctx
    ctx = get_value(_RequestContext)
    if PyCapsule_CheckExact(ctx):
        RequestContext.setContext(PyCapsuleToRequestContext(ctx))
    else:
        # If we don't set something here, then the RequestContext will leak to the next PyContext
        set_PyContext(empty_ctx)

    return 0


FOLLY_PYTHON_PyContext_AddWatcher(_watcher)
