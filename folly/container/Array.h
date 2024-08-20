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
/**
 * Helper functions to create std::arrays.
 *
 * @file container/Array.h
 * @refcode folly/docs/examples/folly/container/Array.cpp
 */

#pragma once

#include <array>
#include <type_traits>
#include <utility>

#include <folly/CPortability.h>
#include <folly/Traits.h>
#include <folly/Utility.h>

namespace folly {

namespace array_detail {
template <class T>
using is_ref_wrapper = is_instantiation_of<std::reference_wrapper, T>;

template <typename T>
using not_ref_wrapper =
    folly::Negation<is_ref_wrapper<typename std::decay<T>::type>>;

template <typename D, typename...>
struct return_type_helper {
  using type = D;
};
template <typename... TList>
struct return_type_helper<void, TList...> {
  static_assert(
      folly::Conjunction<not_ref_wrapper<TList>...>::value,
      "TList cannot contain reference_wrappers when D is void");
  using type = typename std::common_type<TList...>::type;
};

template <typename D, typename... TList>
using return_type = std::
    array<typename return_type_helper<D, TList...>::type, sizeof...(TList)>;
} // namespace array_detail

/// Constructs a std::array with the given argument list.
///
/// @param t  The values to be put in the array.
template <typename D = void, typename... TList>
constexpr array_detail::return_type<D, TList...> make_array(TList&&... t) {
  using value_type =
      typename array_detail::return_type_helper<D, TList...>::type;
  return {{static_cast<value_type>(std::forward<TList>(t))...}};
}

namespace array_detail {
template <typename MakeItem, std::size_t... Index>
FOLLY_ERASE constexpr auto make_array_with_(
    MakeItem const& make, std::index_sequence<Index...>) {
  return std::array<decltype(make(0)), sizeof...(Index)>{{make(Index)...}};
}
} // namespace array_detail

/// Generates a std::array<..., Size> with elements m(i) for i in [0, Size).
///
/// @tparam Size  The size of the array
/// @param make  The generator that makes the array elements. ret[i] = make(i)
template <std::size_t Size, typename MakeItem>
constexpr auto make_array_with(MakeItem const& make) {
  return array_detail::make_array_with_(make, std::make_index_sequence<Size>{});
}

} // namespace folly
