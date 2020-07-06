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

#pragma once

#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Task.h>
#include <folly/fibers/async/Async.h>

namespace folly {
namespace fibers {
namespace async {

/**
 * Block on a task's execution. Should be called from an Async annotated
 * function. The fiber executing task_wait will block while the task is
 * suspended, and the task's work will be executed inline on the fiber main
 * context.
 */
template <typename T>
Async<T> taskWait(folly::coro::Task<T>&& task) {
  return folly::coro::blockingWait(std::move(task));
}

inline Async<void> taskWait(folly::coro::Task<void>&& task) {
  folly::coro::blockingWait(std::move(task));
  return {};
}

} // namespace async
} // namespace fibers
} // namespace folly
