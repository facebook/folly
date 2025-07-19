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

#include <folly/algorithm/LowerBound.h>

#include <algorithm>
#include <array>
#include <numeric>
#include <random>
#include <vector>

#include <fmt/core.h>

#include <folly/Benchmark.h>
#include <folly/container/span.h>
#include <folly/portability/GFlags.h>

namespace {

constexpr size_t kMaxLimit = 100'000'000;
constexpr size_t kMaxKeysLimit = 2 * kMaxLimit;
constexpr std::array kSizes{
    1,
    10,
    100,
    1'000,
    10'000,
    100'000,
    1'000'000,
    10'000'000,
    static_cast<int>(kMaxLimit)};
constexpr size_t kMaxFindIfSize = 1'000;

// Keys are in the range [0, 2 * kMaxLimit) and values are odd numbers up to
// kMaxLimit. This way half of keys are in the values array and half is
// not.
// See section 3 of Khuong, Paul-Virak, and Pat Morin «Array layouts for
// comparison-based searching» paper for more details.
std::vector<int> genKeys() {
  std::vector<int> vec(kMaxKeysLimit);
  std::iota(vec.begin(), vec.end(), 0);
  std::random_device rd;
  std::mt19937_64 g(rd());
  // We want to shuffle values in each interval: [0, 1), [1, 10), [10, 100),
  // etc, but do not mix values between sizes, since we are going to re-use
  // prefix of the array in the benchmark.
  size_t prev = 0;
  for (size_t size : kSizes) {
    std::shuffle(vec.begin() + prev, vec.begin() + size, g);
    prev = size;
  }
  return vec;
}

std::vector<int> genValues() {
  std::vector<int> vec;
  vec.reserve(kMaxLimit);
  for (size_t i = 0; i < kMaxLimit; ++i) {
    vec.push_back(2 * i + 1);
  }
  return vec;
}

folly::span<const int> getKeys(size_t n) {
  folly::BenchmarkSuspender _;
  CHECK(kMaxKeysLimit >= n);
  static const std::vector<int> keys = genKeys();
  auto beg = keys.begin();
  auto end = beg + n;
  return folly::span(beg, end);
}

folly::span<const int> getValues(size_t n) {
  folly::BenchmarkSuspender _;
  CHECK(kMaxLimit >= n);
  static const std::vector<int> values = genValues();
  auto beg = values.begin();
  auto end = beg + n;
  return folly::span(beg, end);
}

using Iter = folly::span<const int>::iterator;

Iter stdFindIf(folly::span<const int> s, int key) {
  return std::find_if(s.begin(), s.end(), [key](int value) {
    return !(value < key);
  });
}

Iter stdLowerBound(folly::span<const int> s, int key) {
  return std::lower_bound(s.begin(), s.end(), key);
}

Iter follyLowerBound(folly::span<const int> s, int key) {
  folly::detail::Options opts{.prefetch = folly::detail::Prefetch::DISABLED};
  return folly::detail::lowerBound(
      s.begin(), s.end(), key, std::less<>{}, opts);
}

Iter follyLowerBoundPrefetch(folly::span<const int> s, int key) {
  folly::detail::Options opts{.prefetch = folly::detail::Prefetch::ENABLED};
  return folly::detail::lowerBound(
      s.begin(), s.end(), key, std::less<>{}, opts);
}

template <typename F>
void search(F func, size_t k) {
  folly::span<const int> keys = getKeys(2 * k);
  folly::span<const int> values = getValues(k);

  int sum = 0;
  for (int key : keys) {
    auto it = func(values, key);
    sum += (it == values.end()) ? -1 : *it;
  }
  folly::doNotOptimizeAway(sum);
}

void addBenchmarks() {
  for (size_t k : kSizes) {
    // std::find_if is too slow for big k and it not that interesting, so skip
    // it for the sake of saving time.
    if (k <= kMaxFindIfSize) {
      std::string name = fmt::format("std::find_if: k={}", k);
      folly::addBenchmark(__FILE__, name, [k]() {
        search(stdFindIf, k);
        return 1;
      });
    }

    std::string name = fmt::format("std::lower_bound: k={}", k);
    folly::addBenchmark(__FILE__, name, [k]() {
      search(stdLowerBound, k);
      return 1;
    });

    name = fmt::format("folly::lower_bound: k={}", k);
    folly::addBenchmark(__FILE__, name, [k]() {
      search(follyLowerBound, k);
      return 1;
    });

    name = fmt::format("folly::lower_bound (prefetch): k={}", k);
    folly::addBenchmark(__FILE__, name, [k]() {
      search(follyLowerBoundPrefetch, k);
      return 1;
    });

    // Draw a separator line.
    folly::addBenchmark(__FILE__, "-", []() { return 0; });
  }
}

} // namespace

int main(int argc, char** argv) {
  folly::gflags::ParseCommandLineFlags(&argc, &argv, true);
  addBenchmarks();
  folly::runBenchmarks();
  return 0;
}
