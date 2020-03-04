# distutils: language = c++

from libcpp.string cimport string
from libc.stdint cimport uint64_t
from libcpp.memory cimport unique_ptr
from libc.string cimport const_uchar
from folly cimport cFollyExecutor
from cpython.ref cimport PyObject
from cython.view cimport memoryview

cdef extern from "folly/io/IOBuf.h" namespace "folly":
    cdef cppclass cIOBuf "folly::IOBuf":
        uint64_t length()
        const_uchar* data()
        bint empty()
        bint isChained()
        size_t countChainElements()
        uint64_t computeChainDataLength()
        unique_ptr[cIOBuf] clone()
        cIOBuf* prev()
        cIOBuf* next()
        void appendChain(unique_ptr[cIOBuf]&& ciobuf)


cdef extern from "folly/io/IOBuf.h" namespace "folly::IOBuf":
    unique_ptr[cIOBuf] wrapBuffer(const_uchar* buf, uint64_t capacity)
    unique_ptr[cIOBuf] createChain(size_t totalCapacity, size_t maxBufCapacity)


cdef extern from "folly/io/IOBufQueue.h" namespace "folly::IOBufQueue":
    cdef cppclass cIOBufQueueOptions "folly::IOBufQueue::Options":
        pass
    cIOBufQueueOptions cacheChainLength()


cdef extern from "folly/io/IOBufQueue.h" namespace "folly":
    cdef cppclass cIOBufQueue "folly::IOBufQueue":
        cIOBufQueue(cIOBufQueueOptions)
        cIOBufQueue()
        unique_ptr[cIOBuf] move()
        void append(unique_ptr[cIOBuf]&& buf)


cdef extern from '<utility>' namespace 'std':
    unique_ptr[cIOBuf] move(unique_ptr[cIOBuf])


cdef extern from "folly/python/iobuf.h" namespace "folly":
    unique_ptr[cIOBuf] iobuf_from_python(cFollyExecutor*, PyObject*, void*, uint64_t)
    bint check_iobuf_equal(cIOBuf*, cIOBuf*)
    bint check_iobuf_less(cIOBuf*, cIOBuf*)

cdef extern from "Python.h":
    cdef int PyBUF_C_CONTIGUOUS


cdef class IOBuf:
    cdef object __weakref__
    cdef cIOBuf* _this
    cdef object _parent
    cdef object _hash
    cdef unique_ptr[cIOBuf] _ours
    cdef Py_ssize_t shape[1]
    cdef Py_ssize_t strides[1]
    @staticmethod
    cdef IOBuf create(cIOBuf* this, object parent)
    cdef void cleanup(self)
    cdef unique_ptr[cIOBuf] c_clone(self)

cdef unique_ptr[cIOBuf] from_python_buffer(memoryview view)
cdef IOBuf from_unique_ptr(unique_ptr[cIOBuf] iobuf)
