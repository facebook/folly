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

#ifdef _MSC_VER
#include <ppl.h>
#endif

namespace {

constexpr uint32_t kSeed = 42;

constexpr auto kLsdSeq = folly::RadixSortOptions{
    .SortStrategy = folly::RadixSortStrategy::Lsd,
    .ExecutionPolicy = folly::RadixExecutionPolicy::Seq,
};

constexpr auto kMsdSeq = folly::RadixSortOptions{
    .SortStrategy = folly::RadixSortStrategy::Msd,
    .ExecutionPolicy = folly::RadixExecutionPolicy::Seq,
};

#ifdef _OPENMP
constexpr auto kLsdPar = folly::RadixSortOptions{
    .SortStrategy = folly::RadixSortStrategy::Lsd,
    .ExecutionPolicy = folly::RadixExecutionPolicy::Par,
};

constexpr auto kMsdPar = folly::RadixSortOptions{
    .SortStrategy = folly::RadixSortStrategy::Msd,
    .ExecutionPolicy = folly::RadixExecutionPolicy::Par,
};
#endif

template <typename T>
std::span<const T> generateData(size_t n) {
  static std::vector<T> data = [] {
    std::vector<T> v(1000000);
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

// --- Sequential benchmarks ---

template <typename T>
void benchmarkStdSort(size_t n) {
  auto data = generateData<T>(n);
  std::vector<T> copy(data.begin(), data.end());
  std::sort(copy.begin(), copy.end());
  folly::doNotOptimizeAway(copy);
}

template <typename T>
void benchmarkStdStableSort(size_t n) {
  auto data = generateData<T>(n);
  std::vector<T> copy(data.begin(), data.end());
  std::stable_sort(copy.begin(), copy.end());
  folly::doNotOptimizeAway(copy);
}

template <typename T>
void benchmarkRadixSortLsdSeq(size_t n) {
  auto data = generateData<T>(n);
  std::vector<T> copy(data.begin(), data.end());
  folly::radixSort<kLsdSeq>(copy.begin(), copy.end());
  folly::doNotOptimizeAway(copy);
}

template <typename T>
void benchmarkRadixSortMsdSeq(size_t n) {
  auto data = generateData<T>(n);
  std::vector<T> copy(data.begin(), data.end());
  folly::radixSort<kMsdSeq>(copy.begin(), copy.end());
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

#ifdef _OPENMP
// --- Parallel benchmarks (only with OpenMP) ---

#if defined(__cpp_lib_parallel_algorithm) || defined(_MSC_VER)
#include <execution>

template <typename T>
void benchmarkStdSortPar(size_t n) {
  auto data = generateData<T>(n);
  std::vector<T> copy(data.begin(), data.end());
  std::sort(std::execution::par, copy.begin(), copy.end());
  folly::doNotOptimizeAway(copy);
}

template <typename T>
void benchmarkStdStableSortPar(size_t n) {
  auto data = generateData<T>(n);
  std::vector<T> copy(data.begin(), data.end());
  std::stable_sort(std::execution::par, copy.begin(), copy.end());
  folly::doNotOptimizeAway(copy);
}
#endif

template <typename T>
void benchmarkRadixSortLsdPar(size_t n) {
  auto data = generateData<T>(n);
  std::vector<T> copy(data.begin(), data.end());
  folly::radixSort<kLsdPar>(copy.begin(), copy.end());
  folly::doNotOptimizeAway(copy);
}

template <typename T>
void benchmarkRadixSortMsdPar(size_t n) {
  auto data = generateData<T>(n);
  std::vector<T> copy(data.begin(), data.end());
  folly::radixSort<kMsdPar>(copy.begin(), copy.end());
  folly::doNotOptimizeAway(copy);
}

#ifdef _MSC_VER
template <typename T>
void benchmarkMsvcParallelRadixsort(size_t n) {
  auto data = generateData<T>(n);
  std::vector<T> copy(data.begin(), data.end());
  Concurrency::parallel_radixsort(copy.begin(), copy.end());
  folly::doNotOptimizeAway(copy);
}
#endif
#endif

} // namespace

// clang-format off

// ============================================================================
// Section 1: Sequential comparison
//   Baseline: std::sort
//   Compared: std::stable_sort, boost::spreadsort, boost::pdqsort,
//             folly radix LSD/Seq, MSD/Seq, DoubleRadixSort (double only)
// ============================================================================

// Uint64 — Sequential
#define BENCHMARK_UINT64_SEQ(N) \
  BENCHMARK(Uint64_StdSort_##N)          { benchmarkStdSort<uint64_t>(N); } \
  BENCHMARK_RELATIVE(Uint64_StableSort_##N)   { benchmarkStdStableSort<uint64_t>(N); } \
  BENCHMARK_RELATIVE(Uint64_Spreadsort_##N)   { benchmarkSpreadsort<uint64_t>(N); } \
  BENCHMARK_RELATIVE(Uint64_Pdqsort_##N)      { benchmarkPdqsort<uint64_t>(N); } \
  BENCHMARK_RELATIVE(Uint64_LsdSeq_##N)       { benchmarkRadixSortLsdSeq<uint64_t>(N); } \
  BENCHMARK_RELATIVE(Uint64_MsdSeq_##N)       { benchmarkRadixSortMsdSeq<uint64_t>(N); }

// Int64 — Sequential
#define BENCHMARK_INT64_SEQ(N) \
  BENCHMARK(Int64_StdSort_##N)          { benchmarkStdSort<int64_t>(N); } \
  BENCHMARK_RELATIVE(Int64_StableSort_##N)   { benchmarkStdStableSort<int64_t>(N); } \
  BENCHMARK_RELATIVE(Int64_Spreadsort_##N)   { benchmarkSpreadsort<int64_t>(N); } \
  BENCHMARK_RELATIVE(Int64_Pdqsort_##N)      { benchmarkPdqsort<int64_t>(N); } \
  BENCHMARK_RELATIVE(Int64_LsdSeq_##N)       { benchmarkRadixSortLsdSeq<int64_t>(N); } \
  BENCHMARK_RELATIVE(Int64_MsdSeq_##N)       { benchmarkRadixSortMsdSeq<int64_t>(N); }

// Int32 — Sequential
#define BENCHMARK_INT32_SEQ(N) \
  BENCHMARK(Int32_StdSort_##N)          { benchmarkStdSort<int32_t>(N); } \
  BENCHMARK_RELATIVE(Int32_StableSort_##N)   { benchmarkStdStableSort<int32_t>(N); } \
  BENCHMARK_RELATIVE(Int32_Spreadsort_##N)   { benchmarkSpreadsort<int32_t>(N); } \
  BENCHMARK_RELATIVE(Int32_Pdqsort_##N)      { benchmarkPdqsort<int32_t>(N); } \
  BENCHMARK_RELATIVE(Int32_LsdSeq_##N)       { benchmarkRadixSortLsdSeq<int32_t>(N); } \
  BENCHMARK_RELATIVE(Int32_MsdSeq_##N)       { benchmarkRadixSortMsdSeq<int32_t>(N); }

// Uint32 — Sequential
#define BENCHMARK_UINT32_SEQ(N) \
  BENCHMARK(Uint32_StdSort_##N)          { benchmarkStdSort<uint32_t>(N); } \
  BENCHMARK_RELATIVE(Uint32_StableSort_##N)   { benchmarkStdStableSort<uint32_t>(N); } \
  BENCHMARK_RELATIVE(Uint32_Spreadsort_##N)   { benchmarkSpreadsort<uint32_t>(N); } \
  BENCHMARK_RELATIVE(Uint32_Pdqsort_##N)      { benchmarkPdqsort<uint32_t>(N); } \
  BENCHMARK_RELATIVE(Uint32_LsdSeq_##N)       { benchmarkRadixSortLsdSeq<uint32_t>(N); } \
  BENCHMARK_RELATIVE(Uint32_MsdSeq_##N)       { benchmarkRadixSortMsdSeq<uint32_t>(N); }

// Double — Sequential (includes DoubleRadixSort)
#define BENCHMARK_DOUBLE_SEQ(N) \
  BENCHMARK(Double_StdSort_##N)          { benchmarkStdSort<double>(N); } \
  BENCHMARK_RELATIVE(Double_StableSort_##N)   { benchmarkStdStableSort<double>(N); } \
  BENCHMARK_RELATIVE(Double_Spreadsort_##N)   { benchmarkSpreadsort<double>(N); } \
  BENCHMARK_RELATIVE(Double_Pdqsort_##N)      { benchmarkPdqsort<double>(N); } \
  BENCHMARK_RELATIVE(Double_LsdSeq_##N)       { benchmarkRadixSortLsdSeq<double>(N); } \
  BENCHMARK_RELATIVE(Double_MsdSeq_##N)       { benchmarkRadixSortMsdSeq<double>(N); } \
  BENCHMARK_RELATIVE(Double_DetailRadix_##N)  { benchmarkDoubleDetailRadixSort(N); }

BENCHMARK_DRAW_TEXT("Uint64 — Sequential");
BENCHMARK_UINT64_SEQ(100)
BENCHMARK_DRAW_LINE();
BENCHMARK_UINT64_SEQ(1000)
BENCHMARK_DRAW_LINE();
BENCHMARK_UINT64_SEQ(10000)
BENCHMARK_DRAW_LINE();
BENCHMARK_UINT64_SEQ(100000)
BENCHMARK_DRAW_LINE();
BENCHMARK_UINT64_SEQ(1000000)

BENCHMARK_DRAW_TEXT("Int64 — Sequential");
BENCHMARK_INT64_SEQ(100)
BENCHMARK_DRAW_LINE();
BENCHMARK_INT64_SEQ(1000)
BENCHMARK_DRAW_LINE();
BENCHMARK_INT64_SEQ(10000)
BENCHMARK_DRAW_LINE();
BENCHMARK_INT64_SEQ(100000)
BENCHMARK_DRAW_LINE();
BENCHMARK_INT64_SEQ(1000000)

BENCHMARK_DRAW_TEXT("Double — Sequential");
BENCHMARK_DOUBLE_SEQ(100)
BENCHMARK_DRAW_LINE();
BENCHMARK_DOUBLE_SEQ(1000)
BENCHMARK_DRAW_LINE();
BENCHMARK_DOUBLE_SEQ(10000)
BENCHMARK_DRAW_LINE();
BENCHMARK_DOUBLE_SEQ(100000)
BENCHMARK_DRAW_LINE();
BENCHMARK_DOUBLE_SEQ(1000000)

BENCHMARK_DRAW_TEXT("Int32 — Sequential");
BENCHMARK_INT32_SEQ(100)
BENCHMARK_DRAW_LINE();
BENCHMARK_INT32_SEQ(1000)
BENCHMARK_DRAW_LINE();
BENCHMARK_INT32_SEQ(10000)
BENCHMARK_DRAW_LINE();
BENCHMARK_INT32_SEQ(100000)
BENCHMARK_DRAW_LINE();
BENCHMARK_INT32_SEQ(1000000)

BENCHMARK_DRAW_TEXT("Uint32 — Sequential");
BENCHMARK_UINT32_SEQ(100)
BENCHMARK_DRAW_LINE();
BENCHMARK_UINT32_SEQ(1000)
BENCHMARK_DRAW_LINE();
BENCHMARK_UINT32_SEQ(10000)
BENCHMARK_DRAW_LINE();
BENCHMARK_UINT32_SEQ(100000)
BENCHMARK_DRAW_LINE();
BENCHMARK_UINT32_SEQ(1000000)

#ifdef _OPENMP
// ============================================================================
// Section 2: Parallel comparison (requires OpenMP)
//   Baseline: std::sort(std::execution::par)
//   Compared: std::stable_sort(par), folly radix LSD/Par, MSD/Par,
//             MSVC parallel_radixsort (integral only, MSVC only)
// ============================================================================

#ifdef _MSC_VER
  #define BENCHMARK_UINT64_PAR(N) \
    BENCHMARK(Uint64_StdSortPar_##N)        { benchmarkStdSortPar<uint64_t>(N); } \
    BENCHMARK_RELATIVE(Uint64_StableSortPar_##N) { benchmarkStdStableSortPar<uint64_t>(N); } \
    BENCHMARK_RELATIVE(Uint64_LsdPar_##N)        { benchmarkRadixSortLsdPar<uint64_t>(N); } \
    BENCHMARK_RELATIVE(Uint64_MsdPar_##N)        { benchmarkRadixSortMsdPar<uint64_t>(N); } \
    BENCHMARK_RELATIVE(Uint64_MsvcRadix_##N)     { benchmarkMsvcParallelRadixsort<uint64_t>(N); }

  #define BENCHMARK_INT64_PAR(N) \
    BENCHMARK(Int64_StdSortPar_##N)        { benchmarkStdSortPar<int64_t>(N); } \
    BENCHMARK_RELATIVE(Int64_StableSortPar_##N) { benchmarkStdStableSortPar<int64_t>(N); } \
    BENCHMARK_RELATIVE(Int64_LsdPar_##N)        { benchmarkRadixSortLsdPar<int64_t>(N); } \
    BENCHMARK_RELATIVE(Int64_MsdPar_##N)        { benchmarkRadixSortMsdPar<int64_t>(N); } \
    BENCHMARK_RELATIVE(Int64_MsvcRadix_##N)     { benchmarkMsvcParallelRadixsort<int64_t>(N); }

  #define BENCHMARK_DOUBLE_PAR(N) \
    BENCHMARK(Double_StdSortPar_##N)        { benchmarkStdSortPar<double>(N); } \
    BENCHMARK_RELATIVE(Double_StableSortPar_##N) { benchmarkStdStableSortPar<double>(N); } \
    BENCHMARK_RELATIVE(Double_LsdPar_##N)        { benchmarkRadixSortLsdPar<double>(N); } \
    BENCHMARK_RELATIVE(Double_MsdPar_##N)        { benchmarkRadixSortMsdPar<double>(N); }

  #define BENCHMARK_INT32_PAR(N) \
    BENCHMARK(Int32_StdSortPar_##N)        { benchmarkStdSortPar<int32_t>(N); } \
    BENCHMARK_RELATIVE(Int32_StableSortPar_##N) { benchmarkStdStableSortPar<int32_t>(N); } \
    BENCHMARK_RELATIVE(Int32_LsdPar_##N)        { benchmarkRadixSortLsdPar<int32_t>(N); } \
    BENCHMARK_RELATIVE(Int32_MsdPar_##N)        { benchmarkRadixSortMsdPar<int32_t>(N); } \
    BENCHMARK_RELATIVE(Int32_MsvcRadix_##N)     { benchmarkMsvcParallelRadixsort<int32_t>(N); }

  #define BENCHMARK_UINT32_PAR(N) \
    BENCHMARK(Uint32_StdSortPar_##N)        { benchmarkStdSortPar<uint32_t>(N); } \
    BENCHMARK_RELATIVE(Uint32_StableSortPar_##N) { benchmarkStdStableSortPar<uint32_t>(N); } \
    BENCHMARK_RELATIVE(Uint32_LsdPar_##N)        { benchmarkRadixSortLsdPar<uint32_t>(N); } \
    BENCHMARK_RELATIVE(Uint32_MsdPar_##N)        { benchmarkRadixSortMsdPar<uint32_t>(N); } \
    BENCHMARK_RELATIVE(Uint32_MsvcRadix_##N)     { benchmarkMsvcParallelRadixsort<uint32_t>(N); }
#else
  #define BENCHMARK_UINT64_PAR(N) \
    BENCHMARK(Uint64_StdSortPar_##N)        { benchmarkStdSortPar<uint64_t>(N); } \
    BENCHMARK_RELATIVE(Uint64_StableSortPar_##N) { benchmarkStdStableSortPar<uint64_t>(N); } \
    BENCHMARK_RELATIVE(Uint64_LsdPar_##N)        { benchmarkRadixSortLsdPar<uint64_t>(N); } \
    BENCHMARK_RELATIVE(Uint64_MsdPar_##N)        { benchmarkRadixSortMsdPar<uint64_t>(N); }

  #define BENCHMARK_INT64_PAR(N) \
    BENCHMARK(Int64_StdSortPar_##N)        { benchmarkStdSortPar<int64_t>(N); } \
    BENCHMARK_RELATIVE(Int64_StableSortPar_##N) { benchmarkStdStableSortPar<int64_t>(N); } \
    BENCHMARK_RELATIVE(Int64_LsdPar_##N)        { benchmarkRadixSortLsdPar<int64_t>(N); } \
    BENCHMARK_RELATIVE(Int64_MsdPar_##N)        { benchmarkRadixSortMsdPar<int64_t>(N); }

  #define BENCHMARK_DOUBLE_PAR(N) \
    BENCHMARK(Double_StdSortPar_##N)        { benchmarkStdSortPar<double>(N); } \
    BENCHMARK_RELATIVE(Double_StableSortPar_##N) { benchmarkStdStableSortPar<double>(N); } \
    BENCHMARK_RELATIVE(Double_LsdPar_##N)        { benchmarkRadixSortLsdPar<double>(N); } \
    BENCHMARK_RELATIVE(Double_MsdPar_##N)        { benchmarkRadixSortMsdPar<double>(N); }

  #define BENCHMARK_INT32_PAR(N) \
    BENCHMARK(Int32_StdSortPar_##N)        { benchmarkStdSortPar<int32_t>(N); } \
    BENCHMARK_RELATIVE(Int32_StableSortPar_##N) { benchmarkStdStableSortPar<int32_t>(N); } \
    BENCHMARK_RELATIVE(Int32_LsdPar_##N)        { benchmarkRadixSortLsdPar<int32_t>(N); } \
    BENCHMARK_RELATIVE(Int32_MsdPar_##N)        { benchmarkRadixSortMsdPar<int32_t>(N); }

  #define BENCHMARK_UINT32_PAR(N) \
    BENCHMARK(Uint32_StdSortPar_##N)        { benchmarkStdSortPar<uint32_t>(N); } \
    BENCHMARK_RELATIVE(Uint32_StableSortPar_##N) { benchmarkStdStableSortPar<uint32_t>(N); } \
    BENCHMARK_RELATIVE(Uint32_LsdPar_##N)        { benchmarkRadixSortLsdPar<uint32_t>(N); } \
    BENCHMARK_RELATIVE(Uint32_MsdPar_##N)        { benchmarkRadixSortMsdPar<uint32_t>(N); }
#endif

BENCHMARK_DRAW_TEXT("Uint64 — Parallel");
BENCHMARK_UINT64_PAR(100)
BENCHMARK_DRAW_LINE();
BENCHMARK_UINT64_PAR(1000)
BENCHMARK_DRAW_LINE();
BENCHMARK_UINT64_PAR(10000)
BENCHMARK_DRAW_LINE();
BENCHMARK_UINT64_PAR(100000)
BENCHMARK_DRAW_LINE();
BENCHMARK_UINT64_PAR(1000000)

BENCHMARK_DRAW_TEXT("Int64 — Parallel");
BENCHMARK_INT64_PAR(100)
BENCHMARK_DRAW_LINE();
BENCHMARK_INT64_PAR(1000)
BENCHMARK_DRAW_LINE();
BENCHMARK_INT64_PAR(10000)
BENCHMARK_DRAW_LINE();
BENCHMARK_INT64_PAR(100000)
BENCHMARK_DRAW_LINE();
BENCHMARK_INT64_PAR(1000000)

BENCHMARK_DRAW_TEXT("Double — Parallel");
BENCHMARK_DOUBLE_PAR(100)
BENCHMARK_DRAW_LINE();
BENCHMARK_DOUBLE_PAR(1000)
BENCHMARK_DRAW_LINE();
BENCHMARK_DOUBLE_PAR(10000)
BENCHMARK_DRAW_LINE();
BENCHMARK_DOUBLE_PAR(100000)
BENCHMARK_DRAW_LINE();
BENCHMARK_DOUBLE_PAR(1000000)

BENCHMARK_DRAW_TEXT("Int32 — Parallel");
BENCHMARK_INT32_PAR(100)
BENCHMARK_DRAW_LINE();
BENCHMARK_INT32_PAR(1000)
BENCHMARK_DRAW_LINE();
BENCHMARK_INT32_PAR(10000)
BENCHMARK_DRAW_LINE();
BENCHMARK_INT32_PAR(100000)
BENCHMARK_DRAW_LINE();
BENCHMARK_INT32_PAR(1000000)

BENCHMARK_DRAW_TEXT("Uint32 — Parallel");
BENCHMARK_UINT32_PAR(100)
BENCHMARK_DRAW_LINE();
BENCHMARK_UINT32_PAR(1000)
BENCHMARK_DRAW_LINE();
BENCHMARK_UINT32_PAR(10000)
BENCHMARK_DRAW_LINE();
BENCHMARK_UINT32_PAR(100000)
BENCHMARK_DRAW_LINE();
BENCHMARK_UINT32_PAR(1000000)
#endif

// clang-format on

int main(int argc, char** argv) {
  folly::Init init(&argc, &argv);
  folly::runBenchmarks();
  return 0;
}
