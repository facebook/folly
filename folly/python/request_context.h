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

#include <Python.h>

#if PY_VERSION_HEX < 0x030e0000 // < 3.14
/*
 *  PyContext_AddWatcher was backported to Meta Builds of Python on Linux
 *       -DFOLLY_PYTHON_META_BUILD=1
 */

extern "C" {
#if defined(__linux__) && FOLLY_PYTHON_META_BUILD
// workaround local issue of missing 3.10 symbols
__attribute__((weak)) extern int PyContext_AddWatcher(
    PyContext_WatchCallback callback);
#endif

#ifndef FOLLY_PYTHON_META_BUILD
/*
 *  For those non-meta builds before 3.14 provide the basics
 */

typedef enum {
  Py_CONTEXT_SWITCHED = 1,
} PyContextEvent;

typedef int (*PyContext_WatchCallback)(PyContextEvent, PyObject*);

#endif

int FOLLY_PYTHON_PyContext_AddWatcher(PyContext_WatchCallback callback) {
#if defined(__linux__) && FOLLY_PYTHON_META_BUILD
  return PyContext_AddWatcher(callback);
#else
  return 1;
#endif
}

} // extern "C"

#else // >= python 3.14
#define FOLLY_PYTHON_PyContext_AddWatcher PyContext_AddWatcher
#endif
