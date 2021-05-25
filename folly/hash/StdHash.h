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

#include <folly/hash/Hash.h>

// Custom hash functions.
namespace std {
// Hash function for pairs. Requires default hash functions for both
// items in the pair.
template <typename T1, typename T2>
struct hash<std::pair<T1, T2>> {
  using folly_is_avalanching = std::true_type;

  size_t operator()(const std::pair<T1, T2>& x) const {
    return folly::hash::hash_combine(x.first, x.second);
  }
};

// Hash function for tuples. Requires default hash functions for all types.
template <typename... Ts>
struct hash<std::tuple<Ts...>> {
 private:
  using FirstT = std::decay_t<std::tuple_element_t<0, std::tuple<Ts..., bool>>>;

 public:
  using folly_is_avalanching = folly::bool_constant<(
      sizeof...(Ts) != 1 ||
      folly::IsAvalanchingHasher<std::hash<FirstT>, FirstT>::value)>;

  size_t operator()(std::tuple<Ts...> const& key) const {
    folly::TupleHasher<
        sizeof...(Ts) - 1, // start index
        Ts...>
        hasher;

    return hasher(key);
  }
};
} // namespace std
