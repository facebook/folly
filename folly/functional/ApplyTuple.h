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

/*
 * Defines a function folly::applyTuple, which takes a function and a
 * std::tuple of arguments and calls the function with those
 * arguments.
 *
 * Example:
 *
 *    int x = folly::applyTuple(std::plus<int>(), std::make_tuple(12, 12));
 *    ASSERT(x == 24);
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
namespace apply_tuple {

inline constexpr std::size_t sum() {
  return 0;
}
template <typename... Args>
inline constexpr std::size_t sum(std::size_t v1, Args... vs) {
  return v1 + sum(vs...);
}

template <typename... Tuples>
struct TupleSizeSum {
  static constexpr auto value = sum(std::tuple_size<Tuples>::value...);
};

template <typename... Tuples>
using MakeIndexSequenceFromTuple = folly::make_index_sequence<
    TupleSizeSum<typename std::decay<Tuples>::type...>::value>;

template <class F, class Tuple, std::size_t... Indexes>
inline constexpr auto call(F&& f, Tuple&& t, folly::index_sequence<Indexes...>)
    -> decltype(invoke(
        std::forward<F>(f),
        std::get<Indexes>(std::forward<Tuple>(t))...)) {
  return invoke(
      std::forward<F>(f), std::get<Indexes>(std::forward<Tuple>(t))...);
}

template <class Tuple, std::size_t... Indexes>
inline constexpr auto forwardTuple(Tuple&& t, folly::index_sequence<Indexes...>)
    -> decltype(
        std::forward_as_tuple(std::get<Indexes>(std::forward<Tuple>(t))...)) {
  return std::forward_as_tuple(std::get<Indexes>(std::forward<Tuple>(t))...);
}

} // namespace apply_tuple
} // namespace detail

//////////////////////////////////////////////////////////////////////

/**
 * Invoke a callable object with a set of arguments passed as a tuple, or a
 *     series of tuples
 *
 * Example: the following lines are equivalent
 *     func(1, 2, 3, "foo");
 *     applyTuple(func, std::make_tuple(1, 2, 3, "foo"));
 *     applyTuple(func, std::make_tuple(1, 2), std::make_tuple(3, "foo"));
 */

template <class F, class... Tuples>
inline constexpr auto applyTuple(F&& f, Tuples&&... t)
    -> decltype(detail::apply_tuple::call(
        std::forward<F>(f),
        std::tuple_cat(detail::apply_tuple::forwardTuple(
            std::forward<Tuples>(t),
            detail::apply_tuple::MakeIndexSequenceFromTuple<Tuples>{})...),
        detail::apply_tuple::MakeIndexSequenceFromTuple<Tuples...>{})) {
  return detail::apply_tuple::call(
      std::forward<F>(f),
      std::tuple_cat(detail::apply_tuple::forwardTuple(
          std::forward<Tuples>(t),
          detail::apply_tuple::MakeIndexSequenceFromTuple<Tuples>{})...),
      detail::apply_tuple::MakeIndexSequenceFromTuple<Tuples...>{});
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
      -> decltype(applyTuple(std::declval<F>(), std::forward<Tuple>(tuple))) {
    return applyTuple(func_, std::forward<Tuple>(tuple));
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

#if __cpp_lib_make_from_tuple || _MSC_VER >= 1910

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
  return applyTuple(
      detail::apply_tuple::Construct<T>(), std::forward<Tuple>(t));
}

#endif

//////////////////////////////////////////////////////////////////////
} // namespace folly
