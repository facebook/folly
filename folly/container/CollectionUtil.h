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

#include <concepts>
#include <folly/lang/Exception.h>

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
 * This function checks whether container contains given key.
 * Use container specific .contains() implementation if available,
 * otherwise uses .find() implementation.
 */
template <class C, class K = typename C::key_type>
  requires(detail::HasContains<C, K> || detail::HasFind<C, K>)
bool contains(const C& container, const K& key) {
  if constexpr (detail::HasContains<C, K>) {
    return container.contains(key);
  } else {
    return container.find(key) != container.end();
  }
}
} // namespace folly
