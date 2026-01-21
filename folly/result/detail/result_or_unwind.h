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
#include <folly/coro/Error.h>
#include <folly/lang/MustUseImmediately.h>
#include <folly/result/detail/result_promise.h>
#include <folly/result/result.h>

#if FOLLY_HAS_RESULT

#include <coroutine>

namespace folly {

template <typename>
class or_unwind;

template <typename>
class or_unwind_owning;

class Executor;
template <typename>
class ExecutorKeepAlive;

namespace coro::detail {
struct WithAsyncStackFunction;
class TaskPromisePrivate;
} // namespace coro::detail

namespace detail {

// Base class for non-owning `or_unwind` types that store refs.
//
// Lifetime safety: Storing a reference to a temporary would dangle, but we're
// protected by `must_use_immediately_crtp` (must be awaited in the same
// full-expression) and C++ temporary lifetime extension for the prvalue case.
// The `unsafe_mover` protocol lets `co_viaIfAsync` etc. "move" these types
// despite being must-use-immediately.
//
// KNOWN BUG (LLVM issue #177023): `auto&& ref = co_await or_unwind(rval())`
// dangles because `co_await` hides the lifetime chain from Clang's
// `lifetimebound` analysis. The annotations below DO work for non-coroutine
// code but NOT through `co_await`. See `result.md` for safe alternatives.
template <typename Derived, typename StorageRef>
class result_or_unwind_ref_base
    : public ext::must_use_immediately_crtp<Derived> {
  static_assert(std::is_reference_v<StorageRef>);
  StorageRef storage_;

 protected:
  static constexpr bool kIsNonValueResult =
      std::is_same_v<std::remove_cvref_t<StorageRef>, non_value_result>;

 private:
  struct my_mover {
    StorageRef storage_;
    explicit my_mover(StorageRef&& s) noexcept
        : storage_(static_cast<StorageRef&&>(s)) {}
    Derived operator()() && noexcept {
      return Derived{static_cast<StorageRef&&>(storage_)};
    }
  };

 protected:
  StorageRef&& storage() && noexcept {
    return static_cast<StorageRef&&>(storage_);
  }
  const auto& storage() const& noexcept { return storage_; }

 public:
  explicit result_or_unwind_ref_base(
      StorageRef&& r [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]]) noexcept
      : storage_(static_cast<StorageRef&&>(r)) {}

  [[nodiscard]] decltype(auto) await_resume() noexcept
      [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] {
    if constexpr (kIsNonValueResult) {
      // `await_ready()` is false, the coro gets destroyed in `await_suspend()`
      compiler_may_unsafely_assume_unreachable();
    } else {
      // Result type: return a reference into the stored result.
      return static_cast<StorageRef&&>(storage_).value_or_throw();
    }
  }

  // unsafe_mover protocol for must-use-immediately types
  static my_mover unsafe_mover(
      ext::must_use_immediately_private_t, Derived&& me) noexcept {
    return my_mover{static_cast<StorageRef&&>(me.storage_)};
  }
};

// Base class for `or_unwind_owning` types that own the result-like values.
// Unlike the ref base, these are movable types (no `must_use_immediately` or
// `unsafe_mover`).  `await_resume` returns by value to avoid dangling.
template <typename Derived, typename StorageVal>
class result_or_unwind_value_base {
  static_assert(!std::is_reference_v<StorageVal>);
  StorageVal storage_;

 protected:
  static constexpr bool kIsNonValueResult =
      std::is_same_v<StorageVal, non_value_result>;

  StorageVal&& storage() && noexcept { return std::move(storage_); }
  const StorageVal& storage() const& noexcept { return storage_; }

 public:
  explicit result_or_unwind_value_base(StorageVal s) noexcept
      : storage_(std::move(s)) {
    // It is unexpected for `or_unwind` to throw, so ban this usage until we
    // encounter a use-case requiring more nuance.
    static_assert(std::is_nothrow_move_constructible_v<StorageVal>);
  }

  explicit result_or_unwind_value_base(stopped_result_t s) noexcept
      // Can remove -- this just communicates that `or_unwind{stopped_result}
      // constructs `or_unwind<non_value_result>`, not `or_unwind<result<T>>`.
    requires kIsNonValueResult
      : storage_(s) {}

