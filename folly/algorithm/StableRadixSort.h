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

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <type_traits>
#include <utility>

#include <folly/Traits.h>

namespace folly {

/**
 * Stable LSD (Least Significant Digit) Radix Sort
 *
 * A stable, O(n) radix sort implementation using counting sort with
 * alternating buffers. Unlike MSD radix sort (e.g., boost::spreadsort),
 * LSD radix sort processes digits from least to most significant, which
 * inherently preserves stability.
 *
 * Key features:
 * - Stable: elements with equal keys preserve their original relative order
 * - O(n) time complexity: k passes of O(n + 256) where k = sizeof(key)/8
 * - O(n) space complexity: requires a temporary buffer
 * - Supports ascending and descending order
 * - Supports custom projections for sorting by member fields
 * - Handles floating-point keys via IEEE 754 bit transformation
 *
 * Example usage:
 *
 *   // Sort vector of integers
 *   std::vector<uint64_t> values = {5, 2, 8, 1, 9};
 *   folly::stable_radix_sort(values.begin(), values.end());
 *
 *   // Sort by struct member
 *   struct Item { double score; int id; };
 *   std::vector<Item> items = ...;
 *   folly::stable_radix_sort(
 *       items.begin(), items.end(),
 *       [](const Item& item) { return item.score; });
 *
 *   // Sort descending
 *   folly::stable_radix_sort_descending(values.begin(), values.end());
 */

namespace stable_radix_sort_keys {

/**
 * FloatKey - Converts IEEE 754 floats/doubles to sortable unsigned integers.
 *
 * IEEE 754 floats are almost sortable in their bit representation, except:
 * - The sign bit inverts the comparison for negative numbers
 * - Negative numbers sort in reverse order
 *
 * This transformation maps the float ordering to unsigned integer ordering:
 * - -inf maps to 0x0000...
 * - -0.0 maps to 0x7FFF...  (just below +0.0)
 * - +0.0 maps to 0x8000...
 * - +inf maps to 0xFFFF...
 *
 * Template parameters:
 *   Float - float or double
 *   Descending - if true, inverts the key for descending sort
 */
template <typename Float, bool Descending = false>
struct FloatKey {
  static_assert(
      std::is_floating_point_v<Float>,
      "FloatKey requires a floating-point type");

  using UInt = uint_bits_t<sizeof(Float) * 8>;
  static_assert(sizeof(Float) == sizeof(UInt));

  UInt operator()(Float f) const noexcept {
    UInt bits = std::bit_cast<UInt>(f);
    constexpr UInt kSignBit = UInt{1} << (sizeof(UInt) * 8 - 1);
    // If negative: flip all bits (makes most negative -> 0)
    // If positive: flip only sign bit (makes positive > all negatives)
    UInt mask = (bits & kSignBit) ? ~UInt{0} : kSignBit;
    UInt sortable = bits ^ mask;
    return Descending ? ~sortable : sortable;
  }
};

/**
 * IntegralKey - Handles signed/unsigned integers for radix sort.
 *
 * For unsigned types: identity (no transformation needed)
 * For signed types: flips sign bit to map signed ordering to unsigned
 *
 * Template parameters:
 *   Int - any integral type
 *   Descending - if true, inverts the key for descending sort
 */
template <typename Int, bool Descending = false>
struct IntegralKey {
  static_assert(
      std::is_integral_v<Int>, "IntegralKey requires an integral type");

  using UInt = std::make_unsigned_t<Int>;

