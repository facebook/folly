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

from builtins import memoryview as py_memoryview
from folly.executor cimport get_running_executor
from cpython cimport Py_buffer
from weakref import WeakValueDictionary
from cpython.object cimport Py_LT, Py_LE, Py_EQ, Py_NE, Py_GT, Py_GE
from cython.operator cimport dereference as deref

__cache = WeakValueDictionary()
__all__ = ['IOBuf']


cdef unique_ptr[cIOBuf] from_python_buffer(memoryview view):
    """Take a python object that supports buffer protocol"""
    if not view.is_c_contig() and not view.is_f_contig():
        raise ValueError("View must be contiguous")
    return move(
        iobuf_from_memoryview(
            get_running_executor(True),
            <PyObject*>view,
            view.view.buf,
            view.view.len,
        )
    )

cdef IOBuf from_unique_ptr(unique_ptr[cIOBuf] ciobuf):
    inst = <IOBuf>IOBuf.__new__(IOBuf)
    inst._ours = move(ciobuf)
    inst._parent = None
    inst._this = inst._ours.get()
    __cache[(<unsigned long>inst._this, id(inst))] = inst
    return inst

cdef api object python_iobuf_from_ptr(unique_ptr[cIOBuf] iobuf):
    return from_unique_ptr(move(iobuf))

cdef cIOBuf from_python_iobuf(object obj) except *:
    return deref((<IOBuf?>obj).c_clone())

cdef cIOBuf* ptr_from_python_iobuf(object obj) except NULL:
    return (<IOBuf?>obj).c_clone().release()


cdef class IOBuf:
    def __init__(self, buffer not None):
        cdef memoryview view = memoryview(buffer, PyBUF_C_CONTIGUOUS)
        self._ours = move(from_python_buffer(view))
        self._this = self._ours.get()
        self._parent = None
        self._hash = None
        __cache[(<unsigned long>self._this, id(self))] = self

    def __dealloc__(self):
        self._ours.reset()

    @staticmethod
    cdef IOBuf create(cIOBuf* this, object parent):
        key = (<unsigned long>this, id(parent))
        cdef IOBuf inst = __cache.get(key)
        if inst is None:
            inst = <IOBuf>IOBuf.__new__(IOBuf)
            inst._this = this
            inst._parent = parent
            __cache[key] = inst
        return inst

    cdef void cleanup(self):
        self._ours.reset()

    cdef unique_ptr[cIOBuf] c_clone(self) noexcept:
        return move(self._this.clone())

    def clone(self):
        """ Clone the iobuf chain """
        return from_unique_ptr(self._this.clone())

    @property
    def next(self):
        _next = self._this.next()
        if _next == self._this:
            return None

        return IOBuf.create(_next, self if self._parent is None else self._parent)

    @property
    def prev(self):
        _prev = self._this.prev()
        if _prev == self._this:
            return None

        return IOBuf.create(_prev, self if self._parent is None else self._parent)

    @property
    def is_chained(self):
        return self._this.isChained()

    def chain_size(self):
        return self._this.computeChainDataLength()

    def chain_count(self):
        return self._this.countChainElements()

    def __bytes__(self):
        return <bytes>self._this.data()[:self._this.length()]

    def __bool__(self):
        return not self._this.empty()

    def __getbuffer__(self, Py_buffer *buffer, int flags):
        self.shape[0] = self._this.length()
        self.strides[0] = 1

        buffer.buf = <void *>self._this.data()
        buffer.format = NULL
        buffer.internal = NULL
        buffer.itemsize = 1
        buffer.len = self.shape[0]
        buffer.ndim = 1
        buffer.obj = self
        buffer.readonly = 1
        buffer.shape = self.shape
        buffer.strides = self.strides
        buffer.suboffsets = NULL

    def __releasebuffer__(self, Py_buffer *buffer):
        # Read-only means we need no logic here
        pass

    def __len__(self):
        return self._this.length()

    def __iter__(self):
        "Iterates through the chain of buffers returning a memory view for each"
        yield py_memoryview(self)
        next = self.next
        while next is not None and next is not self:
            yield py_memoryview(next)
            next = next.next

    def __hash__(self):
        if not self._hash:
            self._hash = hash(b''.join(self))
        return self._hash

    def __richcmp__(self, other, op):
        cdef int cop = op
        if not (isinstance(self, IOBuf) and isinstance(other, IOBuf)):
            if cop == Py_EQ:  # different types are never equal
                return False
            elif cop == Py_NE:  # different types are always notequal
                return True
            else:
                return NotImplemented

        cdef cIOBuf* othis = (<IOBuf>other)._this
        if cop == Py_EQ:
            return check_iobuf_equal(self._this, othis)
        elif cop == Py_NE:
            return not(check_iobuf_equal(self._this, othis))
        elif cop == Py_LT:
            return check_iobuf_less(self._this, othis)
        elif cop == Py_LE:
            return not(check_iobuf_less(othis, self._this))
        elif cop == Py_GT:
            return check_iobuf_less(othis, self._this)
        elif cop == Py_GE:
            return not(check_iobuf_less(self._this, othis))
        else:
            return NotImplemented
