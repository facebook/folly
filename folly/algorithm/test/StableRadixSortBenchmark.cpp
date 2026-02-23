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

#include <folly/algorithm/StableRadixSort.h>

#include <folly/stats/detail/DoubleRadixSort.h>

#include <boost/sort/pdqsort/pdqsort.hpp>
#include <boost/sort/spreadsort/float_sort.hpp>
#include <boost/sort/spreadsort/integer_sort.hpp>
#include <folly/Benchmark.h>
#include <folly/init/Init.h>

#include <algorithm>
#include <cstdint>
#include <random>
#include <span>
#include <type_traits>
#include <vector>

namespace {

constexpr uint32_t kSeed = 42;

template <typename T>
std::span<const T> generateData(size_t n) {
  static std::vector<T> data = [] {
    std::vector<T> v(100000);
    std::mt19937_64 gen(kSeed);
    if constexpr (std::is_floating_point_v<T>) {
      std::uniform_real_distribution<T> dist(-1e9, 1e9);
      std::generate(v.begin(), v.end(), [&] { return dist(gen); });
    } else {
      std::uniform_int_distribution<T> dist;
      std::generate(v.begin(), v.end(), [&] { return dist(gen); });
    }
    return v;
  }();
  return {data.data(), n};
}

template <typename T>
void benchmarkStdSort(size_t n) {
  auto data = generateData<T>(n);
  std::vector<T> copy(data.begin(), data.end());
  std::stable_sort(copy.begin(), copy.end());
  folly::doNotOptimizeAway(copy);
}

template <typename T>
void benchmarkRadixSort(size_t n) {
  auto data = generateData<T>(n);
  std::vector<T> copy(data.begin(), data.end());
  folly::stable_radix_sort(copy.begin(), copy.end());
  folly::doNotOptimizeAway(copy);
}

template <typename T>
void benchmarkSpreadsort(size_t n) {
  auto data = generateData<T>(n);
  std::vector<T> copy(data.begin(), data.end());
  if constexpr (std::is_floating_point_v<T>) {
    boost::sort::spreadsort::float_sort(copy.begin(), copy.end());
  } else {
    boost::sort::spreadsort::integer_sort(copy.begin(), copy.end());
  }
  folly::doNotOptimizeAway(copy);
}

template <typename T>
void benchmarkPdqsort(size_t n) {
  auto data = generateData<T>(n);
  std::vector<T> copy(data.begin(), data.end());
  boost::sort::pdqsort(copy.begin(), copy.end());
  folly::doNotOptimizeAway(copy);
}

void benchmarkDoubleDetailRadixSort(size_t n) {
  auto data = generateData<double>(n);
  std::vector<double> copy(data.begin(), data.end());
  std::vector<double> tmp(n);
  std::vector<uint64_t> buckets(256 * 9);
  folly::detail::double_radix_sort(n, buckets.data(), copy.data(), tmp.data());
  folly::doNotOptimizeAway(copy);
}

} // namespace

// clang-format off
#define BENCHMARK_UINT64(N) \
  BENCHMARK(Uint64_StdStableSort_##N) { benchmarkStdSort<uint64_t>(N); } \
  BENCHMARK(Uint64_RadixSort_##N) { benchmarkRadixSort<uint64_t>(N); } \
  BENCHMARK(Uint64_Spreadsort_##N) { benchmarkSpreadsort<uint64_t>(N); } \
  BENCHMARK(Uint64_Pdqsort_##N) { benchmarkPdqsort<uint64_t>(N); }

#define BENCHMARK_INT64(N) \
  BENCHMARK(Int64_StdStableSort_##N) { benchmarkStdSort<int64_t>(N); } \
  BENCHMARK(Int64_RadixSort_##N) { benchmarkRadixSort<int64_t>(N); } \
  BENCHMARK(Int64_Spreadsort_##N) { benchmarkSpreadsort<int64_t>(N); } \
  BENCHMARK(Int64_Pdqsort_##N) { benchmarkPdqsort<int64_t>(N); }

#define BENCHMARK_DOUBLE(N) \
  BENCHMARK(Double_StdStableSort_##N) { benchmarkStdSort<double>(N); } \
  BENCHMARK(Double_RadixSort_##N) { benchmarkRadixSort<double>(N); } \
  BENCHMARK(Double_Spreadsort_##N) { benchmarkSpreadsort<double>(N); } \
  BENCHMARK(Double_Pdqsort_##N) { benchmarkPdqsort<double>(N); } \
  BENCHMARK(Double_DetailRadixSort_##N) { benchmarkDoubleDetailRadixSort(N); }

#define BENCHMARK_ALL_TYPES(N) \
  BENCHMARK_UINT64(N) \
  BENCHMARK_DRAW_LINE(); \
  BENCHMARK_INT64(N) \
  BENCHMARK_DRAW_LINE(); \
  BENCHMARK_DOUBLE(N) \
  BENCHMARK_DRAW_LINE();
// clang-format on

BENCHMARK_ALL_TYPES(100)
BENCHMARK_ALL_TYPES(1000)
BENCHMARK_ALL_TYPES(10000)
BENCHMARK_ALL_TYPES(100000)

int main(int argc, char** argv) {
  folly::Init init(&argc, &argv);
  folly::runBenchmarks();
  return 0;
}
