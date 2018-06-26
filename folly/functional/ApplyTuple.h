/*
 * Copyright 2012-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
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

#include <folly/Utility.h>
#include <folly/functional/Invoke.h>

namespace folly {

//////////////////////////////////////////////////////////////////////

namespace detail {
template <class F, class T, std::size_t... I>
constexpr decltype(auto) applyImpl(F&& f, T&& t, folly::index_sequence<I...>) {
  return invoke(std::forward<F>(f), std::get<I>(std::forward<T>(t))...);
}
} // namespace detail

//////////////////////////////////////////////////////////////////////

//  mimic: std::apply, C++17
template <typename F, typename Tuple>
constexpr decltype(auto) apply(F&& func, Tuple&& tuple) {
  constexpr auto size = std::tuple_size<std::remove_reference_t<Tuple>>::value;
  return detail::applyImpl(
      std::forward<F>(func),
      std::forward<Tuple>(tuple),
      folly::make_index_sequence<size>{});
}

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

#if __cpp_lib_make_from_tuple || (_MSC_VER >= 1910 && _MSVC_LANG > 201402)

/* using override */ using std::make_from_tuple;

#else

namespace detail {
namespace apply_tuple {
template <class T>
struct Construct {
  template <class... Args>
  constexpr T operator()(Args&&... args) const {
    return T(std::forward<Args>(args)...);
  }
};
} // namespace apply_tuple
} // namespace detail

//  mimic: std::make_from_tuple, C++17
template <class T, class Tuple>
constexpr T make_from_tuple(Tuple&& t) {
  return apply(detail::apply_tuple::Construct<T>(), std::forward<Tuple>(t));
}

#endif

//////////////////////////////////////////////////////////////////////
} // namespace folly
