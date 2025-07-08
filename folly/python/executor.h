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

#include <folly/Executor.h>
#include <folly/python/AsyncioExecutor.h>
#include <folly/python/Weak.h>

#ifdef FOLLY_PYTHON_WIN_SHAREDLIB
#ifdef FOLLY_PYTHON_EXECUTOR_DETAIL_DEFS
#define FOLLY_PYTHON_EXECUTOR_API __declspec(dllexport)
#else
#define FOLLY_PYTHON_EXECUTOR_API __declspec(dllimport)
#endif
#else
#define FOLLY_PYTHON_EXECUTOR_API
#endif

namespace folly {
namespace python {

namespace executor_detail {
void FOLLY_PYTHON_EXECUTOR_API
assign_funcs(AsyncioExecutor* (*)(int), int (*)(PyObject*, AsyncioExecutor*));
} // namespace executor_detail

FOLLY_PYTHON_EXECUTOR_API folly::Executor* getExecutor();

// Returns -1 if an executor was already set for loop, 0 otherwise. A NULL
// executor clears the current executor (caller is responsible for freeing
// any existing executor).
FOLLY_PYTHON_EXECUTOR_API int setExecutorForLoop(
    PyObject* loop, AsyncioExecutor* executor);

} // namespace python
} // namespace folly
