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

#include <exception>

namespace folly {

/// IMPORTANT: `folly`-internal, do NOT use this in new user code. Instead:
///
///   - `co_yield coro::co_cancelled` to signal that a coro was cancelled.
///
///   - To check for cancellation in `folly::coro` coroutines, use one of:
///       // (1) default behavior
///       co_await coro::co_safe_point
///       // (2) custom behavior
///       auto& ctok = co_await coro::co_current_cancellation_token;
///       if (ctok.isCancellationRequested()) {
///         /* ... do stuff ... */
///         co_yield coro::co_cancelled;
///       }
///
///   - Store `stopped_result` to obtain a `result<T>` or `non_value_result`
///     in a stopped state. To check for it, use `res.has_stopped()`.
///
///   - To avoid depending on `OperationCancelled` in code that would do
///     `try-catch` in `folly::coro` coroutines, do this instead:
///
///       auto res = co_await coro::co_await_result(mightGetCancelled());
///       if (auto* ex = get_exception<MyErr>(res)) {
///         // HANDLE ERROR HERE
///       } else if (res.has_stopped()) {
///         // HANDLE CANCELLATION HERE
///         co_yield coro::co_cancelled;
///       } else { // get value, or propagate unhandled errors/cancellation
///         auto v = co_await coro::co_ready(std::move(res));
///       }
///
/// Rationale / purpose: For now, `folly` uses the `OperationCancelled`
/// exception to signal "this work was stopped".  However, as of C++26 (see
/// P2300, plus arguments in P1677), standard C++ differentiates between
/// "value", "error", and "stopped" completions.  Therefore, we ask end-user
/// code to use the above cancellation-specific constructs, WITHOUT assuming
/// that cancellation / stopping is implemented as an exception.
struct OperationCancelled final : public std::exception {
  const char* what() const noexcept override {
    return "coroutine operation cancelled";
  }
};

} // namespace folly
