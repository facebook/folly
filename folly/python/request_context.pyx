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
from cpython.contextvars cimport get_value_no_default as get_value, PyContextVar_Set, PyContextVar_Reset, PyContextVar_New, PyContext_CheckExact
from cpython.pycapsule cimport PyCapsule_CheckExact
from cpython.pystate cimport PyThreadState
from libcpp.utility cimport move

_RequestContext = PyContextVar_New("_RequestContext", NULL)


cdef object set_PyContext(shared_ptr[RequestContext] ptr) except *:
    return PyContextVar_Set(_RequestContext, RequestContextToPyCapsule(move(ptr)))


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
    cdef Context ctx = Context.__new__(Context)
    ctx._ptr = rctx
    try:
        yield ctx
    finally:
        assert RequestContext.setContext(prev_rctx) == rctx


cdef extern from "folly/python/request_context.h":
    ctypedef enum PyContextEvent:
        Py_CONTEXT_SWITCHED = 1

    cdef int FOLLY_PYTHON_PyContext_AddWatcher(
        int(*PyContext_WatchCallback)(PyContextEvent, PyObject* pycontext)
    )

cdef int _watcher(PyContextEvent event, PyObject* pycontext):
    if pycontext is NULL or not PyContext_CheckExact(<object>pycontext) or event != PyContextEvent.Py_CONTEXT_SWITCHED:
        return 0

    cdef shared_ptr[RequestContext] ctx
    py_ctx = get_value(_RequestContext)
    if py_ctx is not None and PyCapsule_CheckExact(py_ctx):
        ctx = PyCapsuleToRequestContext(py_ctx)

    # This is always called so we don't leak RC between PyContext switches.
    RequestContext.setContext(ctx)
    return 0


FOLLY_PYTHON_PyContext_AddWatcher(_watcher)


cdef extern from "folly/python/Weak.h":
    PyThreadState* PyGILState_GetThisThreadState() nogil
    int PyGILState_Check() nogil
    int Py_IsFinalizing() nogil
    int Py_IsInitialized() nogil


# Setup a Watcher to set our contextvar any time the folly RequestContext changes
cdef void _setContextWatcher(const shared_ptr[RequestContext]& prev_ctx, const shared_ptr[RequestContext]& curr_ctx) noexcept:
    # Be the most conservative possible here
    if not Py_IsInitialized() or Py_IsFinalizing() or PyGILState_GetThisThreadState() is NULL or not PyGILState_Check():
        # If we don't already have the GIL, we don't bother setting the contextvar
        # Not all calls represent python threads
        return

    py_ctx = get_value(_RequestContext)

    if py_ctx is None and curr_ctx.get() is NULL:
        # We don't have a contextvar set and we don't have a context, so we don't need to do anything
        return

    if py_ctx is not None and PyCapsule_CheckExact(py_ctx):
        if PyCapsuleToRequestContext(py_ctx).get() == curr_ctx.get():
            # We triggered this change in our context watcher, so we don't need to do anything
            return

    if curr_ctx.get() is NULL:
        # This should be marginally faster than creating a new capsule
        PyContextVar_Set(_RequestContext, None)
    else:
        set_PyContext(RequestContext.saveContext())


RequestContext.addSetContextWatcher(_setContextWatcher)
