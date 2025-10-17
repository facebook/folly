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

import os
import sys
cimport cython
from contextlib import contextmanager
from libcpp.memory cimport make_shared
from cpython.object cimport PyObject
from cpython.contextvars cimport get_value_no_default as get_value, PyContextVar_Set, PyContextVar_Reset, PyContextVar_New, PyContext_CheckExact
from cpython.pystate cimport PyThreadState
from libcpp.utility cimport move

# There is at least 1 platform that doesn't have a recent enough build of meta python, this is the escape hatch for segfaults.
DISABLE_MODULE = os.environ.get("DISABLE_FOLLY_PYTHON_REQUEST_CONTEXT", False)

# Don't store in module dict, limits control surfaces for how it can be set to this module alone.
cdef object _RequestContext = PyContextVar_New("_RequestContext", NULL)

cdef extern from * namespace "folly::python::request_context" nogil:
    """
       namespace folly::python::request_context {
           thread_local unsigned long _last_set_context;
           thread_local bool _last_context_set = false;
           inline unsigned long get_last_set_context() {
               return _last_set_context;
           }
           inline void set_last_set_context(unsigned long value) {
               _last_set_context = value;
               _last_context_set = true;
           }
           inline bool is_last_context_set() {
               return _last_context_set;
           }
           inline void reset_last_context_set() {
               _last_context_set = false;
           }
       }
    """
    cdef unsigned long get_last_set_context()
    cdef void set_last_set_context(unsigned long value)
    cdef bint is_last_context_set()
    cdef void reset_last_context_set()

cdef object set_PyContext(shared_ptr[RequestContext] ptr) except *:
    return PyContextVar_Set(_RequestContext, RequestContextToPyCapsule(move(ptr)))

cdef object get_PyContext(object context = None) except *:
    """Return the PyCapsule from the ContextVar"""
    if context is None:
        return get_value(_RequestContext)

    if not PyContext_CheckExact(context):
        raise TypeError(f"{context!r} is not a PyContext object!")

    return context.get(_RequestContext)


@cython.auto_pickle(False)
cdef class Context:
    """ Python Wrapper around shared_ptr[folly::RequestContext] """
    # This is opague to other cython files
    cdef shared_ptr[RequestContext] _ptr

    def __eq__(self, Context other):
        return self._ptr.get() == other._ptr.get()

    def __hash__(self):
        return <unsigned long>(self._ptr.get())

    def __repr__(self):
        return f"<Python Capsule shared_ptr<folly::RequestContext>: {<unsigned long>self._ptr.get()}>"

    def __bool__(self):
        return self._ptr.get() != NULL

    def use_count(self):
        return self._ptr.use_count()


cdef object RequestContextToPyCapsule(shared_ptr[RequestContext] ptr) noexcept:
    cdef Context holder = Context.__new__(Context)
    holder._ptr = move(ptr)
    return holder

cdef shared_ptr[RequestContext] PyCapsuleToRequestContext(object capsule) noexcept:
    cdef Context holder = <Context>capsule
    return holder._ptr

cdef bint isRequestContextPyCapsule(object capsule) noexcept:
    return isinstance(capsule, Context)


cpdef object save() noexcept:
    """ Return the current setcontext """
    cdef Context ctx = Context.__new__(Context)
    ctx._ptr = move(RequestContext.saveContext())
    return ctx


cpdef object get_from_contextvar() noexcept:
    """ Return the current context from the contextvar """
    cdef Context ctx = Context.__new__(Context)

    ctx_var = get_value(_RequestContext)
    if isinstance(ctx_var, Context):
        return ctx_var
    return ctx


@contextmanager
def active():
    """ Create a Context and shove it into the python contextvar """
    cdef shared_ptr[RequestContext] rctx = make_shared[RequestContext]()
    prev_rctx = RequestContext.setContext(rctx)
    cdef Context ctx = get_from_contextvar()
    assert ctx._ptr == rctx
    rctx.reset() # So we have correct use_count for tests
    try:
        yield ctx
    finally:
        assert RequestContext.setContext(move(prev_rctx)) == ctx._ptr


cdef extern from "folly/python/request_context.h":
    ctypedef enum PyContextEvent:
        Py_CONTEXT_SWITCHED = 1

    cdef int FOLLY_PYTHON_PyContext_AddWatcher(
        int(*PyContext_WatchCallback)(PyContextEvent, PyObject* pycontext)
    )


cdef int _watcher(PyContextEvent event, PyObject* pycontext):
    cdef shared_ptr[RequestContext] ctx

    if pycontext is NULL or event != PyContextEvent.Py_CONTEXT_SWITCHED:
        return 0

    # The context is None lets unset the fRC
    if (<object>pycontext) is None:
        set_last_set_context(0)
        RequestContext.setContext(ctx)

    if not PyContext_CheckExact(<object>pycontext):
        reset_last_context_set()
        return 0

    py_ctx = get_value(_RequestContext)
    if isinstance(py_ctx, Context):
        ctx = PyCapsuleToRequestContext(py_ctx)

    # This is always called so we don't leak RC between PyContext switches.
    set_last_set_context(<unsigned long>ctx.get())
    RequestContext.setContext(move(ctx))
    return 0


if sys.version_info >= (3, 14) or "+meta" in sys.version or "+fb" in sys.version or "+cinder" in sys.version:
    if not DISABLE_MODULE:
        FOLLY_PYTHON_PyContext_AddWatcher(_watcher)


cdef extern from "folly/python/Weak.h":
    PyThreadState* PyGILState_GetThisThreadState() nogil
    int PyGILState_Check() nogil
    int Py_IsFinalizing() nogil
    int Py_IsInitialized() nogil


# Setup a Watcher to set our contextvar any time the folly RequestContext changes
cdef void _setContextWatcher(const shared_ptr[RequestContext]& prev_ctx, const shared_ptr[RequestContext]& curr_ctx) noexcept:

    if is_last_context_set() and get_last_set_context() == <unsigned long>curr_ctx.get():
        # We already set the contextvar, so we don't need to do anything
        return

    # Be the most conservative possible here
    if not Py_IsInitialized() or Py_IsFinalizing() or PyGILState_GetThisThreadState() is NULL or not PyGILState_Check():
        # If we don't already have the GIL, we don't bother setting the contextvar
        # Not all calls represent python threads
        return

    set_last_set_context(<unsigned long>curr_ctx.get())
    if curr_ctx.get() is NULL:
        # This should be marginally faster than creating a new capsule
        PyContextVar_Set(_RequestContext, None)
    else:
        set_PyContext(RequestContext.saveContext())

if not DISABLE_MODULE:
    RequestContext.addSetContextWatcher(_setContextWatcher)
