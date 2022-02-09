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

#include <stdexcept>

#include <folly/python/executor_api.h> // @manual

namespace folly {
namespace python {

namespace {

void ensure_imported() {
  static bool imported = false;
  if (!imported) {
    if (0 != import_folly__executor()) {
      throw std::runtime_error("import_folly__executor failed");
    }
    imported = true;
  }
}

} // namespace

folly::Executor* getExecutor() {
  ensure_imported();
  return get_running_executor(false); // TODO: fried set this to true
}

int setExecutorForLoop(PyObject* loop, AsyncioExecutor* executor) {
  ensure_imported();
  return set_executor_for_loop(loop, executor);
}

} // namespace python
} // namespace folly
