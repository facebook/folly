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

#include <folly/Benchmark.h>
#include <folly/MapUtil.h>
#include <folly/init/Init.h>
#include <folly/sorted_vector_types.h>

#include <string>

static constexpr auto kMediumMapSize = 30;
static constexpr auto kLargeMapSize = 10000;

template <typename T, size_t size>
folly::sorted_vector_map<int, T> makeMap() {
  folly::sorted_vector_map<int, T> map;
  for (size_t ii = 0; ii < size; ++ii) {
    // NOLINTNEXTLINE(facebook-expensive-flat-container-operation)
    map.emplace(ii, T());
  }
  return map;
}

template <typename T>
auto makeMediumMap() {
  return makeMap<T, kMediumMapSize>();
}

template <typename T>
auto makeLargeMap() {
  return makeMap<T, kLargeMapSize>();
}

#define BENCHMARK_SUITE(value_type)                                   \
  BENCHMARK(value_type##_empty_get_ptr, iters) {                      \
    folly::sorted_vector_map<int, value_type> map;                    \
    folly::makeUnpredictable(map);                                    \
    while (iters--) {                                                 \
      auto* p = folly::get_ptr(map, 42);                              \
      folly::doNotOptimizeAway(p);                                    \
    }                                                                 \
  }                                                                   \
                                                                      \
  BENCHMARK(value_type##_small_get_ptr_not_found, iters) {            \
    folly::sorted_vector_map<int, value_type> map;                    \
    folly::makeUnpredictable(map);                                    \
    /* NOLINTNEXTLINE(facebook-expensive-flat-container-operation) */ \
    map.emplace(40, value_type());                                    \
    /* NOLINTNEXTLINE(facebook-expensive-flat-container-operation) */ \
    map.emplace(1000, value_type());                                  \
    while (iters--) {                                                 \
      auto* p = folly::get_ptr(map, 42);                              \
      folly::doNotOptimizeAway(p);                                    \
    }                                                                 \
  }                                                                   \
                                                                      \
  BENCHMARK(value_type##_small_get_ptr_found, iters) {                \
    folly::sorted_vector_map<int, value_type> map;                    \
    folly::makeUnpredictable(map);                                    \
    /* NOLINTNEXTLINE(facebook-expensive-flat-container-operation) */ \
    map.emplace(40, value_type());                                    \
    /* NOLINTNEXTLINE(facebook-expensive-flat-container-operation) */ \
    map.emplace(1000, value_type());                                  \
    while (iters--) {                                                 \
      auto* p = folly::get_ptr(map, 40);                              \
      folly::doNotOptimizeAway(p);                                    \
    }                                                                 \
  }                                                                   \
                                                                      \
  BENCHMARK(value_type##_medium_get_ptr_not_found, iters) {           \
    auto map = makeMediumMap<value_type>();                           \
    folly::makeUnpredictable(map);                                    \
    while (iters--) {                                                 \
      auto* p = folly::get_ptr(map, kMediumMapSize + 1);              \
      folly::doNotOptimizeAway(p);                                    \
    }                                                                 \
  }                                                                   \
                                                                      \
  BENCHMARK(value_type##_medium_get_ptr_found, iters) {               \
    auto map = makeMediumMap<value_type>();                           \
    folly::makeUnpredictable(map);                                    \
    while (iters--) {                                                 \
      auto* p = folly::get_ptr(map, kMediumMapSize / 3);              \
      folly::doNotOptimizeAway(p);                                    \
    }                                                                 \
  }                                                                   \
                                                                      \
  BENCHMARK(value_type##_large_get_ptr_not_found, iters) {            \
    auto map = makeLargeMap<value_type>();                            \
    folly::makeUnpredictable(map);                                    \
    while (iters--) {                                                 \
      auto* p = folly::get_ptr(map, kLargeMapSize + 1);               \
      folly::doNotOptimizeAway(p);                                    \
    }                                                                 \
  }                                                                   \
                                                                      \
  BENCHMARK(value_type##_large_get_ptr_found, iters) {                \
    auto map = makeLargeMap<value_type>();                            \
    folly::makeUnpredictable(map);                                    \
    while (iters--) {                                                 \
      auto* p = folly::get_ptr(map, kLargeMapSize / 3);               \
      folly::doNotOptimizeAway(p);                                    \
    }                                                                 \
  }

using std::string;

using BigArray = std::array<char, 1024>;

BENCHMARK_SUITE(int)
BENCHMARK_DRAW_LINE();
BENCHMARK_SUITE(string)
BENCHMARK_DRAW_LINE();
BENCHMARK_SUITE(BigArray)

int main(int argc, char** argv) {
  folly::Init init(&argc, &argv);
  folly::runBenchmarks();
  return 0;
}
