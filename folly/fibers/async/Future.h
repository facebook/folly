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

#include <folly/fibers/async/Async.h>
#include <folly/futures/Future.h>

namespace folly {
namespace fibers {
namespace async {

/**
 * Async tagged helpers to perform blocking future operations. Ensures that
 * functions that block on futures are annotated as well.
 */
template <typename T>
Async<T> futureWait(SemiFuture<T>&& semi) {
  // Any deferred work will be executed inline on main-context
  return std::move(semi).get();
}

template <typename T>
Async<T> futureWait(Future<T>&& fut) {
  return std::move(fut).get();
}

} // namespace async
} // namespace fibers
} // namespace folly
