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

#include <folly/CppAttributes.h>
#include <folly/coro/Coroutine.h> // delete with clang-15 support
#include <folly/coro/Error.h>
#include <folly/lang/MustUseImmediately.h>
#include <folly/result/detail/result_or_unwind.h>
#include <folly/result/detail/result_promise.h>
#include <folly/result/result.h>

#if FOLLY_HAS_RESULT

// One must never conditionally-include folly headers, but this is required to
// be gated, since `result.h` may get included in C++17 contexts.
#include <coroutine>

/// co_await or_unwind(resultFunc())
///
/// Recommended pre-read: `result.h` docblock and/or `docs/result.md`.
///
/// Use `or_unwind` to unwrap short-circuiting types from synchronous `result`
/// coroutines OR from `folly::coro` async task coroutines:
///
///   result<int> getN();
///   int n = co_await or_unwind(getN());
///
/// Supported types: `result<T>`, `value_only_result<T>`, `non_value_result`.
/// Reference variants (`&`, `const&`, `&&`) bind to the argument; use
/// `or_unwind_owning` when you need to store or return the awaitable.
///
/// If you JUST need a task to cheaply propagate exceptions to its parent,
/// avoiding rethrowing, then you have a more concise option:
///
///   auto v = co_await co_nothrow(asyncMayError()); // best practice
///   // equivalent, but too long
///   auto v = co_await or_unwind(
///       co_await value_or_error_or_stopped(asyncMayError()));
///
/// However, when you are calling synchronous `result` functions, or need to
/// efficiently handle **some** async errors, `or_unwind` is your friend:
///
///   // Or: `res = co_await value_or_error_or_stopped(asyncFn());`
///   auto res = syncResultFn();
///    if (auto ex = get_exception<MyError>(res)) {
///     // handle ex, which quacks like `const MyError*`
///   } else {
///     auto v = co_await or_unwind(std::move(res)); // propagate unhandled
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
/// ## Avoid accidental copies
///
/// Note that if you merely wrote `or_unwind(res)` above, you would be
/// unnecessarily copying the value OR the error (7ns), so mind your move
/// hygiene in hot code.
///
/// ## Avoid dangling references with `or_unwind(rvalue)`
///
/// WARNING: `auto&& ref = co_await or_unwind(resFn())` dangles! `or_unwind`
/// stores a reference to its argument, so when `resFn()` returns a temporary,
/// it dies after the full expression. Use `auto val = ...` (store by value),
/// or `or_unwind_owning`. Search `result.md` for "LLVM issue #177023" for more.
///
/// ## What about cancellation / `has_stopped()`?
///
/// `co_await or_unwind(resFn())` only extracts the value, and unwinds to the
/// parent on either "error" or "stopped" outcomes.
///
/// ## Why not `co_await resultFn()`?
///
/// Making `result` implicitly awaitable would guarantee confusion between
/// async-awaits, and synchronous propagation of non-value outcomes.  The
/// distinction is critical.  For example, one must not hold non-coro mutexes
/// across async suspend points.
///
/// ## Future: Extensions to `Try`?
///
/// It might be fine to specialize `or_unwind` for `Try`.  Just be mindful of
/// its two warts: empty state and empty `exception_wrapper`.

// NOTE: This short-circuiting coroutine implementation was modeled on
// `folly/Expected.h`.  Port compiler fixes or optimizations across both.

