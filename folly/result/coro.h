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
#include <folly/result/result.h>

#if FOLLY_HAS_RESULT

// One must never conditionally-include folly headers, but this is required to
// be gated, since `result.h` may get included in C++17 contexts.
#include <coroutine>

/// co_await or_unwind(resultFunc())
///
/// Recommended pre-read: `result.h` docblock and/or `docs/result.md`.
///
/// Use `or_unwind` to "await" short-circuiting types (currently, just `result`)
/// from `result` coroutines OR from `folly::coro` async task coroutines:
///
///   result<int> getN();
///   int n = co_await or_unwind(getN());
///
/// If you JUST need a task to cheaply propagate exceptions to its parent,
/// avoiding rethrowing, then you have a more concise option:
///
///   auto v = co_await co_nothrow(asyncMayError()); // best practice
///   // equivalent, but too long
///   auto v = co_await or_unwind(co_await co_await_result(asyncMayError()));
///
/// However, when you are calling synchronous `result` functions, or need to
/// efficiently handle **some** async errors, `or_unwind` is your friend:
///
///   auto res = syncResultFn(); // or `co_await co_await_result(asyncFn())`
///   if (auto* ex = get_exception<MyError>(res)) {
///     /* handle ex */
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
/// unnecessarily copying the value OR the error (25ns), so mind your
/// move hygiene in hot code.
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
/// ## Future: Extensions to non-`result` types
///
/// The current implementation is `result`-only.  If you have a need, it might
/// be fine to specialize it for `Try`.  Just be mindful of its two warts:
/// empty state and empty `exception_wrapper`.

namespace folly {

template <typename>
class or_unwind;

class Executor;
template <typename>
class ExecutorKeepAlive;

namespace coro::detail {
struct WithAsyncStackFunction;
class TaskPromisePrivate;
} // namespace coro::detail

namespace detail {

template <typename>
struct result_promise_base;

template <typename T>
struct result_promise_return {
  result<T> storage_{expected_detail::EmptyTag{}};
  result<T>*& pointer_;

  /* implicit */ result_promise_return(result_promise_base<T>& p) noexcept
      : pointer_{p.value_} {
    pointer_ = &storage_;
  }
  result_promise_return(result_promise_return const&) = delete;
  void operator=(result_promise_return const&) = delete;
  result_promise_return(result_promise_return&&) = delete;
  void operator=(result_promise_return&&) = delete;
  // Remove this once clang 15 is well-forgotten. From D42260201:
  // letting dtor be trivial makes the coroutine crash
  ~result_promise_return() {}

  /* implicit */ operator result<T>() {
    // Simplify this once clang 15 is well-forgotten, and remove the dep on
    // `Coroutine.h`.  From D42260201: handle both deferred and eager
    // return-object conversion behaviors see docs for
    // detect_promise_return_object_eager_conversion
    if (coro::detect_promise_return_object_eager_conversion()) {
      assert(storage_.is_expected_empty());
      return result<T>{expected_detail::EmptyTag{}, pointer_}; // eager
    } else {
      assert(!storage_.is_expected_empty());
      return std::move(storage_); // deferred
    }
  }
};

template <typename T>
struct result_promise_base {
  result<T>* value_ = nullptr;

  result_promise_base() = default;
  result_promise_base(result_promise_base const&) = delete;
  void operator=(result_promise_base const&) = delete;
  result_promise_base(result_promise_base&&) = delete;
  void operator=(result_promise_base&&) = delete;
  ~result_promise_base() = default;

  FOLLY_NODISCARD std::suspend_never initial_suspend() const noexcept {
    return {};
  }
  FOLLY_NODISCARD std::suspend_never final_suspend() const noexcept {
    return {};
  }
  void unhandled_exception() noexcept {
    *value_ = non_value_result::from_current_exception();
  }

