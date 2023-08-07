/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <folly/python/iobuf_ext.h>

#if PY_VERSION_HEX < 0x03040000
#define FOLLY_PYHON_PyGILState_Check() (true)
#else
#define FOLLY_PYHON_PyGILState_Check() PyGILState_Check()
#endif

namespace folly::python {

namespace {

struct PyBufferData {
  folly::Executor* executor;
  PyObject* py_object;
};

} // namespace

std::unique_ptr<folly::IOBuf> iobuf_from_memoryview(
    folly::Executor* executor,
    PyObject* py_object,
    void* buf,
    uint64_t length) {
  Py_INCREF(py_object);
  auto* pyUserData = new PyBufferData();
  pyUserData->executor = executor;
  pyUserData->py_object = py_object;

  return folly::IOBuf::takeOwnership(
      buf,
      length,
      [](void* /* buf */, void* userData) {
        auto* py_data = (PyBufferData*)userData;
        auto* pyObject = py_data->py_object;
        if (FOLLY_PYHON_PyGILState_Check()) {
          Py_DECREF(pyObject);
        } else if (py_data->executor) {
          py_data->executor->add([pyObject]() mutable { Py_DECREF(pyObject); });
        } else {
          /*
            This is the last ditch effort. We don't have the GIL and we have no
            asyncio executor.  In this case we will attempt to use the
            pendingCall interface to cpython.  This is likely to fail under
            heavy load due to lock contention.
          */
          int ret = Py_AddPendingCall(
              [](void* userData1) {
                Py_DECREF((PyObject*)userData1);
                return 0;
              },
              (void*)pyObject);
          if (ret != 0) {
            LOG(ERROR)
                << "an IOBuf was created from a non-asyncio thread, and all attempts "
                << "to free the underlying buffer has failed, memory has leaked!";
          } else {
            LOG(WARNING)
                << "an IOBuf was created from a non-asyncio thread, and we successful "
                << "handled cleanup but this is not a reliable interface, it will fail "
                << "under heavy load, do not create IOBufs from non-asyncio threads. ";
          }
        }
        delete py_data;
      },
      pyUserData);
}

} // namespace folly::python
