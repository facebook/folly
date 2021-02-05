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

#include <folly/experimental/coro/Coroutine.h>
#include <folly/experimental/coro/Invoke.h>
#include <folly/experimental/coro/Task.h>
#include <folly/experimental/coro/Traits.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

// Converts the given SemiAwaitable to a Task (without starting it)
template <typename SemiAwaitable>
Task<semi_await_result_t<SemiAwaitable>> toTask(SemiAwaitable&& a) {
  return co_invoke(
      [a = std::forward<SemiAwaitable>(
           a)]() mutable -> Task<semi_await_result_t<SemiAwaitable>> {
        co_return co_await std::forward<SemiAwaitable>(a);
      });
}
template <typename SemiAwaitable>
Task<semi_await_result_t<SemiAwaitable>> toTask(
    std::reference_wrapper<SemiAwaitable> a) {
  return co_invoke(
      [a = std::move(a)]() mutable -> Task<semi_await_result_t<SemiAwaitable>> {
        co_return co_await a.get();
      });
}
inline Task<void> toTask(Future<Unit> a) {
  return co_invoke([a = std::move(a)]() mutable -> Task<void> {
    co_yield co_result(co_await co_awaitTry(std::move(a)));
  });
}
inline Task<void> toTask(SemiFuture<Unit> a) {
  return co_invoke([a = std::move(a)]() mutable -> Task<void> {
    co_yield co_result(co_await co_awaitTry(std::move(a)));
  });
}

// Converts the given SemiAwaitable to a SemiFuture (without starting it)
template <typename SemiAwaitable>
SemiFuture<
    lift_unit_t<semi_await_result_t<remove_reference_wrapper_t<SemiAwaitable>>>>
toSemiFuture(SemiAwaitable&& a) {
  return toTask(std::forward<SemiAwaitable>(a)).semi();
}

// Converts the given SemiAwaitable to a Future, starting it on the Executor
template <typename SemiAwaitable>
Future<
    lift_unit_t<semi_await_result_t<remove_reference_wrapper_t<SemiAwaitable>>>>
toFuture(SemiAwaitable&& a, Executor::KeepAlive<> ex) {
  return toTask(std::forward<SemiAwaitable>(a)).scheduleOn(ex).start().via(ex);
}

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
