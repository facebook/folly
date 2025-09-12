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

#include <algorithm>
#include <concepts>

namespace folly {

namespace detail {
template <typename T, typename K>
concept HasContains = requires(T& t, const K& k) {
  { t.contains(k) };
};

template <typename T, typename K>
concept HasFind = requires(T& t, const K& k) {
  { t.find(k) } -> std::convertible_to<typename T::const_iterator>;
};
} // namespace detail
/**
 * This function check whether container contains given value.
 * Can be used for non-associative containers.
 * Notes:
 * Runtime = O(n)
 * Work with vector, map.
 */
template <class C, class V = typename C::value_type>
  requires(!detail::HasContains<C, V> && !detail::HasFind<C, V>)
bool contains(const C& container, const V& value) {
  const auto e = std::end(container);
  return std::find(std::begin(container), e, value) != e;
}

/**
 * This function checks whether container contains given key.
 * Use container specific .contains() implementation if available,
 * otherwise uses .find() implementation if available, otherwise
 * fallback to O(n) std::find() implementation.
 */
template <class C, class K = typename C::key_type>
bool contains(const C& container, const K& key) {
  if constexpr (detail::HasContains<C, K>) {
    return container.contains(key);
  } else if constexpr (detail::HasFind<C, K>) {
    return container.find(key) != container.end();
  } else {
    // Fallback: use generic and possibly slower std::find otherwise.
    return contains(container, key);
  }
}

} // namespace folly
