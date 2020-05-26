/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

/*
 *  This file serves as a helper for bridging folly::coro::Task and python
 *  asyncio.future.
 */

#pragma once

#include <Python.h>
#include <folly/Executor.h>
#include <folly/Portability.h>

#if FOLLY_HAS_COROUTINES

#include <folly/experimental/coro/Task.h>
#include <folly/python/AsyncioExecutor.h>
#include <folly/python/executor.h>

namespace folly {
namespace python {

template <typename T>
void bridgeCoroTask(
    folly::Executor* executor,
    folly::coro::Task<T>&& coroFrom,
    folly::Function<void(folly::Try<T>&&, PyObject*)> callback,
    PyObject* userData) {
  // We are handing over a pointer to a python object to c++ and need
  // to make sure it isn't removed by python in that time.
  Py_INCREF(userData);
  auto guard = folly::makeGuard([=] { Py_DECREF(userData); });
  std::move(coroFrom).scheduleOn(executor).start(
      [callback = std::move(callback), userData, guard = std::move(guard)](
          folly::Try<T>&& result) mutable {
        callback(std::move(result), userData);
      });
}

template <typename T>
void bridgeCoroTask(
    folly::coro::Task<T>&& coroFrom,
    folly::Function<void(folly::Try<T>&&, PyObject*)> callback,
    PyObject* userData) {
  bridgeCoroTask(
      getExecutor(), std::move(coroFrom), std::move(callback), userData);
}

} // namespace python
} // namespace folly

#endif
