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

#include <functional>
#include <tuple>
#include <utility>

#include <folly/Traits.h>
#include <folly/Utility.h>
#include <folly/functional/Invoke.h>

namespace folly {

//////////////////////////////////////////////////////////////////////

/**
 * Helper to generate an index sequence from a tuple like type
 */
template <typename Tuple>
using index_sequence_for_tuple =
    std::make_index_sequence<std::tuple_size<Tuple>::value>;

namespace detail {
namespace apply_tuple {

namespace adl {

using std::get;

template <std::size_t I>
struct invoke_get_fn {
  template <typename T>
  constexpr auto operator()(T&& t) const
      noexcept(noexcept(get<I>(static_cast<T&&>(t))))
          -> decltype(get<I>(static_cast<T&&>(t))) {
    return get<I>(static_cast<T&&>(t));
  }
};

} // namespace adl

template <
    typename Tuple,
    std::size_t... Indices,
    typename ReturnTuple = std::tuple<
        decltype(adl::invoke_get_fn<Indices>{}(std::declval<Tuple>()))...>>
auto forward_tuple(Tuple&& tuple, std::index_sequence<Indices...>)
    -> ReturnTuple {
  return ReturnTuple{
      adl::invoke_get_fn<Indices>{}(static_cast<Tuple&&>(tuple))...};
}

} // namespace apply_tuple
} // namespace detail

struct ApplyInvoke {
 private:
  template <typename T>
  using seq = index_sequence_for_tuple<std::remove_reference_t<T>>;

  template <std::size_t I>
  using get = detail::apply_tuple::adl::invoke_get_fn<I>;

  template <typename F, typename T, std::size_t... I>
  static constexpr auto
  invoke_(F&& f, T&& t, std::index_sequence<I...>) noexcept(
      noexcept(invoke(static_cast<F&&>(f), get<I>{}(static_cast<T&&>(t))...)))
      -> decltype(invoke(
          static_cast<F&&>(f), get<I>{}(static_cast<T&&>(t))...)) {
    return invoke(static_cast<F&&>(f), get<I>{}(static_cast<T&&>(t))...);
  }

 public:
  template <typename F, typename T>
  constexpr auto operator()(F&& f, T&& t) const noexcept(
      noexcept(invoke_(static_cast<F&&>(f), static_cast<T&&>(t), seq<T>{})))
      -> decltype(invoke_(static_cast<F&&>(f), static_cast<T&&>(t), seq<T>{})) {
    return invoke_(static_cast<F&&>(f), static_cast<T&&>(t), seq<T>{});
  }
};

//////////////////////////////////////////////////////////////////////

/* using override */ using std::apply;

/**
 * Get a tuple of references from the passed tuple, forwarding will be applied
 * on the individual types of the tuple based on the value category of the
 * passed tuple
 *
 * For example
 *
 *    forward_tuple(std::make_tuple(1, 2))
 *
 * Returns a std::tuple<int&&, int&&>,
 *
 *    auto tuple = std::make_tuple(1, 2);
 *    forward_tuple(tuple)
 *
 * Returns a std::tuple<int&, int&>
 */
template <typename Tuple>
auto forward_tuple(Tuple&& tuple) noexcept
    -> decltype(detail::apply_tuple::forward_tuple(
        std::declval<Tuple>(),
        std::declval<
            index_sequence_for_tuple<std::remove_reference_t<Tuple>>>())) {
  return detail::apply_tuple::forward_tuple(
      static_cast<Tuple&&>(tuple),
      index_sequence_for_tuple<std::remove_reference_t<Tuple>>{});
}

/**
 * Mimic the invoke suite of traits for tuple based apply invocation
 */
template <typename F, typename Tuple>
using apply_result = invoke_result<ApplyInvoke, F, Tuple>;
template <typename F, typename Tuple>
using apply_result_t = invoke_result_t<ApplyInvoke, F, Tuple>;
template <typename F, typename Tuple>
inline constexpr bool is_applicable_v = is_invocable_v<ApplyInvoke, F, Tuple>;
template <typename F, typename Tuple>
using is_applicable = is_invocable<ApplyInvoke, F, Tuple>;
template <typename R, typename F, typename Tuple>
inline constexpr bool is_applicable_r_v =
    is_invocable_r_v<R, ApplyInvoke, F, Tuple>;
template <typename R, typename F, typename Tuple>
using is_applicable_r = is_invocable_r<R, ApplyInvoke, F, Tuple>;
template <typename F, typename Tuple>
inline constexpr bool is_nothrow_applicable_v =
    is_nothrow_invocable_v<ApplyInvoke, F, Tuple>;
template <typename F, typename Tuple>
using is_nothrow_applicable = is_nothrow_invocable<ApplyInvoke, F, Tuple>;
template <typename R, typename F, typename Tuple>
inline constexpr bool is_nothrow_applicable_r_v =
    is_nothrow_invocable_r_v<R, ApplyInvoke, F, Tuple>;
template <typename R, typename F, typename Tuple>
using is_nothrow_applicable_r =
    is_nothrow_invocable_r<R, ApplyInvoke, F, Tuple>;

namespace detail {
namespace apply_tuple {

template <class F>
class Uncurry {
 public:
  explicit Uncurry(F&& func) : func_(std::move(func)) {}
  explicit Uncurry(const F& func) : func_(func) {}

  template <class Tuple>
  auto operator()(Tuple&& tuple) const
      -> decltype(apply(std::declval<F>(), std::forward<Tuple>(tuple))) {
    return apply(func_, std::forward<Tuple>(tuple));
  }

 private:
  F func_;
};
} // namespace apply_tuple
} // namespace detail

/**
 * Wraps a function taking N arguments into a function which accepts a tuple of
 * N arguments. Note: This function will also accept an std::pair if N == 2.
 *
 * For example, given the below code:
 *
 *    std::vector<std::tuple<int, int, int>> rows = ...;
 *    auto test = [](std::tuple<int, int, int>& row) {
 *      return std::get<0>(row) * std::get<1>(row) * std::get<2>(row) == 24;
 *    };
 *    auto found = std::find_if(rows.begin(), rows.end(), test);
 *
 *
 * 'test' could be rewritten as:
 *
 *    auto test =
 *        folly::uncurry([](int a, int b, int c) { return a * b * c == 24; });
 *
 */
template <class F>
auto uncurry(F&& f)
    -> detail::apply_tuple::Uncurry<typename std::decay<F>::type> {
  return detail::apply_tuple::Uncurry<typename std::decay<F>::type>(
      std::forward<F>(f));
}

//////////////////////////////////////////////////////////////////////
} // namespace folly
