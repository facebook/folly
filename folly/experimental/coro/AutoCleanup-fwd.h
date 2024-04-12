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

#include <folly/experimental/coro/Cleanup.h>
#include <folly/experimental/coro/Coroutine.h>

#if FOLLY_HAS_COROUTINES

namespace folly::coro {

template <typename T, typename CleanupFn = co_cleanup_fn>
class AutoCleanup;

template <typename T>
struct is_auto_cleanup : std::false_type {};

template <typename T>
constexpr bool is_auto_cleanup_v = is_auto_cleanup<T>::value;

template <typename T, typename CleanupFn>
struct is_auto_cleanup<AutoCleanup<T, CleanupFn>> : std::true_type {};

namespace detail {
template <typename Promise, typename... Args>
void scheduleAutoCleanup(coroutine_handle<Promise> coro, Args&... args);
} // namespace detail

template <typename Promise, typename... Args>
void scheduleAutoCleanupIfNeeded(
    coroutine_handle<Promise> coro, Args&... args) {
  if constexpr ((is_auto_cleanup_v<Args> || ...)) {
    detail::scheduleAutoCleanup(coro, args...);
  }
}

} // namespace folly::coro

#endif