  UInt operator()(Int value) const noexcept {
    constexpr UInt kSignBit =
        std::is_signed_v<Int> ? (UInt{1} << (sizeof(UInt) * 8 - 1)) : 0;
    UInt result = static_cast<UInt>(value) ^ kSignBit;
    return Descending ? ~result : result;
  }
};

/**
 * IdentityKey - Returns the value unchanged (for unsigned integers).
 */
template <bool Descending = false>
struct IdentityKey {
  template <typename T>
  T operator()(T value) const noexcept {
    return Descending ? ~value : value;
  }
};

} // namespace stable_radix_sort_keys

namespace stable_radix_sort_detail {

// Size threshold below which we fall back to insertion sort
inline constexpr size_t kRadixSortThreshold = 16;

// Radix configuration
inline constexpr size_t kRadixBits = 8;
static_assert(kRadixBits == 8, "extractRadixDigit assumes 8-bit radix");
inline constexpr size_t kRadixBuckets = 1 << kRadixBits; // 256
inline constexpr size_t kRadixMask = kRadixBuckets - 1;

template <typename Key>
inline constexpr size_t kRadixPasses = sizeof(Key);

/**
 * Extract the radix digit at the given pass index from a key.
 * Pass 0 extracts the least significant digit.
 */
template <typename Key>
inline uint8_t extractRadixDigit(Key key, size_t pass) noexcept {
  static_assert(std::is_unsigned_v<Key>, "Key must be unsigned");
  // kRadixPasses ensures pass is always < sizeof(Key), so shift is safe
  return static_cast<uint8_t>((key >> (pass * kRadixBits)) & kRadixMask);
}

/**
 * Stable insertion sort for small inputs.
 * O(n^2) but fast for small n due to low overhead.
 */
template <typename RandomIt, typename Projection>
void insertionSort(RandomIt first, RandomIt last, Projection proj) {
  for (auto i = first; i != last; ++i) {
    auto key = proj(*i);
    auto j = i;
    while (j != first) {
      auto prev = j;
      --prev;
      if (proj(*prev) <= key) {
        break;
      }
      std::swap(*prev, *j);
      j = prev;
    }
  }
}

/**
 * Core LSD radix sort implementation with alternating buffers.
 *
 * Uses counting sort for each pass, processing from least to most
 * significant byte. The alternating buffer pattern avoids copying
 * data between passes - we simply swap source and destination pointers.
 */
template <typename T, typename Projection>
void stableRadixSortImpl(T* data, T* buffer, size_t n, Projection proj) {
  using Key = std::invoke_result_t<Projection, const T&>;
  static_assert(std::is_unsigned_v<Key>, "Key type must be unsigned");

  constexpr size_t kPasses = kRadixPasses<Key>;

  T* src = data;
  T* dst = buffer;

  for (size_t pass = 0; pass < kPasses; ++pass) {
    // Phase 1: Count occurrences of each byte value
    alignas(64) std::array<size_t, kRadixBuckets> counts{};
    for (size_t i = 0; i < n; ++i) {
      auto key = proj(src[i]);
      ++counts[extractRadixDigit(key, pass)];
    }

    // Check if this pass can be skipped (all elements have the same byte value)
    size_t nonzeroCount = 0;
    for (size_t c : counts) {
      if (c != 0 && ++nonzeroCount > 1) {
        break;
      }
    }
    if (nonzeroCount <= 1) {
      continue;
    }

    // Phase 2: Convert counts to starting offsets (prefix sum)
    alignas(64) std::array<size_t, kRadixBuckets> offsets{};
    size_t total = 0;
    for (size_t i = 0; i < kRadixBuckets; ++i) {
      offsets[i] = total;
      total += counts[i];
    }

    // Phase 3: Scatter elements to output (preserves stability)
    for (size_t i = 0; i < n; ++i) {
      auto key = proj(src[i]);
      size_t& offset = offsets[extractRadixDigit(key, pass)];
      dst[offset] = src[i];
      ++offset;
    }

    // Swap source and destination for next pass
    std::swap(src, dst);
  }

  // If the final result is in the buffer, copy it back to data
  if (src != data) {
    std::copy(src, src + n, data);
  }
}

/**
 * Wrapper that handles buffer allocation and fallback to std::stable_sort.
 */
template <typename ContiguousIt, typename Projection>
void stableRadixSort(ContiguousIt first, ContiguousIt last, Projection proj) {
  using T = typename std::iterator_traits<ContiguousIt>::value_type;
  static_assert(
      std::contiguous_iterator<ContiguousIt>,
      "stable_radix_sort requires contiguous iterators");

  size_t n = static_cast<size_t>(std::distance(first, last));

  // Fall back to insertion sort for small inputs (handles n <= 1 as well)
  if (n < kRadixSortThreshold) {
    insertionSort(first, last, proj);
    return;
  }

  // Allocate temporary buffer
  auto buffer = std::make_unique_for_overwrite<T[]>(n);

  // Get pointer to data
  T* data = std::to_address(first);

  stableRadixSortImpl(data, buffer.get(), n, proj);
}

/**
 * Default projection that handles common types.
 */
template <typename T, bool Descending>
struct DefaultProjection {
  auto operator()(const T& value) const noexcept {
    static_assert(
        std::is_floating_point_v<T> || is_non_bool_integral_v<T>,
        "stable_radix_sort requires floating-point or integral types");
    if constexpr (std::is_floating_point_v<T>) {
      return stable_radix_sort_keys::FloatKey<T, Descending>{}(value);
    } else {
      return stable_radix_sort_keys::IntegralKey<T, Descending>{}(value);
    }
  }
};

/**
 * Projection wrapper that applies type-appropriate transformation.
 */
template <bool Descending, typename Projection>
struct TransformingProjection {
  Projection inner;

