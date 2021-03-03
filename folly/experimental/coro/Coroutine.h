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

#if __has_include(<variant>)
#include <variant>
#endif

#include <folly/Portability.h>
#include <folly/Utility.h>

#if FOLLY_HAS_COROUTINES

#if __has_include(<coroutine>)
#include <coroutine>
#else
#include <experimental/coroutine>
#endif

#endif // FOLLY_HAS_COROUTINES

//  A place for foundational vocabulary types.
//
//  This header reexports the foundational vocabulary coroutine-helper types
//  from the standard, and exports several new foundational vocabulary types
//  as well.
//
//  Types which are non-foundational and non-vocabulary should go elsewhere.

#if FOLLY_HAS_COROUTINES

namespace folly::coro {

#if __has_include(<coroutine>)
namespace impl = std;
#else
namespace impl = std::experimental;
#endif

using impl::coroutine_handle;
using impl::coroutine_traits;
using impl::noop_coroutine;
using impl::noop_coroutine_handle;
using impl::noop_coroutine_promise;
using impl::suspend_always;
using impl::suspend_never;

//  ready_awaitable
//
//  An awaitable which is immediately ready with a value. Suspension is no-op.
//  Resumption returns the value.
//
//  The value type is permitted to be a reference.
template <typename T = void>
class ready_awaitable {
  static_assert(!std::is_void<T>::value, "base template unsuitable for void");

 public:
  explicit ready_awaitable(T value) //
      noexcept(noexcept(T(FOLLY_DECLVAL(T&&))))
      : value_(static_cast<T&&>(value)) {}

  bool await_ready() noexcept { return true; }
  void await_suspend(coroutine_handle<>) noexcept {}
  T await_resume() noexcept(noexcept(T(FOLLY_DECLVAL(T&&)))) {
    return static_cast<T&&>(value_);
  }

 private:
  T value_;
};

//  ready_awaitable
//
//  An awaitable type which is immediately ready. Suspension is a no-op.
template <>
class ready_awaitable<void> {
 public:
  ready_awaitable() noexcept = default;

  bool await_ready() noexcept { return true; }
  void await_suspend(coroutine_handle<>) noexcept {}
  void await_resume() noexcept {}
};

namespace detail {

//  await_suspend_return_coroutine_fn
//  await_suspend_return_coroutine
//
//  The special member await_suspend has three forms, differing in their return
//  types. It may return void, bool, or coroutine_handle<>. This invokes member
//  await_suspend on the argument, conspiring always to return coroutine_handle
//  no matter the underlying form of member await_suspend on the argument.
struct await_suspend_return_coroutine_fn {
  template <typename A, typename P>
  coroutine_handle<> operator()(A& a, coroutine_handle<P> coro) const
      noexcept(noexcept(a.await_suspend(coro))) {
    using result = decltype(a.await_suspend(coro));
    if constexpr (std::is_same<void, result>::value) {
      a.await_suspend(coro);
      return noop_coroutine();
    } else if constexpr (std::is_same<bool, result>::value) {
      return a.await_suspend(coro) ? noop_coroutine() : coro;
    } else {
      return a.await_suspend(coro);
    }
  }
};
inline constexpr await_suspend_return_coroutine_fn
    await_suspend_return_coroutine{};

} // namespace detail

#if __has_include(<variant>)

//  variant_awaitable
//
//  An awaitable type which is backed by one of several possible underlying
//  awaitables.
template <typename... A>
class variant_awaitable : private std::variant<A...> {
 private:
  using base = std::variant<A...>;

  template <typename Visitor>
  auto visit(Visitor v) {
    return std::visit(v, static_cast<base&>(*this));
  }

 public:
  // imports the base-class constructors wholesale for implementation simplicity
  using base::base; // assume there are no valueless-by-exception instances

  auto await_ready() noexcept(
      (noexcept(FOLLY_DECLVAL(A&).await_ready()) && ...)) {
    return visit([&](auto& a) { return a.await_ready(); });
  }
  template <typename P>
  auto await_suspend(coroutine_handle<P> coro) noexcept(
      (noexcept(FOLLY_DECLVAL(A&).await_suspend(coro)) && ...)) {
    auto impl = detail::await_suspend_return_coroutine;
    return visit([&](auto& a) { return impl(a, coro); });
  }
  auto await_resume() noexcept(
      (noexcept(FOLLY_DECLVAL(A&).await_resume()) && ...)) {
    return visit([&](auto& a) { return a.await_resume(); });
  }
};

#endif // __has_include(<variant>)

} // namespace folly::coro

#endif
