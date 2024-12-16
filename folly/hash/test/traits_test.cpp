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

#include <folly/hash/traits.h>

#include <folly/portability/GTest.h>

using namespace folly;

namespace {

struct HashableStruct1 {};
struct HashableStruct2 {};
struct UnhashableStruct {};

template <typename X, typename Y>
struct CompositeStruct {
  X x;
  Y y;
};

} // namespace

namespace std {

template <>
struct hash<HashableStruct1> {
  [[maybe_unused]] size_t operator()(const HashableStruct1&) const noexcept {
    return 0;
  }
};

template <>
struct hash<HashableStruct2> {
  [[maybe_unused]] size_t operator()(const HashableStruct2&) const noexcept {
    return 0;
  }
};

template <typename X, typename Y>
struct hash<enable_std_hash_helper<CompositeStruct<X, Y>, X, Y>> {
  [[maybe_unused]] size_t operator()(
      const CompositeStruct<X, Y>& value) const noexcept {
    return std::hash<X>{}(value.x) + std::hash<Y>{}(value.y);
  }
};

} // namespace std

static_assert(is_hashable_v<HashableStruct1>);
static_assert(is_hashable_v<HashableStruct2>);
static_assert(!is_hashable_v<UnhashableStruct>);
static_assert(is_hashable_v<CompositeStruct<HashableStruct1, HashableStruct1>>);
static_assert(is_hashable_v<CompositeStruct<HashableStruct1, HashableStruct2>>);
static_assert(
    !is_hashable_v<CompositeStruct<HashableStruct1, UnhashableStruct>>);