  template <typename T>
  auto operator()(const T& value) const noexcept {
    auto extracted = inner(value);
    using Extracted = decltype(extracted);
    static_assert(
        std::is_floating_point_v<Extracted> ||
            is_non_bool_integral_v<Extracted>,
        "projection must return floating-point or integral");
    if constexpr (std::is_floating_point_v<Extracted>) {
      return stable_radix_sort_keys::FloatKey<Extracted, Descending>{}(
          extracted);
    } else if constexpr (std::is_signed_v<Extracted>) {
      return stable_radix_sort_keys::IntegralKey<Extracted, Descending>{}(
          extracted);
    } else {
      return Descending ? ~extracted : extracted;
    }
  }
};

} // namespace stable_radix_sort_detail

/**
 * stable_radix_sort - Sort elements in ascending order.
 *
 * Sorts elements using LSD radix sort. This is a stable sort: elements
 * with equal keys preserve their original relative order.
 *
 * Note: For floating-point types, the sort uses an augmented ordering
 * equivalent to std::stable_sort(first, last, augmented_less) where:
 *
 *   template <std::floating_point T>
 *   struct augmented_less {
 *     bool operator()(T lhs, T rhs) const noexcept {
 *       if (lhs < rhs) return true;
 *       if (rhs < lhs) return false;
 *       return std::signbit(lhs) > std::signbit(rhs);
 *     }
 *   };
 *
 * In other words, -0.0 sorts before +0.0. For all other values, the
 * ordering matches operator<.
 *
 * Requirements:
 * - ContiguousIt must be a contiguous iterator
 * - Value type must be floating-point or integral
 *
 * Complexity: O(n * k) where k = sizeof(value_type) / 8
 * Space: O(n) for temporary buffer
 */
template <typename ContiguousIt>
void stable_radix_sort(ContiguousIt first, ContiguousIt last) {
  using T = typename std::iterator_traits<ContiguousIt>::value_type;
  stable_radix_sort_detail::stableRadixSort(
      first, last, stable_radix_sort_detail::DefaultProjection<T, false>{});
}

/**
 * stable_radix_sort - Sort elements by extracted key in ascending order.
 *
 * Sorts elements by the key returned by proj. The key must be a type
 * that can be sorted by radix sort (floating-point, integral, or unsigned).
 *
 * Example:
 *   struct Item { double score; int id; };
 *   std::vector<Item> items = ...;
 *   folly::stable_radix_sort(
 *       items.begin(), items.end(),
 *       [](const Item& item) { return item.score; });
 */
template <typename ContiguousIt, typename Projection>
void stable_radix_sort(ContiguousIt first, ContiguousIt last, Projection proj) {
  using Wrapped =
      stable_radix_sort_detail::TransformingProjection<false, Projection>;
  stable_radix_sort_detail::stableRadixSort(first, last, Wrapped{proj});
}

/**
 * stable_radix_sort_descending - Sort elements in descending order.
 *
 * Like stable_radix_sort, but produces descending order.
 */
template <typename ContiguousIt>
void stable_radix_sort_descending(ContiguousIt first, ContiguousIt last) {
  using T = typename std::iterator_traits<ContiguousIt>::value_type;
  stable_radix_sort_detail::stableRadixSort(
      first, last, stable_radix_sort_detail::DefaultProjection<T, true>{});
}

/**
 * stable_radix_sort_descending - Sort by extracted key in descending order.
 *
 * Like stable_radix_sort with projection, but produces descending order.
 */
template <typename ContiguousIt, typename Projection>
void stable_radix_sort_descending(
    ContiguousIt first, ContiguousIt last, Projection proj) {
  using Wrapped =
      stable_radix_sort_detail::TransformingProjection<true, Projection>;
  stable_radix_sort_detail::stableRadixSort(first, last, Wrapped{proj});
}

} // namespace folly
