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

// Built twice, once at -O0 and once at -O2, for F14MixedOptimizationTest.cpp.
// FOLLY_F14_MIXED_OPT_NS names the namespace of each copy.

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include <folly/Conv.h>
#include <folly/container/F14Map.h>
#include <folly/container/detail/F14Table.h>

namespace folly::f14::test {

struct MixedOptMaps {
  F14ValueMap<int, int> value;
  F14NodeMap<int, int> node;
  F14VectorMap<int, int> vector;
};

// Same definition as in F14MixedOptimizationTest.cpp. The body differs with
// __OPTIMIZE__ on purpose, to show which file's copy the linker kept: a
// deliberate, test-only ODR violation. Do not copy this pattern.
inline bool linkedCopyIsOptimized() {
#ifdef __OPTIMIZE__
  return true;
#else
  return false;
#endif
}

namespace FOLLY_F14_MIXED_OPT_NS {

namespace {

// kind: 0 = F14ValueMap, 1 = F14NodeMap, 2 = F14VectorMap.
template <typename F>
decltype(auto) withMap(MixedOptMaps& maps, int kind, F&& f) {
  if (kind == 0) {
    return f(maps.value);
  }
  if (kind == 1) {
    return f(maps.node);
  }
  return f(maps.vector);
}

#if FOLLY_F14_VECTOR_INTRINSICS_AVAILABLE
template <typename Item>
std::size_t firstEmptyResultSize() {
  return sizeof(
      decltype(std::declval<detail::F14Chunk<Item> const&>().firstEmpty(
          std::declval<detail::TagVector>())));
}
#endif

} // namespace

MixedOptMaps* makeMaps() {
  return new MixedOptMaps();
}

void destroyMaps(MixedOptMaps* maps) {
  delete maps;
}

void insertKeys(MixedOptMaps& maps, int kind, int begin, int end) {
  withMap(maps, kind, [&](auto& map) {
    for (int k = begin; k < end; ++k) {
      map.emplace(k, 2 * k);
    }
  });
}

void eraseKeys(MixedOptMaps& maps, int kind, int begin, int end) {
  withMap(maps, kind, [&](auto& map) {
    for (int k = begin; k < end; ++k) {
      map.erase(k);
    }
  });
}

void eraseEvenKeysWhileIterating(MixedOptMaps& maps, int kind) {
  withMap(maps, kind, [&](auto& map) {
    for (auto it = map.begin(); it != map.end();) {
      if (it->first % 2 == 0) {
        it = map.erase(it);
      } else {
        ++it;
      }
    }
  });
}

// Keys in [begin, end) found with the value insertKeys() gave them.
int countFound(MixedOptMaps& maps, int kind, int begin, int end) {
  return withMap(maps, kind, [&](auto& map) {
    int found = 0;
    for (int k = begin; k < end; ++k) {
      auto it = map.find(k);
      found += it != map.end() && it->second == 2 * k;
    }
    return found;
  });
}

std::size_t size(MixedOptMaps& maps, int kind) {
  return withMap(maps, kind, [](auto& map) { return map.size(); });
}

long sumByIteration(MixedOptMaps& maps, int kind) {
  return withMap(maps, kind, [](auto& map) {
    long sum = 0;
    for (auto const& [key, value] : map) {
      sum += value;
    }
    return sum;
  });
}

bool isOptimized() {
#ifdef __OPTIMIZE__
  return true;
#else
  return false;
#endif
}

bool callLinkedCopyIsOptimized() {
  return linkedCopyIsOptimized();
}

// The F14 choices that must be the same in every file of a program.
std::string isaFacts() {
  std::string facts = "FOLLY_F14_DETAIL_ISA_SVE2_WITH_BRIDGE=" +
      folly::to<std::string>(FOLLY_F14_DETAIL_ISA_SVE2_WITH_BRIDGE) +
      " FOLLY_F14_SVE_PREDICATE_NATIVE_ACTIVE=" +
      folly::to<std::string>(FOLLY_F14_SVE_PREDICATE_NATIVE_ACTIVE);
#if FOLLY_F14_VECTOR_INTRINSICS_AVAILABLE
  facts +=
      " sizeof(firstEmpty() result)=" +
      folly::to<std::string>(
          firstEmptyResultSize<std::pair<int const, int>>()) +
      "," +
      folly::to<std::string>(
          firstEmptyResultSize<std::pair<int const, int>*>()) +
      "," + folly::to<std::string>(firstEmptyResultSize<std::uint32_t>());
#endif
  return facts;
}

} // namespace FOLLY_F14_MIXED_OPT_NS

} // namespace folly::f14::test
