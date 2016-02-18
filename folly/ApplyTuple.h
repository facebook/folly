/*
 * Copyright 2016 Facebook, Inc.
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
#include <utility>

namespace folly {

//////////////////////////////////////////////////////////////////////

namespace detail {
namespace apply_tuple {

template <std::size_t...>
struct IndexSequence {};

template <std::size_t N, std::size_t... Is>
struct MakeIndexSequence : MakeIndexSequence<N - 1, N - 1, Is...> {};

template <std::size_t... Is>
struct MakeIndexSequence<0, Is...> : IndexSequence<Is...> {};

template <class Tuple>
using MakeIndexSequenceFromTuple =
    MakeIndexSequence<std::tuple_size<typename std::decay<Tuple>::type>::value>;

// This is to allow using this with pointers to member functions,
// where the first argument in the tuple will be the this pointer.
template <class F>
inline constexpr F&& makeCallable(F&& f) {
  return std::forward<F>(f);
}
template <class M, class C>
inline constexpr auto makeCallable(M(C::*d)) -> decltype(std::mem_fn(d)) {
  return std::mem_fn(d);
}

template <class F, class Tuple, std::size_t... Indexes>
inline constexpr auto call(F&& f, Tuple&& t, IndexSequence<Indexes...>)
    -> decltype(
        std::forward<F>(f)(std::get<Indexes>(std::forward<Tuple>(t))...)) {
  return std::forward<F>(f)(std::get<Indexes>(std::forward<Tuple>(t))...);
}

} // namespace apply_tuple
} // namespace detail

//////////////////////////////////////////////////////////////////////

template <class F, class Tuple>
inline constexpr auto applyTuple(F&& f, Tuple&& t)
    -> decltype(detail::apply_tuple::call(
        detail::apply_tuple::makeCallable(std::forward<F>(f)),
        std::forward<Tuple>(t),
        detail::apply_tuple::MakeIndexSequenceFromTuple<Tuple>{})) {
  return detail::apply_tuple::call(
      detail::apply_tuple::makeCallable(std::forward<F>(f)),
      std::forward<Tuple>(t),
      detail::apply_tuple::MakeIndexSequenceFromTuple<Tuple>{});
}

//////////////////////////////////////////////////////////////////////
}