namespace folly {

// NB: Making these `final` makes `unsafe_mover` simpler due to no slicing risk.

/// `co_await or_unwind(resFn())` returns a reference into `res`.
template <typename T>
class or_unwind<result<T>&&> final
    : public detail::result_or_unwind<result<T>&&> {
  using detail::result_or_unwind<result<T>&&>::result_or_unwind;
};
template <typename T>
or_unwind(result<T>&&) -> or_unwind<result<T>&&>;

/// `co_await or_unwind(res)` returns a reference into `res`.
template <typename T>
class or_unwind<result<T>&> final
    : public detail::result_or_unwind<result<T>&> {
  using detail::result_or_unwind<result<T>&>::result_or_unwind;
};
template <typename T>
or_unwind(result<T>&) -> or_unwind<result<T>&>;

/// `co_await or_unwind(std::as_const(res))` returns a reference into `res`.
template <typename T>
class or_unwind<const result<T>&> final
    : public detail::result_or_unwind<const result<T>&> {
  using detail::result_or_unwind<const result<T>&>::result_or_unwind;
};
template <typename T>
or_unwind(const result<T>&) -> or_unwind<const result<T>&>;

/// `co_await or_unwind_owning(res)` takes ownership of `res`.
///
/// Use when you need the awaitable to own the result (e.g., when returning
/// from a helper function that composes awaitables).
template <typename T>
class or_unwind_owning<result<T>> final
    : public detail::result_or_unwind_owning<result<T>> {
  using detail::result_or_unwind_owning<result<T>>::result_or_unwind_owning;
};
template <typename T>
or_unwind_owning(result<T>) -> or_unwind_owning<result<T>>;

/// `co_await or_unwind(non_value_result{...})` propagates the error right away.
///
/// IMPORTANT: Unlike `result<T>`, there's no lvalue `or_unwind(nvr&)` variant.
/// If adding one, and add a test akin to the `result<T>` lvalue mutation check
/// in `forEachOrUnwindVariant`.
template <>
class or_unwind<non_value_result&&> final
    : public detail::result_or_unwind<non_value_result&&> {
  using detail::result_or_unwind<non_value_result&&>::result_or_unwind;
};
or_unwind(non_value_result&&) -> or_unwind<non_value_result&&>;

/// co_await or_unwind_owning(non_value_result{...})
/// co_await or_unwind_owning(stopped_result)
/// co_await or_unwind(stopped_result)
///
/// Owning awaitable for `non_value_result`. Unlike the reference variant,
/// this can be returned from functions or stored before awaiting.
template <>
class or_unwind_owning<non_value_result> final
    : public detail::result_or_unwind_owning<non_value_result> {
  using detail::result_or_unwind_owning<
      non_value_result>::result_or_unwind_owning;
};
or_unwind_owning(non_value_result) -> or_unwind_owning<non_value_result>;
or_unwind_owning(stopped_result_t) -> or_unwind_owning<non_value_result>;

// `or_unwind(stopped_result)` must create an owning variant since
// `stopped_result_t` is a tag type, not a reference we can store.
//
// Implementation edge case: Yes, `or_unwind<non_value_result>` is owning,
// despite the lack of `_owning` in the name.  This works out because
// `result_or_unwind_base` selects the storage base using the inner type.
template <>
class or_unwind<non_value_result> final
    : public detail::result_or_unwind<non_value_result> {
  using detail::result_or_unwind<non_value_result>::result_or_unwind;
};
or_unwind(stopped_result_t) -> or_unwind<non_value_result>;

//
// NOTE: We may remove support for `co_await non_value_result{...}` and
// `co_await stopped_result`, instead requiring `or_unwind`.
//

namespace detail {

struct result_non_value_awaitable {
  non_value_result non_value_;

  constexpr std::false_type await_ready() const noexcept { return {}; }
  [[noreturn]] void await_resume() {
    compiler_may_unsafely_assume_unreachable();
  }
  template <typename U>
  FOLLY_ALWAYS_INLINE void await_suspend(result_promise_handle<U> h) {
    auto& v = *h.promise().value_;
    expected_detail::ExpectedHelper::assume_empty(v.exp_);
    v.exp_ = Unexpected{std::move(non_value_)};
    h.destroy();
  }
};
} // namespace detail

/// co_await stopped_result
inline auto /* implicit */ operator co_await(stopped_result_t s) {
  return detail::result_non_value_awaitable{.non_value_ = non_value_result{s}};
}

/// co_await non_value_result{SomeError{...}}
/// co_await std::move(res).non_value()
///
/// Pass-by-&& to discourage accidental copies of `std::exception_ptr`.  If you
/// get a compile error, use `res.copy()`.
inline auto /* implicit */ operator co_await(non_value_result&& nvr) {
  return detail::result_non_value_awaitable{.non_value_ = std::move(nvr)};
}

} // namespace folly

#endif // FOLLY_HAS_RESULT
