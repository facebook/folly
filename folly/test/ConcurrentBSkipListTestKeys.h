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

// Shared test key types for ConcurrentBSkipList tests and benchmarks.

#include <compare>
#include <cstdint>
#include <functional>
#include <limits>
#include <type_traits>

namespace folly::bskip_test {

// 16-byte key type for benchmarking large hw-atomic keys.
struct Bench16B {
  int64_t timestamp;
  int64_t id;

  bool operator==(Bench16B o) const {
    return timestamp == o.timestamp && id == o.id;
  }
  bool operator<(Bench16B o) const {
    return timestamp > o.timestamp || (timestamp == o.timestamp && id < o.id);
  }
};
static_assert(sizeof(Bench16B) == 16);
static_assert(std::is_trivially_copyable_v<Bench16B>);

} // namespace folly::bskip_test

// numeric_limits must be specialized before any template that checks
// is_specialized (InternalTraits) or any type that calls min()/max()
// (AoS16BKey24).
namespace std {
template <>
struct numeric_limits<folly::bskip_test::Bench16B> {
  static constexpr bool is_specialized = true;
  static constexpr folly::bskip_test::Bench16B min() {
    return {numeric_limits<int64_t>::max(), numeric_limits<int64_t>::min()};
  }
  static constexpr folly::bskip_test::Bench16B max() {
    return {numeric_limits<int64_t>::min(), numeric_limits<int64_t>::max()};
  }
};

template <>
struct hash<folly::bskip_test::Bench16B> {
  size_t operator()(folly::bskip_test::Bench16B k) const {
    return hash<int64_t>{}(k.id) ^ (hash<int64_t>{}(k.timestamp) << 1);
  }
};
} // namespace std

namespace folly::bskip_test {

struct __attribute__((packed)) AoS16BKey24 {
  Bench16B key{};
  uint64_t hitword{0};

  auto operator<=>(const AoS16BKey24& other) const {
    if (key < other.key) {
      return std::strong_ordering::less;
    }
    if (other.key < key) {
      return std::strong_ordering::greater;
    }
    return std::strong_ordering::equal;
  }
  bool operator==(const AoS16BKey24& other) const { return key == other.key; }
  static AoS16BKey24 min_val() {
    return {std::numeric_limits<Bench16B>::min(), 0};
  }
  static AoS16BKey24 max_val() {
    return {std::numeric_limits<Bench16B>::max(), 0};
  }
};
static_assert(sizeof(AoS16BKey24) == 24);

} // namespace folly::bskip_test

namespace std {
template <>
struct numeric_limits<folly::bskip_test::AoS16BKey24> {
  static constexpr bool is_specialized = true;
  static folly::bskip_test::AoS16BKey24 min() {
    return folly::bskip_test::AoS16BKey24::min_val();
  }
  static folly::bskip_test::AoS16BKey24 max() {
    return folly::bskip_test::AoS16BKey24::max_val();
  }
};

template <>
struct hash<folly::bskip_test::AoS16BKey24> {
  size_t operator()(const folly::bskip_test::AoS16BKey24& key) const {
    return hash<folly::bskip_test::Bench16B>{}(key.key);
  }
};
} // namespace std
