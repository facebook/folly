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

#include <type_traits>

#include <folly/Traits.h>
#include <folly/Utility.h>

#if defined(__cpp_lib_ranges)
#include <ranges>
#endif

namespace folly {

namespace detail {

// clang-format off
template <
    typename R,
    typename S = typename R::size_type,
    typename V = typename R::value_type,
    typename TD = decltype(FOLLY_DECLVAL(R&).data()),
    typename TCD = decltype(FOLLY_DECLVAL(R const&).data()),
    typename TCS = decltype(FOLLY_DECLVAL(R const&).size())>
using is_contiguous_range_fallback_impl_ = std::bool_constant<(true
    && (std::is_same_v<V*, TD> || std::is_same_v<V const*, TD>)
    && std::is_same_v<V const*, TCD>
    && std::is_same_v<S, TCS>)>;
// clang-format on

template <
    typename R,
    bool RUser = std::is_union_v<R> || std::is_class_v<R>>
constexpr bool is_contiguous_range_v_ = //
    !require_sizeof<conditional_t<RUser, R, int>>
#if defined(__cpp_lib_ranges)
    || std::ranges::contiguous_range<R>
#endif
    || is_bounded_array_v<R> //
    || detected_or_t<std::false_type, is_contiguous_range_fallback_impl_, R>{};

} // namespace detail

//  is_contiguous_range_v
//
//  True when any of:
//  * std::ranges::contiguous_range holds, if available
//  * is_bounded_array_v holds
//  * certain conditions using member types or type aliases size_type and
//    value_type and member functions size() and data()
//  Otherwise false.
//
//  Necessarily true if the given type is any of:
//  * T[S], ie a bounded array
//  * std::array
//  * std::basic_string
//  * std::basic_string_view
//  * std::span
//  * std::vector
//
//  Rejects incomplete class/union types, even if they would be accepted when
//  completed.
template <typename R>
constexpr bool is_contiguous_range_v = detail::is_contiguous_range_v_<R>;

} // namespace folly
