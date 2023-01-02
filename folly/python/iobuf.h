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

#include <folly/Executor.h>
#include <folly/io/IOBuf.h>
#include <folly/python/iobuf_ext.h>

namespace folly::python {

// Returns a copy of the C++ IOBuf underlying a Python IOBuf object. In
// practice, this makes it possible to pass Python IOBuf objects across the
// Python/C++ boundary in pybind and other non-Cython extension code.
folly::IOBuf iobuf_from_python_iobuf(PyObject* iobuf);

inline bool check_iobuf_equal(const folly::IOBuf* a, const folly::IOBuf* b) {
  return folly::IOBufEqualTo{}(a, b);
}

inline bool check_iobuf_less(const folly::IOBuf* a, const folly::IOBuf* b) {
  return folly::IOBufLess{}(a, b);
}

} // namespace folly::python
