import asyncio
from libcpp.memory cimport unique_ptr
from folly.executor cimport get_executor
from folly.fiber_manager cimport cFiberManager, cLoopController, cAsyncioLoopController
from weakref import WeakKeyDictionary

#asynico Loops to FiberManager
loop_to_controller = WeakKeyDictionary()


cdef class FiberManager:
    def __cinit__(self):
        self.cManager.reset(new cFiberManager(
            unique_ptr[cLoopController](new cAsyncioLoopController(
                get_executor()))));


cdef cFiberManager* get_fiber_manager():
   loop = asyncio.get_event_loop()
   try:
       manager = <FiberManager>(loop_to_controller[loop])
   except KeyError:
       manager = FiberManager()
       loop_to_controller[loop] = manager
   return manager.cManager.get()
