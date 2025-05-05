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

#include <limits>
#include <tuple>
#include <type_traits>

#include <folly/ConstexprMath.h>
#include <folly/Portability.h>
#include <folly/Utility.h>

// tupleRange<start, n>(tuple): select n elements starting at index start
//    in the given tuple
// tupleRange<start>(tuple): select all elements starting at index start
//    until the end of the given tuple
// tuplePrepend(x, tuple): return a tuple obtained by prepending x to the
//    given tuple.
//
// In Lisp lingo, std::get<0> is car, tupleRange<1> is cdr, and tuplePrepend
// is cons.

namespace folly {

namespace detail {

template <std::size_t A, std::size_t... B>
std::index_sequence<(A + B)...> tuple_ops_index_seq_inc(
    std::index_sequence<B...>);

template <
    std::size_t start,
    std::size_t count,
    typename tup,
    std::size_t size = std::tuple_size_v<remove_cvref_t<tup>>,
    std::enable_if_t<(start <= size), int> = 0>
using tuple_ops_range_of_seq = decltype(tuple_ops_index_seq_inc<start>( //
    std::make_index_sequence<constexpr_min(count, size - start)>{}));

template <typename tup, std::size_t... index>
FOLLY_ERASE std::tuple<
    remove_cvref_t<std::tuple_element_t<index, remove_cvref_t<tup>>>...>
tuple_ops_range_of_(tup&& t, std::index_sequence<index...>) {
  return std::make_tuple(std::get<index>(static_cast<tup&&>(t))...);
}

} // namespace detail

// Return a tuple consisting of the elements at a range of indices.
//
// Use as tupleRange<start, count>(t) to return a tuple of (at most) count
// elements starting at index start in tuple t.
// If only start is specified (tupleRange<start>(t)), returns all elements
// starting at index start until the end of the tuple t.
// Won't compile if start > size of t.
// Will return fewer elements (size - start) if start + n > size of t.
template <
    std::size_t start = 0,
    std::size_t count = std::numeric_limits<std::size_t>::max(),
    typename tup>
auto tupleRange(tup&& t) -> decltype(detail::tuple_ops_range_of_(
    static_cast<tup&&>(t),
    detail::tuple_ops_range_of_seq<start, count, tup>{})) {
  return detail::tuple_ops_range_of_(
      static_cast<tup&&>(t),
      detail::tuple_ops_range_of_seq<start, count, tup>{});
}

// Return a tuple obtained by prepending car to the tuple cdr.
template <class T, class U>
auto tuplePrepend(T&& car, U&& cdr) -> decltype(std::tuple_cat(
    std::make_tuple(std::forward<T>(car)), std::forward<U>(cdr))) {
  return std::tuple_cat(
      std::make_tuple(std::forward<T>(car)), std::forward<U>(cdr));
}

} // namespace folly
