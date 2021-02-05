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

#include <type_traits>
#include <variant>

#include <folly/Utility.h>

#include <folly/experimental/coro/Coroutine.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

template <typename T>
class AwaitableReady {
 public:
  explicit AwaitableReady(T value) noexcept(
      std::is_nothrow_move_constructible<T>::value)
      : value_(static_cast<T&&>(value)) {}

  bool await_ready() noexcept { return true; }

  void await_suspend(std::experimental::coroutine_handle<>) noexcept {}

  T await_resume() noexcept(std::is_nothrow_move_constructible<T>::value) {
    return static_cast<T&&>(value_);
  }

 private:
  T value_;
};

template <>
class AwaitableReady<void> {
 public:
  AwaitableReady() noexcept = default;
  bool await_ready() noexcept { return true; }
  void await_suspend(std::experimental::coroutine_handle<>) noexcept {}
  void await_resume() noexcept {}
};

namespace detail {

struct await_suspend_return_coroutine_fn {
  template <typename A, typename P>
  std::experimental::coroutine_handle<> operator()(
      A& a,
      std::experimental::coroutine_handle<P> coro) const
      noexcept(noexcept(a.await_suspend(coro))) {
    using result = decltype(a.await_suspend(coro));
    auto noop = std::experimental::noop_coroutine();
    if constexpr (std::is_same_v<void, result>) {
      a.await_suspend(coro);
      return noop;
    } else if constexpr (std::is_same_v<bool, result>) {
      return a.await_suspend(coro) ? noop : coro;
    } else {
      return a.await_suspend(coro);
    }
  }
};
inline constexpr await_suspend_return_coroutine_fn
    await_suspend_return_coroutine{};

template <typename... A>
class AwaitableVariant : private std::variant<A...> {
 private:
  using base = std::variant<A...>;

  template <typename P = void>
  using handle = std::experimental::coroutine_handle<P>;

  template <typename Visitor>
  auto visit(Visitor v) {
    return std::visit(v, static_cast<base&>(*this));
  }

 public:
  using base::base; // assume there are no valueless-by-exception instances

  auto await_ready() noexcept(
      (noexcept(FOLLY_DECLVAL(A&).await_ready()) && ...)) {
    return visit([&](auto& a) { return a.await_ready(); });
  }
  template <typename P>
  auto await_suspend(handle<P> coro) noexcept(
      (noexcept(FOLLY_DECLVAL(A&).await_suspend(coro)) && ...)) {
    auto impl = await_suspend_return_coroutine;
    return visit([&](auto& a) { return impl(a, coro); });
  }
  auto await_resume() noexcept(
      (noexcept(FOLLY_DECLVAL(A&).await_resume()) && ...)) {
    return visit([&](auto& a) { return a.await_resume(); });
  }
};

} // namespace detail

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
