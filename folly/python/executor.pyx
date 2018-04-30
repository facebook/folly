import asyncio
from folly cimport cFollyExecutor
from folly.executor cimport cAsyncioExecutor
from libcpp.memory cimport make_unique, unique_ptr
from cython.operator cimport dereference as deref
from weakref import WeakKeyDictionary

#asynico Loops to AsyncioExecutor
loop_to_q = WeakKeyDictionary()


cdef class AsyncioExecutor:
   def __cinit__(self):
       self.cQ = make_unique[cAsyncioExecutor]();

   def fileno(AsyncioExecutor self):
       return deref(self.cQ).fileno()

   def drive(AsyncioExecutor self):
       deref(self.cQ).drive()

   def __dealloc__(AsyncioExecutor self):
       # We drive it one last time
       deref(self.cQ).drive()


cdef cFollyExecutor* get_executor():
   loop = asyncio.get_event_loop()
   try:
       Q = <AsyncioExecutor>(loop_to_q[loop])
   except KeyError:
       Q = AsyncioExecutor()
       loop.add_reader(Q.fileno(), Q.drive)
       loop_to_q[loop] = Q
   return Q.cQ.get()
