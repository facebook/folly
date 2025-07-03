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

#include <folly/python/executor.h>
#include <folly/python/import.h>

namespace folly {
namespace python {

namespace executor_detail {
folly::Function<AsyncioExecutor*(bool)> get_running_executor;
folly::Function<int(PyObject*, AsyncioExecutor*)> set_executor_for_loop;
} // namespace executor_detail

int import_folly_executor_impl() {
  // This is exactly what cython does, but in a Weak Friendly Way
  int ret = 0;
  PyObject* mod = nullptr;
  mod = PyImport_ImportModule("folly.executor");
  if (mod == nullptr) { // We failed to import
    ret = -1;
  }
  Py_DecRef(mod);
  return ret;
}

FOLLY_CONSTINIT static import_cache import_folly_executor{
    import_folly_executor_impl, "folly.executor"};

folly::Executor* getExecutor() {
  if (!isLinked()) {
    // Python isn't even linked
    return nullptr;
  }
  import_folly_executor();
  DCHECK(executor_detail::get_running_executor != nullptr);
  return executor_detail::get_running_executor(
      false); // TODO: fried set this to true
}

int setExecutorForLoop(PyObject* loop, AsyncioExecutor* executor) {
  if (!isLinked()) {
    // Python isn't even linked
    return -2;
  }
  import_folly_executor();
  DCHECK(executor_detail::set_executor_for_loop != nullptr);
  return executor_detail::set_executor_for_loop(loop, executor);
}

} // namespace python
} // namespace folly
