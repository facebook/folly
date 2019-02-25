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
