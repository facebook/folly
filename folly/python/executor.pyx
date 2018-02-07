import asyncio
from folly cimport cFollyExecutor
from folly.executor cimport cNotificationQueueExecutor
from libcpp.memory cimport make_unique, unique_ptr
from cython.operator cimport dereference as deref
from weakref import WeakKeyDictionary

#asynico Loops to NotificationQueueExecutor
loop_to_q = WeakKeyDictionary()


cdef class NotificationQueueExecutor:
   def __cinit__(self):
       self.cQ = make_unique[cNotificationQueueExecutor]();

   def fileno(NotificationQueueExecutor self):
       return deref(self.cQ).fileno()

   def drive(NotificationQueueExecutor self):
       deref(self.cQ).drive()

   def __dealloc__(NotificationQueueExecutor self):
       # We drive it one last time
       deref(self.cQ).drive()


cdef cFollyExecutor* get_executor():
   loop = asyncio.get_event_loop()
   try:
       Q = <NotificationQueueExecutor>(loop_to_q[loop])
   except KeyError:
       Q = NotificationQueueExecutor()
       loop.add_reader(Q.fileno(), Q.drive)
       loop_to_q[loop] = Q
   return Q.cQ.get()