  [[nodiscard]] decltype(auto) await_resume() noexcept {
    if constexpr (kIsNonValueResult) {
      // `await_ready()` is false, the coro gets destroyed in `await_suspend()`
      compiler_may_unsafely_assume_unreachable();
    } else if constexpr (std::is_reference_v<typename StorageVal::value_type>) {
      return std::move(storage_).value_or_throw(); // Return ref into result.
    } else {
      // Owning result types: return by value to avoid dangling refs.
      return
          typename StorageVal::value_type(std::move(storage_).value_or_throw());
    }
  }
};

// Helper to select ref vs value base class.
template <typename Derived, typename Storage>
using result_or_unwind_base = std::conditional_t<
    std::is_reference_v<Storage>,
    result_or_unwind_ref_base<Derived, Storage>,
    result_or_unwind_value_base<Derived, Storage>>;

// Shared awaitable logic for both ref and value storage.
template <typename Derived, typename Storage>
class result_or_unwind_crtp : public result_or_unwind_base<Derived, Storage> {
  using Base = result_or_unwind_base<Derived, Storage>;

  // Capability check: result<T> has non_value(), value_only_result doesn't
  static constexpr bool kHasNonValue = requires {
    FOLLY_DECLVAL(Storage&&).non_value();
  };

 public:
  using Base::await_resume;
  using Base::Base;

  [[nodiscard]] bool await_ready() const noexcept {
    if constexpr (Base::kIsNonValueResult) {
      return false;
    } else {
      return this->storage().has_value();
    }
  }

  // Suspend into a `result<U>` coroutine.
  template <typename U>
  void await_suspend(result_promise_handle<U> h) {
    auto toResult = [&](auto&& nv) {
      auto& v = *h.promise().value_;
      expected_detail::ExpectedHelper::assume_empty(v.exp_);
      // For lvalue `Storage` (e.g. `result<T>&`), `storage()` returns a ref,
      // and `result::non_value() const&` returns `const non_value_result&`,
      // which gets copied into `Unexpected` -- preserving the original result.
      // This is intentional: users don't expect `co_await or_unwind(r)` to
      // mutate `r`, since `r` might outlive the current coro.
      v.exp_ = Unexpected{static_cast<decltype(nv)>(nv)};
      h.destroy(); // Abort the rest of the coroutine, resume() won't be called
    };
    if constexpr (Base::kIsNonValueResult) {
      toResult(std::move(*this).storage());
    } else if constexpr (kHasNonValue) {
      toResult(std::move(*this).storage().non_value());
    } else {
      // `value_only_result`: not reached since `await_ready()` is always true.
      compiler_may_unsafely_assume_unreachable();
    }
  }

  // Suspend into a task-like coroutine (`now_task`, `Task`, etc.).
  //
  // NB: Do NOT support `AsyncGenerator` which also has `co_yield co_error`.
  // It doesn't abort the generator, a surprising semantic for `or_unwind`.
  template <typename Promise>
    requires requires(Promise p, coro::detail::TaskPromisePrivate priv) {
      p.continuationRef(priv);
    }
  auto await_suspend(std::coroutine_handle<Promise> h) noexcept {
    // Uses the legacy `co_error` API because `folly::coro` models cancellation
    // as an exception, and checking for `OperationCancelled` costs 50-100ns+.
    auto toTask = [&](non_value_result&& nv) {
      return h.promise()
          .yield_value(
              coro::co_error(
                  std::move(nv).get_legacy_error_or_cancellation_slow(
                      result_private_t{})))
          .await_suspend(h);
    };
    if constexpr (Base::kIsNonValueResult) {
      return toTask(std::move(*this).storage());
    } else if constexpr (kHasNonValue) {
      // `copy` since `get_legacy_error_or_cancellation_slow` lacks `const&`.
      return toTask(::folly::copy(std::move(*this).storage().non_value()));
    } else {
      // `value_only_result`: not reached since `await_ready()` is always true.
      compiler_may_unsafely_assume_unreachable();
    }
  }

  // Bypass `co_viaIfAsync` / `withAsyncStack`: `or_unwind` is synchronous.
  friend auto co_viaIfAsync(
      const ExecutorKeepAlive<Executor>&, Derived r) noexcept {
    return ext::must_use_immediately_unsafe_mover(std::move(r))();
  }
  // Conventionally, the first arg would be `cpo_t<co_withAsyncStack>`, but
  // that cannot be forward-declared.
  friend auto tag_invoke(
      const coro::detail::WithAsyncStackFunction&, Derived&& r) noexcept {
    return ext::must_use_immediately_unsafe_mover(std::move(r))();
  }
};

// Aliases to reduce CRTP boilerplate in `or_unwind` specializations.
template <typename R>
using result_or_unwind = result_or_unwind_crtp<::folly::or_unwind<R>, R>;
template <typename R>
using result_or_unwind_owning =
    result_or_unwind_crtp<::folly::or_unwind_owning<R>, R>;

} // namespace detail
} // namespace folly

#endif // FOLLY_HAS_RESULT
