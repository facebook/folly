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

#pragma once

#include <folly/python/Weak.h>

namespace folly::python {

/**
 * RAII helper to release the Python GIL for the duration of a scope.
 * Only releases if Python is linked, initialized, and the current thread holds
 * the GIL. Safe to use in code that may or may not be called from Python.
 *
 * Example usage:
 *   void expensiveBlockingOperation() {
 *     ScopedGILRelease gilRelease;
 *     // GIL is released here, allowing other Python threads to run
 *     doBlockingIO();
 *   } // GIL is automatically reacquired when gilRelease goes out of scope
 */
class ScopedGILRelease {
 public:
  ScopedGILRelease()
      : tState_(
            (isLinked() && (Py_IsInitialized() != 0) &&
             (PyGILState_Check() != 0))
                ? PyEval_SaveThread()
                : nullptr) {}

  ~ScopedGILRelease() {
    if (tState_) {
      PyEval_RestoreThread(tState_);
    }
  }

  ScopedGILRelease(const ScopedGILRelease&) = delete;
  ScopedGILRelease& operator=(const ScopedGILRelease&) = delete;
  ScopedGILRelease(ScopedGILRelease&&) = delete;
  ScopedGILRelease& operator=(ScopedGILRelease&&) = delete;

 private:
  PyThreadState* tState_;
};

} // namespace folly::python
