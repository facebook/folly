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

#include <coroutine>

#include <folly/coro/Error.h>

/// Use `co_ready` to "await" synchronous coroutine types from inside async
/// coroutines like `coro::Task`. For example:
///
///   result<int> getN();
///   int n = co_await co_ready(getN());
///
/// Also see `co_await_result` (`AwaitResult.h`) and `co_result` (`Result.h`).
///
/// If you need to optimize away ALL exception throwing in **async** code,
/// `co_ready` is not your top choice.  In a `Task` coro:
///
///   auto v = co_await co_nothrow(asyncMayError()); // best practice
///   auto v = co_await co_ready(co_await_result(asyncMayError())); // too long
///
/// However, when you are calling synchronous `result` functions, or need to
/// efficiently handle **some** async errors, `co_ready` is your friend:
///
///   auto res = syncResultFn(); // or `co_await co_await_result(asyncFn())`
///   if (auto* ex = get_exception<MyError>(res)) {
///     /* handle ex */
///   } else {
///     auto v = co_await co_ready(std::move(res)); // propagate unhandled
///   }
///
/// This pattern has a few good properties:
///   - Easy error handling -- extracts the value from its argument, or
///     short-circuit any error to current coro's awaiter.
///   - Unlike `catch (const std::exception& ex)`, won't catch (and therefore
///     break) cancellation.
///   - The error path is MUCH more efficient (3-30 nanoseconds) than
///     `value_or_throw()` (1 microsecond).
///
/// We don't support `co_await syncResultFn()` to avoids confusion about which
/// parts of the code are sync vs async.  The distinction is critical, since
/// one must not hold non-coro mutexes across async suspend points.
///
/// Future:
///   - Adding `std::ref` / `std::cref`, and possibly `folly::rref` variants of
///     this (as in `result.h`) might improve performance in hot code.
///   - The current implementation is `result`-only.  If you have a need, it
///     would be fine to add the analogous specialization for `Try`.  Just be
///     mindful of its two warts: empty state and empty `exception_wrapper`.

namespace folly {
class Executor;
template <typename>
class ExecutorKeepAlive;
template <typename>
class result;
} // namespace folly

namespace folly::coro {

namespace detail {
struct WithAsyncStackFunction;
}

template <typename>
class co_ready;

#if FOLLY_HAS_RESULT

template <typename T>
class co_ready<result<T>> {
 private:
  result<T> res_;

 public:
  explicit co_ready(result<T>&& res) : res_(std::move(res)) {}

  bool await_ready() const noexcept { return res_.has_value(); }

  auto await_resume() noexcept -> decltype(std::move(res_).value_or_throw()) {
    return std::move(res_).value_or_throw();
  }

  template <typename Promise>
  auto await_suspend(
      std::coroutine_handle<Promise> awaitingCoroutine) noexcept {
    auto& promise = awaitingCoroutine.promise();
    // We have to use the legacy API because (1) `folly::coro` internals still
    // model cancellation as an exception, (2) to use `co_cancelled` here we'd
    // have to check `res_` for `OperationCancelled` which can cost 50-100ns+.
    auto awaiter = promise.yield_value(co_error(
        std::move(res_).non_value().get_legacy_error_or_cancellation()));
    return awaiter.await_suspend(awaitingCoroutine);
  }

  friend auto co_viaIfAsync(
      const ExecutorKeepAlive<Executor>&, co_ready&& r) noexcept {
    return std::move(r);
  }

  // Conventionally, the first arg would be `cpo_t<co_withAsyncStack>`, but
  // that cannot be forward-declared.
  friend auto tag_invoke(
      const detail::WithAsyncStackFunction&, co_ready&& r) noexcept {
    return std::move(r);
  }
};

template <typename T>
co_ready(result<T>&&) -> co_ready<result<T>>;

#endif // FOLLY_HAS_RESULT

} // namespace folly::coro
