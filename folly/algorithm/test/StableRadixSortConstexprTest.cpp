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

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <type_traits>
#include <vector>

using namespace folly;

namespace {
// ---------------------------------------------------------------------------
// constexpr helpers
// ---------------------------------------------------------------------------

template <typename T, size_t N>
constexpr bool isSortedArray(const std::array<T, N>& a) {
  for (size_t i = 1; i < N; ++i) {
    if (a[i - 1] > a[i]) {
      return false;
    }
  }
  return true;
}

template <typename T>
constexpr bool isSortedVec(const std::vector<T>& v) {
  for (size_t i = 1; i < v.size(); ++i) {
    if (v[i - 1] > v[i]) {
      return false;
    }
  }
  return true;
}

// ===========================================================================
// constexpr (static_assert) — only tests small enough for Clang's constexpr
// evaluator (no new[] support).  All ≤64 elements.
// ===========================================================================

// ---------------------------------------------------------------------------
// LSD Seq — array, 32 elements (int32_t)
// ---------------------------------------------------------------------------

constexpr bool lsdSeqArraySmall() {
  std::array<int32_t, 32> a = {
      5,  31, 1, 16, 4,  28, 9, 22, 14, 7,  29, 11, 25, 3,  18, 6,
      20, 13, 2, 30, 10, 27, 8, 24, 15, 12, 26, 17, 23, 19, 21, 0};
  stable_radix_sort(a.begin(), a.end());
  return isSortedArray(a);
}
static_assert(lsdSeqArraySmall());

// ---------------------------------------------------------------------------
// LSD Seq — vector, 48 elements (int64_t)
// ---------------------------------------------------------------------------

constexpr bool lsdSeqVecSmall() {
  std::vector<int64_t> v = {
      -10, 50,  -30, 80,   -50, 20,  -70, 40,  90,  -100, 60,  -80,
      10,  -40, 70,  -20,  30,  -90, 0,   100, -60, 55,   -35, 75,
      -45, 25,  -65, 45,   85,  -95, 65,  -75, 15,  -25,  35,  -85,
      5,   -55, 95,  -105, 52,  -38, 78,  -48, 22,  -68,  42,  88};
  stable_radix_sort(v.begin(), v.end());
  return isSortedVec(v);
}
static_assert(lsdSeqVecSmall());

// ---------------------------------------------------------------------------
// LSD Seq — array, 50 elements, descending (int32_t)
// ---------------------------------------------------------------------------

constexpr bool lsdSeqDescending() {
  constexpr auto kDescOpts = [] {
    auto opts = RadixSortOptions{};
    opts.SortOrder = RadixSortOrder::Descending;
    return opts;
  }();

  std::array<int32_t, 50> a = {};
  for (size_t i = 0; i < 50; ++i) {
    a[i] = static_cast<int32_t>(i - 25);
  }
  stable_radix_sort<kDescOpts>(a.begin(), a.end());
  for (size_t i = 1; i < 50; ++i) {
    if (a[i - 1] < a[i]) {
      return false;
    }
  }
  return true;
}
static_assert(lsdSeqDescending());

// ---------------------------------------------------------------------------
// MSD Seq — array, 40 elements (int16_t)
// ---------------------------------------------------------------------------

constexpr bool msdSeqArraySmall() {
  constexpr auto kMsdOpts = [] {
    auto opts = RadixSortOptions{};
    opts.SortStrategy = RadixSortStrategy::Msd;
    opts.ExecutionPolicy = RadixExecutionPolicy::Seq;
    return opts;
  }();

  // int8_t only has 1 pass with 8-bit chunks → MSD needs ≥ 2 passes.
  std::array<int16_t, 40> a = {
      500,  -1000, 3000, -5000, 1500, -2500, 4500, -6500, 1000, -3000,
      5000, -7000, 2000, -4000, 6000, -8000, 3500, -5500, 500,  -1500,
      2500, -3500, 4000, -6000, 5500, -7500, 1000, -2000, 3000, -4500,
      6500, -8500, 2000, -5000, 4500, -6500, 1500, -2500, 4000, -6000};
  stable_radix_sort<kMsdOpts>(a.begin(), a.end());
  return isSortedArray(a);
}
static_assert(msdSeqArraySmall());

// ---------------------------------------------------------------------------
// MSD Seq — vector, 56 elements (int32_t)
// ---------------------------------------------------------------------------

constexpr bool msdSeqVecSmall() {
  constexpr auto kMsdOpts = [] {
    auto opts = RadixSortOptions{};
    opts.SortStrategy = RadixSortStrategy::Msd;
    opts.ExecutionPolicy = RadixExecutionPolicy::Seq;
    return opts;
  }();

  std::vector<int32_t> v = {
      100, -200, 300, -400, 500, -600, 700, -800, 150, -250, 350, -450,
      550, -650, 750, -850, 200, -300, 400, -500, 600, -700, 800, -900,
      100, -150, 250, -350, 450, -550, 650, -750, 850, -950, 50,  -100,
      175, -275, 375, -475, 575, -675, 775, -875, 125, -225, 325, -425,
      525, -625, 725, -825, 925, -125, 225, -325, 425};
  stable_radix_sort<kMsdOpts>(v.begin(), v.end());
  return isSortedVec(v);
}
static_assert(msdSeqVecSmall());

// ===========================================================================
// Runtime-only tests — >64 elements.
// Clang's constexpr evaluator does not support new[], and
// AllocatorHolder (the sort's temp-buffer allocator) uses new[].
// ===========================================================================

// ---------------------------------------------------------------------------
// LSD Seq — array, 128 elements (uint64_t)
// ---------------------------------------------------------------------------

void lsdSeqArrayLarge() {
  std::array<uint64_t, 128> a = {};
  for (size_t i = 0; i < 128; ++i) {
    a[i] = (i < 64) ? (127 - i) * 1000ULL : (i - 64) * 7ULL;
  }
  stable_radix_sort(a.begin(), a.end());
  assert(isSortedArray(a));
}

// ---------------------------------------------------------------------------
// LSD Seq — vector, 200 elements (uint32_t)
// ---------------------------------------------------------------------------

void lsdSeqVecLarge() {
  std::vector<uint32_t> v(200);
  for (size_t i = 0; i < 200; ++i) {
    v[i] = static_cast<uint32_t>((199 - i) * 3u + 7u);
  }
  stable_radix_sort(v.begin(), v.end());
  assert(isSortedVec(v));
}

// ---------------------------------------------------------------------------
// MSD Seq — array, 100 elements (uint16_t)
// ---------------------------------------------------------------------------

void msdSeqArrayLarge() {
  constexpr auto kMsdOpts = [] {
    auto opts = RadixSortOptions{};
    opts.SortStrategy = RadixSortStrategy::Msd;
    opts.ExecutionPolicy = RadixExecutionPolicy::Seq;
    return opts;
  }();

  std::array<uint16_t, 100> a = {};
  for (size_t i = 0; i < 100; ++i) {
    a[i] = static_cast<uint16_t>((99 - i) * 657u + 13u);
  }
  stable_radix_sort<kMsdOpts>(a.begin(), a.end());
  assert(isSortedArray(a));
}

// ---------------------------------------------------------------------------
// MSD Seq — vector, 150 elements (uint64_t)
// ---------------------------------------------------------------------------

void msdSeqVecLarge() {
  constexpr auto kMsdOpts = [] {
    auto opts = RadixSortOptions{};
    opts.SortStrategy = RadixSortStrategy::Msd;
    opts.ExecutionPolicy = RadixExecutionPolicy::Seq;
    return opts;
  }();

  std::vector<uint64_t> v(150);
  for (size_t i = 0; i < 150; ++i) {
    v[i] = (149 - i) * 123456789ULL + 42ULL;
  }
  stable_radix_sort<kMsdOpts>(v.begin(), v.end());
  assert(isSortedVec(v));
}

// ---------------------------------------------------------------------------
// MSD Seq — array, 80 elements, descending (uint32_t)
// ---------------------------------------------------------------------------

void msdSeqDescending() {
  constexpr auto kDescMsdOpts = [] {
    auto opts = RadixSortOptions{};
    opts.SortStrategy = RadixSortStrategy::Msd;
    opts.ExecutionPolicy = RadixExecutionPolicy::Seq;
    opts.SortOrder = RadixSortOrder::Descending;
    return opts;
  }();

  std::array<uint32_t, 80> a = {};
  for (size_t i = 0; i < 80; ++i) {
    a[i] = static_cast<uint32_t>(i * 17u + 3u);
  }
  stable_radix_sort<kDescMsdOpts>(a.begin(), a.end());
  for (size_t i = 1; i < 80; ++i) {
    assert(a[i - 1] >= a[i]);
  }
}
} // namespace
int main() {
  lsdSeqArrayLarge();
  lsdSeqVecLarge();
  msdSeqArrayLarge();
  msdSeqVecLarge();
  msdSeqDescending();
  return 0;
}
