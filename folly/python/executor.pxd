from libcpp.memory cimport unique_ptr
from folly cimport cFollyExecutor

cdef extern from "folly/executors/NotificationQueueExecutor.h" namespace "folly":
    cdef cppclass cNotificationQueueExecutor "folly::NotificationQueueExecutor"(cFollyExecutor):
        int fileno()
        void drive()

cdef class NotificationQueueExecutor:
    cdef unique_ptr[cNotificationQueueExecutor] cQ

cdef api cFollyExecutor* get_executor()