  result_promise_return<T> get_return_object() noexcept { return *this; }
};

template <typename T>
struct result_promise<T, typename std::enable_if<!std::is_void_v<T>>::type>
    : public result_promise_base<T> {
  // For reference types, this deliberately requires users to `co_return`
  // one of `std::ref`, `std::cref`, or `folly::rref`.
  //
  // The default for `U` is tested in `returnImplicitCtor`.
  template <typename U = T>
  void return_value(U&& u) {
    auto& v = *this->value_;
    expected_detail::ExpectedHelper::assume_empty(v.exp_);
    v = static_cast<U&&>(u);
  }
};

template <typename T>
struct result_promise<T, typename std::enable_if<std::is_void_v<T>>::type>
    : public result_promise_base<T> {
  // You can fail via `co_await err` since void coros only allow `co_return;`.
  void return_void() { this->value_->exp_.emplace(unit); }
};

template <typename T> // Need an alias since nested types cannot be deduced.
using result_promise_handle = std::coroutine_handle<result_promise<T>>;

// This is separate to let future https://fburl.com/result-generator-impl reuse
// the awaitables below.
struct result_await_suspender {
  // Future: check if all these `FOLLY_ALWAYS_INLINE`s aren't a pessimization.
  template <typename T, typename U>
  FOLLY_ALWAYS_INLINE void operator()(T&& t, result_promise_handle<U> handle) {
    auto& v = *handle.promise().value_;
    expected_detail::ExpectedHelper::assume_empty(v.exp_);
    // `T` can be `non_value_result&&`, or one of a few `result<T>` refs.
    if constexpr (std::is_same_v<non_value_result, std::remove_cvref_t<T>>) {
      v.exp_ = Unexpected{std::forward<T>(t)};
    } else {
      v.exp_ = Unexpected{std::forward<T>(t).non_value()};
    }
    // Abort the rest of the coroutine. resume() is not going to be called
    handle.destroy();
  }
};

template <typename Derived, typename ResultRef>
class or_unwind_crtp
    : public ext::must_use_immediately_crtp<
          or_unwind_crtp<Derived, ResultRef>> {
 protected:
  ResultRef resultRef_;

 public:
  explicit or_unwind_crtp(
      ResultRef&& rr [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]]) noexcept
      : resultRef_(static_cast<ResultRef&&>(rr)) {
    // not `noexcept` otherwise, have to update `my_mover` below
    static_assert(std::is_reference_v<ResultRef>);
  }

  bool await_ready() const noexcept { return resultRef_.has_value(); }

  auto await_resume() noexcept [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]]
  -> decltype(FOLLY_DECLVAL(ResultRef&&).value_or_throw()) {
    return static_cast<ResultRef&&>(resultRef_).value_or_throw();
  }

  // When awaited from a `result<U>` coro:
  template <typename U>
  void await_suspend(detail::result_promise_handle<U> awaitingCoro) {
    // There is no `value_only_result::non_value()`
    if constexpr (requires {
                    static_cast<ResultRef&&>(resultRef_).non_value();
                  }) {
      auto& v = *awaitingCoro.promise().value_;
      expected_detail::ExpectedHelper::assume_empty(v.exp_);
      // We can't move the error out of mutable l-value references to `result`,
      // because the user isn't counting on `co_await or_unwind(m)` to mutate
      // the `result`.  For example, `m` might not be a local, and could
      // outlive the current coro.
      v.exp_ = Unexpected{static_cast<ResultRef&&>(resultRef_).non_value()};
      awaitingCoro.destroy();
    }
  }

  // When awaited from a `coro::some_task<U>` coro -- covered in
  // `await_result_from_task_test.cpp`.
  //
  // Why only task-like coroutines?  While `AsyncGenerator` also supports
  // `co_yield co_error`, the behavior doesn't abort the generator, which would
  // probably be surprising.  Support for short-circuiting a generator via
  // `co_await or_unwind()` wouldn't be hard to add, though.
  template <typename Promise>
    requires requires(Promise promise, coro::detail::TaskPromisePrivate priv) {
      // Pick `Task` and all `TaskWrapper`s, but not `AsyncGenerator`.
      promise.continuationRef(priv);
    }
  auto await_suspend(std::coroutine_handle<Promise> awaitingCoro) noexcept {
    // We have to use the legacy API because (1) `folly::coro` internals still
    // model cancellation as an exception, (2) to use `co_cancelled` here we'd
    // have to check `resultRef_` for `OperationCancelled` which can cost
    // 50-100ns+.
    auto awaiter = awaitingCoro.promise().yield_value(coro::co_error(
        // This `copy` is here because `get_legacy_error_or_cancellation` lacks
        // a `const`-qualified overload.
        ::folly::copy(static_cast<ResultRef&&>(resultRef_).non_value())
            .get_legacy_error_or_cancellation()));
    return awaiter.await_suspend(awaitingCoro);
  }

