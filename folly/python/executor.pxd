from libcpp.memory cimport unique_ptr
from folly cimport cFollyExecutor

cdef extern from "folly/python/NotificationQueueExecutor.h" namespace "folly::python":
    cdef cppclass cNotificationQueueExecutor "folly::python::NotificationQueueExecutor"(cFollyExecutor):
        int fileno()
        void drive()

cdef class NotificationQueueExecutor:
    cdef unique_ptr[cNotificationQueueExecutor] cQ

cdef api cFollyExecutor* get_executor()