  friend auto co_viaIfAsync(
      const ExecutorKeepAlive<Executor>&, Derived r) noexcept {
    return must_use_immediately_unsafe_mover(std::move(r))();
  }

  // Conventionally, the first arg would be `cpo_t<co_withAsyncStack>`, but
  // that cannot be forward-declared.
  friend auto tag_invoke(
      const coro::detail::WithAsyncStackFunction&, Derived&& r) noexcept {
    return must_use_immediately_unsafe_mover(std::move(r))();
  }

 private:
  struct my_mover {
   private:
    ResultRef resultRef_;

   public:
    explicit my_mover(ResultRef&& rr) noexcept
        : resultRef_(static_cast<ResultRef&&>(rr)) {}
    Derived operator()() && noexcept {
      return Derived{static_cast<ResultRef&&>(resultRef_)};
    }
  };

 public:
  static my_mover unsafe_mover( // no slicing risk, `or_unwind` is `final`
      ext::must_use_immediately_private_t,
      Derived&& me) noexcept {
    return my_mover{static_cast<ResultRef&&>(me.resultRef_)};
  }
};

template <typename ResultRef>
using or_unwind_base = or_unwind_crtp<or_unwind<ResultRef>, ResultRef>;

// There's no `result` in the name as a hint to lift this to a shared header as
// soon as another usecase arises.
template <typename AwaitSuspender>
struct non_value_awaitable {
  non_value_result non_value_;

  constexpr std::false_type await_ready() const noexcept { return {}; }
  [[noreturn]] void await_resume() {
    compiler_may_unsafely_assume_unreachable();
  }
  FOLLY_ALWAYS_INLINE void await_suspend(auto h) {
    AwaitSuspender()(std::move(non_value_), h);
  }
};

} // namespace detail

// co_await stopped_result
inline auto /* implicit */ operator co_await(stopped_result_t s) {
  return detail::non_value_awaitable<detail::result_await_suspender>{
      .non_value_ = non_value_result{s}};
}

// co_await non_value_result{SomeError{...}}
// co_await std::move(res).non_value()
//
// Pass-by-&& to discourage accidental copies of `std::exception_ptr`, if you
// get a compile error, use `res.copy()`.
inline auto /* implicit */ operator co_await(non_value_result && nvr) {
  return detail::non_value_awaitable<detail::result_await_suspender>{
      .non_value_ = std::move(nvr)};
}

// Making these `final` makes `unsafe_mover` simpler due to no slicing risk.

// co_await or_unwind(resFn())
template <typename T>
class or_unwind<result<T>&&> final
    : public detail::or_unwind_base<result<T>&&> {
  using detail::or_unwind_base<result<T>&&>::or_unwind_base;
};
template <typename T>
or_unwind(result<T>&&) -> or_unwind<result<T>&&>;

// co_await or_unwind(res)
template <typename T>
class or_unwind<result<T>&> final : public detail::or_unwind_base<result<T>&> {
  using detail::or_unwind_base<result<T>&>::or_unwind_base;
};
template <typename T>
or_unwind(result<T>&) -> or_unwind<result<T>&>;

// co_await or_unwind(std::as_const(res))
template <typename T>
class or_unwind<const result<T>&> final
    : public detail::or_unwind_base<const result<T>&> {
  using detail::or_unwind_base<const result<T>&>::or_unwind_base;
};
template <typename T>
or_unwind(const result<T>&) -> or_unwind<const result<T>&>;

// This short-circuiting coroutine implementation was modeled on
// `folly/Expected.h`. Please try to port any compiler fixes or
// optimizations across both.
} // namespace folly

#endif // FOLLY_HAS_RESULT
