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

// Compile-time configurable radix sort supporting LSD/MSD traversal,
// sequential/parallel execution (via OpenMP), configurable digit width,
// sign handling for signed/floating-point types, and NaN positioning.
//
// For high-level usage see radixSort() and the RadixSortOptions struct.

#pragma once

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <concepts>
#include <iterator>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

#include <folly/ConstexprMath.h>
#include <folly/FollyMemset.h>
#include <folly/Traits.h>
#include <folly/container/Iterator.h>
#include <folly/container/hybrid_vector.h>
#include <folly/lang/Assume.h>
#ifdef _OPENMP
#include <omp.h>
#endif

namespace folly {
/**
 * @brief Radix sort traversal strategy.
 *
 * @details
 * - Lsd: Least Significant Digit first. Processes digits from least to most
 *   significant byte. Stable, iterative, and uses a single flat array.
 *
 * - Msd: Most Significant Digit first. Recursively partitions by the most
 *   significant digit. Forms a tree of sub-problems.
 *
 * # Memory layout & algorithm flow
 *
 * ## LSD (iterative, multi-pass)
 *
 *   Pass 1 (LSB)       Pass 2             Pass 3             Pass 4 (MSB)
 *   +----------------+ +----------------+ +----------------+ +----------------+
 *   | array -> buffer| | buffer -> array| | array -> buffer| | buffer -> array|
 *   +----------------+ +----------------+ +----------------+ +----------------+
 *   -> sorted
 *
 *   - Works on the whole array every pass.
 *   - Each pass scatters elements into 256 (or 65536) buckets.
 *   - No recursion, very good cache locality.
 *
 * ## MSD (recursive, depth-first)
 *
 *   Level 0 (byte 7)      Level 1 (byte 6)          Level 2 (byte 5) ...
 *   +-------------------------------------+
 *   |          whole array                |
 *   +-----------------+-------------------+
 *                     |
 *                     | split by most significant byte
 *              +------+------+
 *              |      |      |
 *        +-----+  +-----+  +-----+
 *        |bucket0|  |bucket1|  ... (up to 256 buckets)
 *        +--+---+  +--+---+
 *           |         |
 *           | recursive sort on next byte
 *        +--+---+  +--+---+
 *        |      |  |      |
 *       ...    ...  ...   ...
 *
 *   - High-order bits decide global order early.
 *   - Once a bucket contains <= threshold, fallback to LSD or insertion sort.
 *
 * # Performance considerations
 *
 * ## 32-bit keys (e.g., `uint32_t`)
 *
 *   - LSD with 8-bit chunks: only 4 passes, each pass scans the whole array.
 *   - Very predictable memory access, minimal recursion overhead.
 *   - Usually faster than MSD on modern CPUs (better cache and SIMD friendly).
 *
 * ## 64-bit keys (e.g., `uint64_t`)
 *
 *   - LSD with 8-bit chunks needs 8 passes -> more memory writes.
 *   - LSD with 16-bit chunks reduces passes to 4 but uses 65536 buckets,
 *     increasing histogram memory traffic.
 *   - MSD can be faster because the highest bytes often distribute data widely.
 *     After the first pass, sub-problems become much smaller, reducing total
 *     work. Recursion overhead is amortized over large inputs.
 *
 * ## Rule of thumb
 *
 *   - For 32-bit integers or small element types, prefer `Lsd`.
 *   - For 64-bit integers, especially with skewed distributions or when
 *     the most significant byte has high entropy, `Msd` may outperform `Lsd`.
 *   - Always benchmark with your actual data.
 */
enum class RadixSortStrategy { Lsd, Msd };

/**
 * @brief Execution policy for radix sort.
 *
 * @details
 * Controls whether the sort runs sequentially or uses parallel execution
 *
 * @note Parallel execution is only available when _OPENMP is defined.
 */
enum class RadixExecutionPolicy {
  Seq, // Sequential execution
  Par, // Parallel execution (requires OpenMP)
  /* Unseq,    */
  /* ParUnseq, */
};

/**
 * @brief Sort order for radix sort.
 */
enum class RadixSortOrder { Ascending, Descending };

/**
 * @brief Number of bits processed per radix pass.
 *
 * @details
 * Determines the bucket count as 2^RadixBitsPerPass.
 * - `Bits8`  : 8 bits per pass → 256 buckets, 4 passes for 32‑bit keys.
 * - `Bits16` : 16 bits per pass → 65536 buckets, 2 passes for 32‑bit keys.
 *
 * `Bits16` can be faster for large arrays because it halves the number of
 * passes, but the larger histogram incurs more memory traffic.
 */
enum class RadixBitsPerPass { Bits8 = 8, Bits16 = 16 };

/**
 * @brief Integer type used for bucket counters in the histogram.
 *
 * @details
 * - `UInt32` (32‑bit): Sufficient for arrays with fewer than 2³² elements.
 *   Uses less memory for the counter array.
 * - `UInt64` (64‑bit): Prevents overflow for arrays with 2³² or more elements,
 *   but doubles the memory footprint of the counter arrays compared to
 * `UInt32`.
 *
 * The choice affects both memory usage and the maximum array size that can
 * be sorted without counter overflow.
 */
enum class RadixHistogramStorageType { UInt32 = 32, UInt64 = 64 };

/**
 * @brief Controls where NaN values are placed in the sorted order
 *        when sorting floating-point types.
 *
 * @details
 * This enum is used together with `RadixNaNsAssumption`. When the assumption
 * is `Existence`, the sort will apply the selected positioning strategy.
 * When the assumption is `NonExistence`, this setting is ignored (no NaN
 * handling overhead).
 *
 * @warning Requires IEEE 754 compliance (std::numeric_limits<T>::is_iec559)
 *          to be guaranteed at compile time for predictable behavior.
 */
enum class RadixNaNsPosHandling {
  /// No special treatment. NaN values may appear in arbitrary positions
  /// because IEEE 754 does not define a total order for NaN.
  Unhandled,

  /// All NaN values are placed at the very beginning of the sorted output,
  /// before -inf (the smallest finite value).
  AtFirst,

  /// All NaN values are placed at the very end, after +inf.
  AtLast
};

/**
 * @brief Informs the sort whether the input floating‑point array may contain
 *        NaN values.
 *
 * @details
 * The default is `NonExistence`, which provides the best performance by
 * skipping all NaN‑related logic. If `Existence` is chosen, the sort will
 * use the strategy specified by `RadixNaNsPosHandling` to position NaN
 * values.
 *
 * @note This enum applies only to floating‑point types (float, double, etc.).
 *       For integer types, it has no effect.
 */
enum class RadixNaNsAssumption {
  /// NaN values may be present; the sort will use `RadixNaNsPosHandling`
  /// to determine their final position.
  Existence,

  /// Assume that no NaN values occur in the input. The sort will skip
  /// all NaN‑handling code, and `RadixNaNsPosHandling` is ignored.
  NonExistence
};

/**
 * @brief Complete set of compile-time options for radix sort.
 */
struct RadixSortOptions {
  RadixSortStrategy SortStrategy = RadixSortStrategy::Lsd;
  RadixExecutionPolicy ExecutionPolicy = RadixExecutionPolicy::Seq;
  RadixSortOrder SortOrder = RadixSortOrder::Ascending;
  RadixBitsPerPass BitsPerPass = RadixBitsPerPass::Bits8;
  RadixHistogramStorageType HistogramStorageType =
      RadixHistogramStorageType::UInt32;
  RadixNaNsPosHandling NaNsPosHandling = RadixNaNsPosHandling::Unhandled;
  RadixNaNsAssumption NaNsAssumption = RadixNaNsAssumption::NonExistence;

  /// MSD sub-problem size below which LSD is used
  size_t MsdFallbackToLsdThreshold = 256U * 256U;
  /// Stack-allocate histogram if under this
  size_t HistogramStackThresholdBytes = 16U * 1024U;
};

namespace detail {

// Allocator concepts and RAII
template <typename Alloc>
concept standard_allocator = requires(
    Alloc a, typename std::allocator_traits<Alloc>::size_type n) {
  typename std::allocator_traits<Alloc>::value_type;
  requires std::copy_constructible<Alloc>;
  requires std::same_as<
      typename std::allocator_traits<Alloc>::pointer,
      typename std::allocator_traits<Alloc>::value_type*>;
  {
    a.allocate(n)
  } -> std::same_as<typename std::allocator_traits<Alloc>::pointer>;
  {
    a.deallocate(
        std::declval<typename std::allocator_traits<Alloc>::pointer>(), n)
  } -> std::same_as<void>;
};

template <standard_allocator Alloc>
class AllocatorHolder {
  using Traits = std::allocator_traits<Alloc>;

 public:
  using value_type = typename Traits::value_type;
  using pointer = typename Traits::pointer;
  using size_type = typename Traits::size_type;

  explicit AllocatorHolder(size_type size, Alloc const& alloc)
      : alloc_(alloc),
        size_(size),
        buffer_(size == 0 ? nullptr : Traits::allocate(alloc_, size)) {
    construct();
  }

  AllocatorHolder(AllocatorHolder const&) = delete;
  AllocatorHolder& operator=(AllocatorHolder const&) = delete;

  ~AllocatorHolder() { destroy(); }

  pointer data() noexcept { return buffer_; }

 private:
  void construct() {
    if constexpr (
        !std::is_trivially_default_constructible_v<value_type> ||
        !std::is_trivially_destructible_v<value_type>) {
      size_type i = 0;
      try {
        for (; i < size_; ++i) {
          Traits::construct(alloc_, buffer_ + i);
        }
      } catch (...) {
        for (size_type j = 0; j < i; ++j) {
          Traits::destroy(alloc_, buffer_ + j);
        }
        Traits::deallocate(alloc_, buffer_, size_);
        throw;
      }
    }
  }

  void destroy() noexcept {
    if (buffer_ == nullptr) {
      return;
    }
    if constexpr (!std::is_trivially_destructible_v<value_type>) {
      for (size_type i = 0; i < size_; ++i) {
        Traits::destroy(alloc_, buffer_ + i);
      }
    }
    Traits::deallocate(alloc_, buffer_, size_);
  }

  [[no_unique_address]] Alloc alloc_;
  size_type size_{0};
  pointer buffer_{nullptr};
};

// User key maps must return arithmetic keys.
template <typename KeyMap, typename T>
concept arithmetic_key_map = std::regular_invocable<KeyMap, T> &&
    std::is_arithmetic_v<std::invoke_result_t<KeyMap, T>>;

// Internal projections must return unsigned radix keys.
template <typename Projection, typename T>
concept unsigned_integral_projection = requires(Projection p, T t) {
  requires std::regular_invocable<Projection, T>;
  { p(t) } -> std::unsigned_integral;
};

// Adjusts sort order for reverse iterators at compile time.
template <typename Iter, RadixSortOrder Order>
struct RadixRealSortOrder {
  static constexpr RadixSortOrder value = Order;
};

// Reverse iterators invert the requested sort order.
template <typename WrappedIter, RadixSortOrder Order>
struct RadixRealSortOrder<std::reverse_iterator<WrappedIter>, Order> {
  static constexpr RadixSortOrder value = (Order == RadixSortOrder::Ascending)
      ? RadixSortOrder::Descending
      : RadixSortOrder::Ascending;
};

// Default key map for arithmetic value types.
struct radix_key_map_fn {
  template <typename T>
  constexpr T operator()(T val) const noexcept {
    return val;
  }
};
inline constexpr radix_key_map_fn radix_key_map{};

///< Sign-bit traits

// Provides bit masks and indices for the sign bit of an arithmetic type.
template <typename T>
struct SignBitTraits {
  static_assert(std::is_arithmetic_v<T>, "arithmetic type required");
  using type = T;
  using uint_t = folly::uint_bits_t<sizeof(T) * CHAR_BIT>;
  static constexpr size_t kSignBitIndex = sizeof(T) * CHAR_BIT - 1;
  static constexpr uint_t kAllBitMask = ~uint_t(0);
  static constexpr uint_t kSignBitMask = kAllBitMask << kSignBitIndex;
};

///< Radix sort traits (compile-time parameter bundle)

// Compile-time parameter pack combining user options, element type, and key.
template <
    RadixSortOptions RadixOptions,
    typename T,
    detail::arithmetic_key_map<T> KeyMap>
struct RadixSortTraits : SignBitTraits<std::invoke_result_t<KeyMap, T>> {
  // Re-export user options
  static constexpr auto kSortStrategy = RadixOptions.SortStrategy;
  static constexpr auto kExecutionPolicy = RadixOptions.ExecutionPolicy;
  static constexpr auto kSortOrder = RadixOptions.SortOrder;
  static constexpr auto kNaNsPosHandling = RadixOptions.NaNsPosHandling;
  static constexpr auto kNaNsAssumption = RadixOptions.NaNsAssumption;
  static constexpr auto kHistogramStackThresholdBytes =
      RadixOptions.HistogramStackThresholdBytes;
  static constexpr auto kMsdFallbackToLsdThreshold =
      RadixOptions.MsdFallbackToLsdThreshold;

  // Core type aliases
  using value_t = T;
  using key_t = std::invoke_result_t<KeyMap, T>;
  using uint_t = typename SignBitTraits<key_t>::uint_t;
  using hist_t = std::conditional_t<
      RadixOptions.HistogramStorageType == RadixHistogramStorageType::UInt32,
      std::uint32_t,
      std::uint64_t>;

  // Radix parameters
  static constexpr size_t kBitsPerPass =
      static_cast<std::underlying_type_t<RadixBitsPerPass>>(
          RadixOptions.BitsPerPass);
  static constexpr size_t kNumPasses = sizeof(uint_t) * CHAR_BIT / kBitsPerPass;
  static constexpr size_t kNumBuckets = constexpr_pow(2, kBitsPerPass);
  static constexpr size_t kRadixMask = kNumBuckets - 1U;

  // Pass order (for LSD vs MSD)
  static constexpr size_t kFirstPass =
      kSortStrategy == RadixSortStrategy::Lsd ? 0 : kNumPasses - 1;
  static constexpr size_t kLastPass =
      kSortStrategy == RadixSortStrategy::Lsd ? kNumPasses - 1 : 0;

  // Whether sign correction is applied on the first pass
  static constexpr bool kIsSignHandlingOnFirstPass =
      (kSortStrategy == RadixSortStrategy::Msd || kNumPasses == 1) &&
      std::is_signed_v<key_t>;

  // Compile-time sequence of all pass indices
  static constexpr auto kPassIndices = std::make_index_sequence<kNumPasses>{};
};

///< Unsigned integer projection

/// Projects elements to unsigned integers for radix sorting.
/// Composes a user KeyMap with bit-level transformations for signed integers
/// and IEEE 754 floats, handling sign bits and NaN positioning.
struct radix_uint_projection_fn {
  template <
      RadixNaNsPosHandling NaNsPosHandling,
      typename Arithmetic,
      typename KeyMap>
  struct Projector {
    KeyMap km_;

    using Traits = SignBitTraits<Arithmetic>;
    using uint_t = typename Traits::uint_t;

    /// Transforms an arithmetic key into an unsigned integer.
    /// Unsigned → identity. Signed → flips sign bit (if IsSignBitHandling).
    /// IEEE 754 → maps negative floats to [0, 0x7F..FF], positive to
    /// [0x80..00, 0xFF..FF]; NaN goes to 0 or max if requested.
    template <bool IsSignBitHandling = !std::unsigned_integral<Arithmetic>>
    static constexpr auto transform(Arithmetic key) noexcept -> uint_t {
      if constexpr (
          std::same_as<Arithmetic, bool> ||
          std::unsigned_integral<Arithmetic>) {
        // Unsigned types need no transformation.
        return key;
      } else if constexpr (std::signed_integral<Arithmetic>) {
        // Two's complement signed integers: flip sign bit for correct order.
        return IsSignBitHandling
            ? static_cast<uint_t>(key) ^ Traits::kSignBitMask
            : static_cast<uint_t>(key);
      } else if constexpr (std::floating_point<Arithmetic>) {
        static_assert(
            !(NaNsPosHandling != RadixNaNsPosHandling::Unhandled &&
              !std::numeric_limits<Arithmetic>::is_iec559),
            "NaN pos handling requires IEEE 754 floating-point");

        // Handle NaNs if requested.
        if constexpr (NaNsPosHandling != RadixNaNsPosHandling::Unhandled) {
          if (std::isnan(key)) {
            // Map NaN to either the smallest or largest possible key so
            // that it sorts to the beginning or end of the sequence.
            return NaNsPosHandling == RadixNaNsPosHandling::AtFirst
                ? uint_t(0)
                : std::numeric_limits<uint_t>::max();
          }
        }
        const auto v = std::bit_cast<uint_t>(key);

        // signMask is 0 for positive floats, ~0 for negative.
        // Computed as 0 - (v >> kSignBitIndex) with unsigned wrap.
        const auto signMask = (uint_t(0) - (v >> Traits::kSignBitIndex));

        // Apply sign-bit flip and, for negatives, invert all bits except sign.
        uint_t base = IsSignBitHandling
            ? (v ^ signMask) | (Traits::kSignBitMask & ~signMask)
            : (v ^ signMask);

        // Shift finite values up by one if NaN occupies slot 0.
        if constexpr (NaNsPosHandling == RadixNaNsPosHandling::AtFirst) {
          base += 1U;
        }
        return base;
      } else {
        assume_unreachable();
      }
    }

    template <bool IsSignBitHandling = !std::unsigned_integral<Arithmetic>>
    constexpr auto operator()(const auto& x) const noexcept -> uint_t {
      return transform<IsSignBitHandling>(km_(x));
    }

    constexpr auto getKeyMap() const noexcept -> const KeyMap& { return km_; }
  };

  template <RadixNaNsPosHandling NaNsPosHandling, typename T, typename KeyMap>
  constexpr auto operator()(
      std::in_place_type_t<T>, KeyMap&& keyMap) const noexcept {
    using arithmetic_t = std::invoke_result_t<KeyMap, T>;
    return Projector<NaNsPosHandling, arithmetic_t, std::decay_t<KeyMap>>{
        std::forward<KeyMap>(keyMap)};
  }
};
inline constexpr radix_uint_projection_fn radix_uint_projection{};

///< Radix digit extraction

/// Returns the bucket index for a key in a given radix pass.
/// Shifts by `pass * kBitsPerPass` and masks with `kRadixMask`.
/// In descending order, inverts the key first.
template <typename RadixTraits>
#if !defined(DEBUG) && !defined(_DEBUG)
FOLLY_ALWAYS_INLINE
#endif
    constexpr auto nthRadix(
        typename RadixTraits::uint_t x, size_t pass) noexcept -> size_t {
  if constexpr (RadixTraits::kSortOrder == RadixSortOrder::Ascending) {
    return (x >> RadixTraits::kBitsPerPass * pass) & RadixTraits::kRadixMask;
  } else if constexpr (RadixTraits::kSortOrder == RadixSortOrder::Descending) {
    return ((~x) >> RadixTraits::kBitsPerPass * pass) & RadixTraits::kRadixMask;
  } else {
    assume_unreachable();
  }
}

/// Returns the most significant set bit index of an arithmetic value.
/// Used to skip radix passes whose higher-order bits are all zero.
/// For floating-point, evaluates the IEEE 754 bit representation.
template <typename T>
#if !defined(DEBUG) && !defined(_DEBUG)
FOLLY_ALWAYS_INLINE
#endif
    constexpr auto highestOneBitIndex(T value) noexcept -> size_t {
  static_assert(std::is_arithmetic_v<T>, "T must be arithmetic");
  using uint_t = uint_bits_t<sizeof(T) * CHAR_BIT>;

  uint_t bits;
  if constexpr (std::is_integral_v<T>) {
    // Preserve two's complement bit pattern
    bits = static_cast<uint_t>(value);
  } else if constexpr (std::is_floating_point_v<T>) {
    bits = std::bit_cast<uint_t>(value);
  } else {
    assume_unreachable();
  }

  return std::bit_width(static_cast<uint_t>(bits | uint_t(1))) - 1;
}

///< Radix parallel computing utils

/// Thread-chunk descriptors for distributing work across OpenMP threads.
struct ThreadChunkInfo {
  uint32_t threads; ///< Total OpenMP threads.
  uint32_t remain; ///< Threads that process one extra element.
  size_t step; ///< floor(n / threads) — base elements per thread.
};

/// Per-thread chunk statistics used for sortedness detection.
template <typename T>
struct ThreadChunkStats {
  bool isSorted; ///< Whether the chunk is locally sorted (ascending).
  T minVal; ///< Minimum value in the chunk.
  T maxVal; ///< Maximum value in the chunk.
};

/// Returns the half-open [beg, end) range assigned to thread `tid`.
#if !defined(DEBUG) && !defined(_DEBUG)
FOLLY_ALWAYS_INLINE
#endif
constexpr auto computeThreadChunkRange(
    size_t tid, size_t step, size_t remain) noexcept
    -> std::pair<size_t, size_t> {
  size_t begIndex, endIndex;
  if (tid < remain) {
    begIndex = tid * (step + 1);
    endIndex = begIndex + (step + 1);
  } else {
    begIndex = remain * (step + 1) + (tid - remain) * step;
    endIndex = begIndex + step;
  }
  return std::pair{begIndex, endIndex};
}

///< Radix sort fallback sorting mode

/// Maximum element count for which insertion sort is used instead of radix
/// sort.
inline constexpr size_t kRadixSortThreshold = 64U;

/// Partitions boolean values into false/true groups in a single pass.
template <std::random_access_iterator RandIter, bool Descending>
constexpr void sortBool(RandIter first, RandIter last) {
  size_t cnt = 0;
  for (auto curr = first; curr < last; ++curr) {
    const auto v = *curr;
    *curr = Descending ? false : true;
    *(first + cnt) = v;
    cnt += static_cast<size_t>(Descending ? v : !v);
  }
}

/// Insertion sort over a bidirectional iterator range.
template <
    std::bidirectional_iterator BiIter,
    std::indirect_strict_weak_order<BiIter> Compare = std::ranges::less>
constexpr void insertionSort(BiIter first, BiIter last, Compare comp = {}) {
  if (first == last) [[unlikely]] {
    return;
  }
  for (auto current = std::next(first); current != last; ++current) {
    const auto value = std::move(*current);
    auto hole = current;
    auto prev = hole;
    while (prev != first && comp(value, *std::prev(prev))) {
      --prev;
      *hole = std::move(*prev);
      hole = prev;
    }
    *hole = std::move(value);
  }
}

/// Fallback LSD radix sort used by MSD for small sub-problems.
template <
    typename RadixTraits,
    typename Projection,
    bool IsSignBitHandling = false>
void integerRadixSort(
    typename RadixTraits::value_t* FOLLY_RESTRICT data,
    const size_t n,
    typename RadixTraits::value_t* FOLLY_RESTRICT buffer,
    const size_t passes,
    typename RadixTraits::hist_t* FOLLY_RESTRICT hist1d,
    Projection proj,
    size_t depth = 0) {
  if (n == 0) {
    return;
  }

  for (size_t pass = 0; pass < passes; ++pass) {
    using hist_t = typename RadixTraits::hist_t;
    __folly_memset(hist1d, 0, RadixTraits::kNumBuckets * sizeof(hist_t));

    for (size_t i = 0; i < n; i++) {
      auto x = proj.template operator()<IsSignBitHandling>(data[i]);
      ++hist1d[nthRadix<RadixTraits>(x, pass)];
    }

    for (size_t i = 1; i < RadixTraits::kNumBuckets; i++) {
      hist1d[i] += hist1d[i - 1];
    }

    for (size_t i = n; i-- > 0;) {
      auto x = proj.template operator()<IsSignBitHandling>(data[i]);
      buffer[--hist1d[nthRadix<RadixTraits>(x, pass)]] = std::move(data[i]);
    }
    std::swap(data, buffer);
  }

  if (depth + passes & 1) {
    std::move(data, data + n, buffer);
  }
}

///< Implementations for LSD/MSD and sequential/parallel execution.

// Dispatcher selected by options, iterator, allocator, and projection.
template <
    typename RadixTraits,
    std::random_access_iterator RandIter,
    standard_allocator Allocator,
    unsigned_integral_projection<std::iter_value_t<RandIter>> Projection>
struct RadixSortImplDispatcher;

///< LSD + Seq — sequential, least-significant-digit-first
template <
    typename RadixTraits,
    std::random_access_iterator RandIter,
    standard_allocator Allocator,
    unsigned_integral_projection<std::iter_value_t<RandIter>> Projection>
  requires(
      RadixTraits::kSortStrategy == RadixSortStrategy::Lsd &&
      RadixTraits::kExecutionPolicy == RadixExecutionPolicy::Seq)
struct RadixSortImplDispatcher<RadixTraits, RandIter, Allocator, Projection>
    : RadixTraits {
  // Re-export frequently used types and constants from the base trait.
  using Traits = RadixTraits;
  using Base = RadixTraits;
  using Base::kHistogramStackThresholdBytes;
  using Base::kLastPass;
  using Base::kNumBuckets;
  using Base::kNumPasses;
  using Base::kPassIndices;
  using Base::kSortOrder;
  using typename Base::hist_t;
  using typename Base::key_t;
  using typename Base::value_t;
  using hist2d_t = hist_t (*)[kNumBuckets];

  /// Single-pass histogram builder and sortedness checker.
  /// Returns true if the input is already in the desired order.
  static constexpr auto buildHistogram(
      const value_t* data, const size_t n, hist2d_t hist2d, Projection proj)
      -> bool {
    // Choose the comparison that indicates "not out of order":
    // for ascending, we expect prev <= key; for descending, prev >= key.
    auto comp = std::conditional_t<
        kSortOrder == RadixSortOrder::Ascending,
        std::greater_equal<>,
        std::less_equal<>>{};
    auto prev = kSortOrder == RadixSortOrder::Ascending
        ? std::numeric_limits<key_t>::lowest()
        : std::numeric_limits<key_t>::max();

    const auto& keyMap = proj.getKeyMap();
    bool isSorted = true;

    for (size_t i = 0; i < n; ++i) {
      const auto key = keyMap(data[i]);
      isSorted = isSorted && comp(key, prev);
      prev = key;

      // Build histograms for all passes in a single scan (fold over
      // kPassIndices at compile time). Scanning once instead of kNumPasses
      // times keeps the loop in L1 cache.
      [&]<size_t... Is>(std::index_sequence<Is...>) {
        ((++hist2d[Is][nthRadix<Traits>(Projection::transform(key), Is)]), ...);
      }(kPassIndices);
    }
    return isSorted;
  }

  /// Converts per-pass bucket counts to prefix-sum offsets.
  /// Returns non-empty bucket count per pass (used to skip trivial passes).
  template <size_t... Is>
  static auto prefixSum(hist2d_t hist2d, std::index_sequence<Is...>)
      -> std::array<size_t, sizeof...(Is)> {
    std::array<size_t, sizeof...(Is)> nonEmptyCounts;
    auto process = [&](size_t index) -> void {
      nonEmptyCounts[index] = (hist2d[index][0] != 0);
      for (size_t i = 1; i < kNumBuckets; ++i) {
        nonEmptyCounts[index] += (hist2d[index][i] != 0);
        hist2d[index][i] += hist2d[index][i - 1];
      }
    };
    (process(Is), ...);
    return nonEmptyCounts;
  }

  /// Scatters all passes. Sign-bit correction deferred to the last pass.
  /// Returns pointer to the sorted data (may be data or buffer depending on
  /// pass-count parity).
  static constexpr auto doScatter(
      value_t* FOLLY_RESTRICT data,
      const size_t n,
      value_t* FOLLY_RESTRICT buffer,
      const std::array<size_t, kNumPasses>& nonEmptyCounts,
      hist2d_t hist2d,
      Projection proj) -> value_t* {
    // All passes except the last one: sign-bit correction is not needed.
    for (size_t pass = 0; pass < kLastPass; ++pass) {
      if (nonEmptyCounts[pass] <= 1) {
        continue; // All elements have the same digit; no reordering needed.
      }
      for (size_t i = n; i-- > 0;) {
        const auto x = proj.template operator()<false>(data[i]);
        buffer[--hist2d[pass][nthRadix<Traits>(x, pass)]] = std::move(data[i]);
      }
      std::swap(data, buffer);
    }

    // Last pass: apply full sign-bit handling if the key type is signed.
    if (nonEmptyCounts[kLastPass] > 1) {
      for (size_t i = n; i-- > 0;) {
        const auto x = proj(data[i]); // uses the default `IsSignBitHandling`
        buffer[--hist2d[kLastPass][nthRadix<Traits>(x, kLastPass)]] =
            std::move(data[i]);
      }
      std::swap(data, buffer);
    }
    return data;
  }

  /// Entry point: builds histogram, computes prefix sums, scatters all passes.
  /// Always leaves the sorted result in the original array.
  static constexpr void sort(
      value_t* FOLLY_RESTRICT data,
      const size_t n,
      const Allocator& allocator,
      Projection proj) {
    auto* const original = data;

    // Allocate temporary element buffer.
    AllocatorHolder<Allocator> holder(n, allocator);
    auto* FOLLY_RESTRICT buffer = holder.data();

    const size_t histSize = kNumPasses * kNumBuckets;
    const size_t histBytes = histSize * sizeof(hist_t);

    // Allocate histogram storage, preferring stack if size is small.
    FOLLY_HYBRID_VECTOR_ALIGNED_THRESHOLD(
        hist_t, histStorage, histSize, 64, kHistogramStackThresholdBytes);
    __folly_memset(histStorage.data(), 0, histBytes);
    auto hist2d = reinterpret_cast<hist2d_t>(histStorage.data());

    // Step 1: build histograms and check sortedness.
    if (bool sorted = buildHistogram(data, n, hist2d, proj)) {
      return; // Already sorted; nothing to do.
    }

    // Step 2: convert counts to offsets and get non-empty bucket counts.
    std::array nonEmptyCounts = prefixSum(hist2d, kPassIndices);

    // Step 3: scatter all passes.
    // The result may end up in the temporary buffer if the number of passes
    // executed is odd.
    auto* result = doScatter(data, n, buffer, nonEmptyCounts, hist2d, proj);

    // Step 4: if the sorted data is not in the original array, move it back.
    if (result != original) {
      std::move(result, result + n, original);
    }
  }
};

///< MSD + Seq — sequential, most-significant-digit-first
template <
    typename RadixTraits,
    std::random_access_iterator RandIter,
    standard_allocator Allocator,
    unsigned_integral_projection<std::iter_value_t<RandIter>> Projection>
  requires(
      RadixTraits::kSortStrategy == RadixSortStrategy::Msd &&
      RadixTraits::kExecutionPolicy == RadixExecutionPolicy::Seq &&
      RadixTraits::kNumPasses > 1)
struct RadixSortImplDispatcher<RadixTraits, RandIter, Allocator, Projection>
    : RadixTraits {
  // Re-export frequently used types and constants from the base trait.
  using Traits = RadixTraits;
  using Base = RadixTraits;
  using Base::kBitsPerPass;
  using Base::kFirstPass;
  using Base::kHistogramStackThresholdBytes;
  using Base::kIsSignHandlingOnFirstPass;
  using Base::kMsdFallbackToLsdThreshold;
  using Base::kNumBuckets;
  using Base::kNumPasses;
  using Base::kSortOrder;
  using typename Base::hist_t;
  using typename Base::key_t;
  using typename Base::uint_t;
  using typename Base::value_t;
  using hist2d_t = hist_t (*)[kNumBuckets];

  /// First-pass scan: builds MSB histogram, checks sortedness, computes msb.
  /// Returns {msb index, is sorted}.
  static constexpr auto firstPassScan(
      const value_t* data, const size_t n, hist2d_t hist2d, Projection proj)
      -> std::pair<size_t, bool> {
    auto prev = kSortOrder == RadixSortOrder::Ascending
        ? std::numeric_limits<key_t>::lowest()
        : std::numeric_limits<key_t>::max();
    auto comp = std::conditional_t<
        kSortOrder == RadixSortOrder::Ascending,
        std::greater_equal<>,
        std::less_equal<>>{};

    const auto& keyMap = proj.getKeyMap();
    uint_t combined = 0;
    bool sorted = true;
    for (size_t i = 0; i < n; ++i) {
      const auto key = keyMap(data[i]);
      sorted = sorted && comp(key, prev);
      prev = key;
      // OR all keys' bit patterns to find the overall msb.
      // This determines the actual pass count in the sort step,
      // skipping passes for leading-zero bytes at no extra scan cost.
      combined |= std::bit_cast<uint_t>(key);
      const auto x = Projection::transform(key);
      ++hist2d[kFirstPass][nthRadix<Traits>(x, kFirstPass)];
    }

    return {highestOneBitIndex(combined), sorted};
  }

  /// Builds the histogram for a given non-first pass.
  static constexpr void buildHistogram(
      const value_t* data,
      const size_t n,
      hist2d_t hist2d,
      const size_t pass,
      Projection proj) {
    for (size_t i = 0; i < n; ++i) {
      auto x = proj.template operator()<false>(data[i]);
      ++hist2d[pass][nthRadix<Traits>(x, pass)];
    }
  }

  /// Converts bucket counts to prefix-sum offsets for a pass.
  static constexpr auto prefixSum(hist2d_t hist2d, const size_t pass)
      -> size_t {
    size_t nonEmptyCount = (hist2d[pass][0] != 0);
    for (size_t i = 1; i < kNumBuckets; ++i) {
      nonEmptyCount += (hist2d[pass][i] != 0);
      hist2d[pass][i] += hist2d[pass][i - 1];
    }
    return nonEmptyCount;
  }

  /// Scatters elements into the buffer for a given pass.
  template <bool IsSignBitHandling = kIsSignHandlingOnFirstPass>
  static constexpr void doScatter(
      value_t* FOLLY_RESTRICT data,
      const size_t n,
      value_t* FOLLY_RESTRICT buffer,
      const size_t pass,
      hist2d_t hist2d,
      Projection proj) {
    for (size_t i = n; i-- > 0;) {
      auto x = proj.template operator()<IsSignBitHandling>(data[i]);
      buffer[--hist2d[pass][nthRadix<Traits>(x, pass)]] = std::move(data[i]);
    }
  }

  /// Iterates over buckets after scatter; recurses into multi-element buckets.
  static constexpr void recurseBuckets(
      value_t* FOLLY_RESTRICT data,
      const size_t n,
      value_t* FOLLY_RESTRICT buffer,
      const size_t pass,
      hist2d_t hist2d,
      Projection proj,
      size_t depth = 0) {
    for (size_t i = 0; i < kNumBuckets; ++i) {
      size_t chunkBeg = hist2d[pass][i];
      size_t chunkEnd = (i + 1 < kNumBuckets) ? hist2d[pass][i + 1] : n;

      if (size_t chunkSize = chunkEnd - chunkBeg; chunkSize > 1) {
        // Recurse to the next lower digit, increasing depth.
        doMsdRecursion(
            buffer + chunkBeg,
            chunkSize,
            data + chunkBeg,
            pass - 1,
            hist2d,
            proj,
            depth + 1);
      } else if (chunkSize == 1 && (depth & 1) == 0) {
        // For singleton buckets at even depth, move the element back to the
        // original array because the buffer roles alternate each recursion.
        data[chunkBeg] = std::move(buffer[chunkBeg]);
      }
    }
  }

  /// Recursively sorts sub-problem for a given pass.
  /// Falls back to LSD if sub-problem is small or no lower digits remain.
  static constexpr void doMsdRecursion(
      value_t* FOLLY_RESTRICT data,
      const size_t n,
      value_t* FOLLY_RESTRICT buffer,
      const size_t pass,
      hist2d_t hist2d,
      Projection proj,
      size_t depth = 0) {
    auto* hist1d = hist2d[pass];
    // Fallback to LSD when the sub-problem is small or no lower digits remain.
    if (n <= kMsdFallbackToLsdThreshold || pass < 1) {
      // `pass + 1` because LSD expects the number of passes, not the index.
      integerRadixSort<Traits>(data, n, buffer, pass + 1, hist1d, proj, depth);
      return;
    }

    // Step 1 : Clear the histogram for the current pass.
    __folly_memset(hist1d, 0, kNumBuckets * sizeof(hist_t));

    // Step 2 : build histogram for the current pass.
    buildHistogram(data, n, hist2d, pass, proj);

    // Step 3 :
    if (size_t nonEmptyCount = prefixSum(hist2d, pass); nonEmptyCount <= 1) {
      // If all elements share the same digit, skip scatter and go to next pass.
      return doMsdRecursion(data, n, buffer, pass - 1, hist2d, proj, depth);
    }

    // Step 4 : scatter using the current pass (no sign-bit handling for
    // non-first passes).
    doScatter<false>(data, n, buffer, pass, hist2d, proj);
    // Step 5 : recurse into each bucket.
    recurseBuckets(data, n, buffer, pass, hist2d, proj, depth);
  }

  /// Entry point: first-pass scan, then recursive MSD partitioning.
  /// Always leaves the sorted result in the original array.
  static constexpr void sort(
      value_t* FOLLY_RESTRICT data,
      const size_t n,
      const Allocator& allocator,
      Projection proj) {
    AllocatorHolder<Allocator> holder(n, allocator);
    auto* FOLLY_RESTRICT buffer = holder.data();

    const size_t histSize = kNumPasses * kNumBuckets;
    const size_t histBytes = histSize * sizeof(hist_t);

    // Pre-allocate histogram memory for all passes at once,
    // prevent memory allocation overhead in every recursion.
    FOLLY_HYBRID_VECTOR_ALIGNED_THRESHOLD(
        hist_t, histStorage, histSize, 64, kHistogramStackThresholdBytes);
    auto hist2d = reinterpret_cast<hist2d_t>(histStorage.data());

    // Only the first pass histogram needs to be cleared; other passes will be
    // cleared individually in `doMsdRecursion`.
    __folly_memset(hist2d[kFirstPass], 0, kNumBuckets * sizeof(hist_t));

    // First pass scan: builds MSB histogram, checks sortedness, computes msb.
    const auto [msb, sorted] = firstPassScan(data, n, hist2d, proj);
    if (sorted) {
      return;
    }

    // Compute the actual number of passes needed based on the highest set bit.
    // Example: for 32-bit with 8-bit chunks, msb=24 -> passes=(24+8)/8=4.
    const auto passes = (msb + kBitsPerPass) / kBitsPerPass;

    // If the full number of passes is needed, the first pass histogram is
    // ready.
    if (passes == kNumPasses && prefixSum(hist2d, kFirstPass) > 1) {
      // Scatter using the first pass (with sign-bit handling if needed).
      doScatter(data, n, buffer, kFirstPass, hist2d, proj);
      recurseBuckets(data, n, buffer, kFirstPass, hist2d, proj);
    } else {
      // Start recursion from the highest pass that actually contains data.
      doMsdRecursion(data, n, buffer, passes - 1, hist2d, proj);
    }
  }
};

#ifdef _OPENMP
/// Each thread builds a private histogram; prefixSum merges them.
template <typename RadixTraits, typename Allocator, typename Projection>
struct RadixSortParHelpers : RadixTraits {
  using Traits = RadixTraits;
  using Base = RadixTraits;
  using Base::kFirstPass;
  using Base::kIsSignHandlingOnFirstPass;
  using Base::kNaNsAssumption;
  using Base::kNumBuckets;
  using Base::kSortOrder;
  using typename Base::hist_t;
  using typename Base::key_t;
  using typename Base::uint_t;
  using typename Base::value_t;
  using hist2d_t = hist_t (*)[kNumBuckets];

  /// Like MSD+Seq::firstPassScan but with per-thread histograms.
  static auto firstPassScan(
      const value_t* data,
      const ThreadChunkInfo info,
      hist2d_t hist2d,
      Projection proj) -> std::pair<size_t, bool> {
    auto comp = std::conditional_t<
        kSortOrder == RadixSortOrder::Ascending,
        std::greater_equal<>,
        std::less_equal<>>{};

    const auto& keyMap = proj.getKeyMap();
    const uint32_t threads = info.threads;
    const uint32_t remain = info.remain;
    const size_t step = info.step;
    using ThreadChunkStatsWrapped = ThreadChunkStats<key_t>;
    FOLLY_HYBRID_VECTOR(ThreadChunkStatsWrapped, statsArray, threads);
    statsArray.resize(threads);

#pragma omp parallel num_threads((int)threads)
    {
      const int tid = omp_get_thread_num();
      const auto [beg, end] = computeThreadChunkRange(tid, step, remain);

      bool sorted = true;
      auto minVal = std::numeric_limits<key_t>::max();
      auto maxVal = std::numeric_limits<key_t>::lowest();
      auto prev = kSortOrder == RadixSortOrder::Ascending
          ? std::numeric_limits<key_t>::lowest()
          : std::numeric_limits<key_t>::max();

      for (size_t i = beg; i < end; ++i) {
        const auto key = keyMap(data[i]);
        sorted = sorted && comp(key, prev);
        prev = key;

        auto x{Projection::template transform<kIsSignHandlingOnFirstPass>(key)};
        ++hist2d[tid][nthRadix<Traits>(x, kFirstPass)];

        // Skip NaN when computing min/max if NaNs are allowed (they break
        // ordering).
        if constexpr (
            std::is_floating_point_v<key_t> &&
            kNaNsAssumption == RadixNaNsAssumption::Existence) {
          if (std::isnan(key))
            continue;
        }
        if (key < minVal) {
          minVal = key;
        }
        if (key > maxVal) {
          maxVal = key;
        }
      }
      statsArray[tid] = ThreadChunkStats{sorted, minVal, maxVal};
    }

    // Merge per-thread statistics.
    auto& [sorted1, minVal1, maxVal1] = statsArray[0];
    uint_t combined = 0;
    combined |= std::bit_cast<uint_t>(minVal1) | std::bit_cast<uint_t>(maxVal1);
    for (size_t i = 1; i < threads; ++i) {
      const auto [sorted, minVal, maxVal] = statsArray[i];
      combined |= std::bit_cast<uint_t>(minVal) | std::bit_cast<uint_t>(maxVal);
      sorted1 = sorted1 && sorted;
      if constexpr (kSortOrder == RadixSortOrder::Ascending) {
        sorted1 = sorted1 && (minVal >= statsArray[i - 1].maxVal);
      } else {
        sorted1 = sorted1 && (maxVal <= statsArray[i - 1].minVal);
      }
    }
    return {highestOneBitIndex(combined), sorted1};
  }

  /// Per-thread histogram builder (like the Seq version but parallelized).
  template <bool IsSignBitHandling = false>
  static void buildHistogram(
      value_t* data,
      const ThreadChunkInfo info,
      hist2d_t hist2d,
      const size_t pass,
      Projection proj) {
    const uint32_t threads = info.threads;
    const uint32_t remain = info.remain;
    const size_t step = info.step;
#pragma omp parallel num_threads((int)threads)
    {
      const int tid = omp_get_thread_num();
      const auto [beg, end] = computeThreadChunkRange(tid, step, remain);

      for (size_t i = beg; i < end; ++i) {
        const auto x = proj.template operator()<IsSignBitHandling>(data[i]);
        ++hist2d[tid][nthRadix<Traits>(x, pass)];
      }
    }
  }

  /// Merges per-thread histogram into global prefix-sum offsets (N-shaped
  /// scan). After this, hist2d[tid][bucket] is the global start offset for that
  /// thread and bucket. Returns the number of non-empty buckets.
  static auto prefixSum(hist2d_t hist2d, size_t threads) -> size_t {
    for (size_t tid = 1; tid < threads; ++tid) {
      hist2d[tid][0] += hist2d[tid - 1][0];
    }

    size_t nonEmpty = (hist2d[threads - 1][0] != 0);
    for (size_t i = 1; i < kNumBuckets; ++i) {
      const auto last = hist2d[threads - 1][i - 1];
      hist2d[0][i] += last;

      for (size_t tid = 1; tid < threads; ++tid) {
        hist2d[tid][i] += hist2d[tid - 1][i];
      }

      nonEmpty += (hist2d[threads - 1][i] - last != 0);
    }
    return nonEmpty;
  }

  /// Per-thread scatter (like the Seq version but parallelized).
  template <bool IsSignBitHandling = false>
  static void doScatter(
      value_t* FOLLY_RESTRICT data,
      const ThreadChunkInfo info,
      value_t* FOLLY_RESTRICT buffer,
      const size_t pass,
      hist2d_t hist2d,
      Projection proj) {
    const uint32_t threads = info.threads;
    const uint32_t remain = info.remain;
    const size_t step = info.step;
#pragma omp parallel num_threads((int)threads)
    {
      int tid = omp_get_thread_num();
      auto [beg, end] = computeThreadChunkRange(tid, step, remain);
      for (size_t i = end; i-- > beg;) {
        const auto x = proj.template operator()<IsSignBitHandling>(data[i]);
        buffer[--hist2d[tid][nthRadix<Traits>(x, pass)]] = std::move(data[i]);
      }
    }
  }
};

///< LSD + Par — parallel version of LSD+Seq (requires _OPENMP)
/// Algorithm is the same; key difference: each thread builds its own histogram
/// and scatters elements independently. See LSD + Seq for algorithm details.
template <
    typename RadixTraits,
    std::random_access_iterator RandIter,
    standard_allocator Allocator,
    unsigned_integral_projection<std::iter_value_t<RandIter>> Projection>
  requires(
      RadixTraits::kSortStrategy == RadixSortStrategy::Lsd &&
      RadixTraits::kExecutionPolicy == RadixExecutionPolicy::Par)
struct RadixSortImplDispatcher<RadixTraits, RandIter, Allocator, Projection>
    : public RadixSortParHelpers<RadixTraits, Allocator, Projection> {
  using Traits = RadixTraits;
  using Base = RadixSortParHelpers<RadixTraits, Allocator, Projection>;
  using Base::buildHistogram;
  using Base::doScatter;
  using Base::firstPassScan;
  using Base::kBitsPerPass;
  using Base::kFirstPass;
  using Base::kHistogramStackThresholdBytes;
  using Base::kIsSignHandlingOnFirstPass;
  using Base::kNumBuckets;
  using Base::kNumPasses;
  using Base::prefixSum;
  using typename Base::hist_t;
  using typename Base::value_t;
  using hist2d_t = hist_t (*)[kNumBuckets];

  /// Entry point (parallel LSD). Same algorithm as LSD+Seq; uses per-thread
  /// histograms and prefixSum merging for parallel passes.
  static void sort(
      value_t* FOLLY_RESTRICT data,
      const size_t n,
      const Allocator& allocator,
      Projection proj) {
    auto* const original = data;
    AllocatorHolder<Allocator> holder(n, allocator);
    auto* FOLLY_RESTRICT buffer = holder.data();

    const size_t threads = ::omp_get_max_threads();
    // Per-thread histogram: hist2d[tid][bucket] avoids atomic contention.
    const size_t histSize = threads * kNumBuckets;
    const size_t histBytes = histSize * sizeof(hist_t);
    const ThreadChunkInfo chunkInfo(threads, n % threads, n / threads);

    FOLLY_HYBRID_VECTOR_ALIGNED_THRESHOLD(
        hist_t, histStorage, histSize, 64, kHistogramStackThresholdBytes);
    __folly_memset(histStorage.data(), 0, histBytes);
    auto hist2d = reinterpret_cast<hist2d_t>(histStorage.data());

    // Unlike LSD+Seq's single-scan multi-pass histogram, the 2D per-thread
    // layout requires iterative histogram-scatter per pass.
    auto [msb, sorted] = firstPassScan(data, chunkInfo, hist2d, proj);
    if (sorted) {
      return;
    }

    if (size_t numEmptyCount = prefixSum(hist2d, threads); numEmptyCount > 1) {
      Base::template doScatter<kIsSignHandlingOnFirstPass>(
          data, chunkInfo, buffer, kFirstPass, hist2d, proj);
      std::swap(data, buffer);
    }

    const auto passes = (msb + kBitsPerPass) / kBitsPerPass;

    // Process remaining passes (from 1 to passes-1).
    for (size_t pass = 1; pass < passes; ++pass) {
      __folly_memset(histStorage.data(), 0, histBytes);

      if (pass != passes - 1 || passes != kNumPasses) {
        buildHistogram(data, chunkInfo, hist2d, pass, proj);
        if (prefixSum(hist2d, threads) > 1) {
          doScatter(data, chunkInfo, buffer, pass, hist2d, proj);
          std::swap(data, buffer);
        }
      } else {
        Base::template buildHistogram<true>(
            data, chunkInfo, hist2d, pass, proj);
        if (prefixSum(hist2d, threads) > 1) {
          Base::template doScatter<true>(
              data, chunkInfo, buffer, pass, hist2d, proj);
          std::swap(data, buffer);
        }
      }
    }

    // If the sorted data is in the temporary buffer, move it back to the
    // original array (this happens when the number of passes executed is odd).
    if (data != original) {
      std::move(data, data + n, original);
    }
  }
};

///< MSD + Par — parallel version of MSD+Seq (requires _OPENMP)
/// Algorithm is the same; key difference: each sub-problem at the top level
/// is processed by separate OpenMP threads. See MSD + Seq for algorithm
/// details.
template <
    typename RadixTraits,
    std::random_access_iterator RandIter,
    standard_allocator Allocator,
    unsigned_integral_projection<std::iter_value_t<RandIter>> Projection>
  requires(
      RadixTraits::kSortStrategy == RadixSortStrategy::Msd &&
      RadixTraits::kExecutionPolicy == RadixExecutionPolicy::Par &&
      RadixTraits::kNumPasses > 1)
struct RadixSortImplDispatcher<RadixTraits, RandIter, Allocator, Projection>
    : public RadixSortParHelpers<RadixTraits, Allocator, Projection> {
  using Traits = RadixTraits;
  using Base = RadixSortParHelpers<RadixTraits, Allocator, Projection>;
  using Base::buildHistogram;
  using Base::doScatter;
  using Base::firstPassScan;
  using Base::kBitsPerPass;
  using Base::kFirstPass;
  using Base::kHistogramStackThresholdBytes;
  using Base::kMsdFallbackToLsdThreshold;
  using Base::kNumBuckets;
  using Base::kNumPasses;
  using Base::prefixSum;
  using typename Base::hist_t;
  using typename Base::value_t;
  using hist2d_t = hist_t (*)[kNumBuckets];

  /// Iterates buckets after scatter, parallelized at the bucket level.
  static void recurseBuckets(
      value_t* FOLLY_RESTRICT data,
      const size_t n,
      value_t* FOLLY_RESTRICT buffer,
      const size_t pass,
      hist2d_t hist2d,
      const ThreadChunkInfo info,
      Projection proj,
      size_t depth = 0) {
    // static schedule: bucket sizes vary but static avoids steal overhead
#pragma omp parallel for schedule(static) \
    num_threads((int)info.threads) if (info.threads > 1 && !omp_in_parallel())
    for (size_t i = 0; i < kNumBuckets; ++i) {
      size_t chunkBeg = hist2d[0][i];
      size_t chunkEnd = (i + 1 < kNumBuckets) ? hist2d[0][i + 1] : n;

      if (size_t chunkSize = chunkEnd - chunkBeg; chunkSize > 1) {
        doMsdRecursion(
            buffer + chunkBeg,
            chunkSize,
            data + chunkBeg,
            pass - 1,
            info,
            proj,
            depth + 1);
      } else if (chunkSize == 1 && (depth & 1) == 0) {
        data[chunkBeg] = std::move(buffer[chunkBeg]);
      }
    }
  }

  /// Same logic as MSD+Seq::doMsdRecursion, but disables nested OpenMP
  /// parallelism once inside a parallel region.
  static void doMsdRecursion(
      value_t* FOLLY_RESTRICT data,
      const size_t n,
      value_t* FOLLY_RESTRICT buffer,
      const size_t pass,
      const ThreadChunkInfo info,
      Projection proj,
      size_t depth = 0) {
    // Disable nested parallelism once inside a parallel region. Recursing
    // with all threads would oversubscribe and hurt cache locality.
    const uint32_t threads = omp_in_parallel() ? 1U : info.threads;
    const size_t histSize = size_t{threads} * kNumBuckets;
    const size_t histBytes = histSize * sizeof(hist_t);

    FOLLY_HYBRID_VECTOR_ALIGNED_THRESHOLD(
        hist_t, histStorage, histSize, 64, kHistogramStackThresholdBytes);
    auto hist2d = reinterpret_cast<hist2d_t>(histStorage.data());
    auto* hist1d = hist2d[0];

    if (n <= kMsdFallbackToLsdThreshold || pass < 1) {
      integerRadixSort<Traits>(data, n, buffer, pass + 1, hist1d, proj, depth);
      return;
    }

    __folly_memset(histStorage.data(), 0, histBytes);

    auto [step, remain] = ::lldiv(n, threads);
    const ThreadChunkInfo subInfo(threads, remain, step);
    buildHistogram(data, subInfo, hist2d, pass, proj);

    if (prefixSum(hist2d, threads) <= 1) {
      doMsdRecursion(data, n, buffer, pass - 1, info, proj, depth);
      return;
    }
    doScatter(data, subInfo, buffer, pass, hist2d, proj);
    recurseBuckets(data, n, buffer, pass, hist2d, subInfo, proj, depth);
  }

  /// Entry point (parallel MSD). Same as MSD+Seq with parallel first-pass
  /// scan; recursion falls back to sequential once inside a parallel region.
  static void sort(
      value_t* FOLLY_RESTRICT data,
      const size_t n,
      const Allocator& allocator,
      Projection proj) {
    AllocatorHolder<Allocator> holder(n, allocator);
    auto* FOLLY_RESTRICT buffer = holder.data();

    const size_t threads = ::omp_get_max_threads();
    const size_t histSize = threads * kNumBuckets;
    const size_t histBytes = histSize * sizeof(hist_t);
    const ThreadChunkInfo chunkInfo(threads, n % threads, n / threads);

    FOLLY_HYBRID_VECTOR_ALIGNED_THRESHOLD(
        hist_t, histStorage, histSize, 64, kHistogramStackThresholdBytes);
    __folly_memset(histStorage.data(), 0, histBytes);
    auto hist2d = reinterpret_cast<hist2d_t>(histStorage.data());

    auto [msb, sorted] = firstPassScan(data, chunkInfo, hist2d, proj);
    if (sorted) {
      return;
    }

    const auto passes = (msb + kBitsPerPass) / kBitsPerPass;
    if (passes == kNumPasses && prefixSum(hist2d, threads) > 1) {
      Base::template doScatter<true>(
          data, chunkInfo, buffer, kFirstPass, hist2d, proj);
      recurseBuckets(data, n, buffer, kFirstPass, hist2d, chunkInfo, proj);
    } else {
      doMsdRecursion(data, n, buffer, passes - 1, chunkInfo, proj);
    }
  }
};
#endif

// Selects and invokes the correct RadixSortImplDispatcher.
template <
    typename RadixTraits,
    std::random_access_iterator RandIter,
    detail::standard_allocator Allocator,
    unsigned_integral_projection<std::iter_value_t<RandIter>> Projection>
constexpr void radixSortImpl(
    RandIter first,
    RandIter last,
    const Allocator& allocator,
    Projection proj) {
  static_assert(
      !(RadixTraits::kNaNsAssumption == RadixNaNsAssumption::NonExistence &&
        RadixTraits::kNaNsPosHandling != RadixNaNsPosHandling::Unhandled),
      "If you assumed there are no NaNs, then you shouldn't change the default settings for NaN positions.");

  static_assert(
      !(RadixTraits::kBitsPerPass == 16U &&
        sizeof(typename RadixTraits::uint_t) == 1U),
      "Do not set RadixSortOptions.BitsPerPass = RadixBitsPerPass::Bits16 (two-byte) for one-byte data!");

  static_assert(
      !(RadixTraits::kSortStrategy == RadixSortStrategy::Msd &&
        RadixTraits::kNumPasses <= 1),
      "MSD sort with only one pass is equivalent to counting sort; "
      "use LSD strategy (RadixSortStrategy::Lsd) for single-pass sorting, "
      "or ensure at least 2 passes for MSD to avoid unsigned underflow in recurseBuckets (pass(0) - 1).");

#ifndef _OPENMP
  static_assert(
      RadixTraits::kExecutionPolicy != RadixExecutionPolicy::Par,
      "Parallel policy requires _OPENMP defined");
#endif

  assert(first <= last && "Invalid iterator range: first must be <= last");

  // 1. Special case: boolean elements use a dedicated O(n) partition routine.
  if constexpr (std::same_as<std::iter_value_t<RandIter>, bool>) {
    // Invert order if descending is requested.
    sortBool<RandIter, RadixTraits::kSortOrder != RadixSortOrder::Ascending>(
        first, last);
  } else {
    const size_t size = last - first;

    // 2. Small-array fallback: insertion sort avoids radix-sort overhead.
    if (size <= kRadixSortThreshold) [[unlikely]] {
      // Use projection as key extractor; compare according to SortOrder.
      insertionSort(first, last, [proj](const auto& a, const auto& b) {
        if constexpr (RadixTraits::kSortOrder == RadixSortOrder::Ascending) {
          return proj(a) < proj(b);
        } else {
          return proj(b) < proj(a);
        }
      });
      return;
    }

    // 3. Regular path: Dispatch to the selected strategy
    //    (LSD/Seq or LSD/Par or MSD/Seq or MSD/Par).
    RadixSortImplDispatcher<RadixTraits, RandIter, Allocator, Projection>::sort(
        &*first, size, allocator, proj);
  }
}

} // namespace detail
namespace detail {

inline constexpr RadixSortOptions kDeprecatedStableRadixSortOptions{};

inline constexpr RadixSortOptions kDeprecatedStableRadixSortDescendingOptions =
    [] {
      RadixSortOptions opts{};
      opts.SortOrder = RadixSortOrder::Descending;
      return opts;
    }();

} // namespace detail

///< Deprecated shims -- use radixSort() instead.

template <typename ContiguousIt>
[[deprecated("use folly::radixSort instead")]]
void stable_radix_sort(ContiguousIt first, ContiguousIt last) {
  radixSort(first, last);
}

template <typename ContiguousIt, typename KeyMap>
[[deprecated("use folly::radixSort with a key map instead")]]
void stable_radix_sort(ContiguousIt first, ContiguousIt last, KeyMap keyMap) {
  radixSort(first, last, std::move(keyMap));
}

template <typename ContiguousIt>
[[deprecated("use folly::radixSort with RadixSortOrder::Descending instead")]]
void stable_radix_sort_descending(ContiguousIt first, ContiguousIt last) {
  radixSort<detail::kDeprecatedStableRadixSortDescendingOptions>(first, last);
}

template <typename ContiguousIt, typename KeyMap>
[[deprecated("use folly::radixSort with RadixSortOrder::Descending instead")]]
void stable_radix_sort_descending(
    ContiguousIt first, ContiguousIt last, KeyMap keyMap) {
  radixSort<detail::kDeprecatedStableRadixSortDescendingOptions>(
      first, last, std::move(keyMap));
}

///< Public API

/**
 * @brief Radix sort a range `[first, last)` with full control over
 *                options, allocator, and key projection.
 *
 * @details
 * This is the most general overload. It accepts a `RadixSortOptions` template
 * argument, an allocator for temporary storage, and an arbitrary `KeyMap`
 * that projects each element to an arithmetic key (integral or floating‑point).
 *
 * The function automatically handles reverse iterators by swapping the
 * sort order and passing the underlying base iterators to the internal
 * implementation.
 *
 * @tparam RadixOptions  Compile‑time sorting options (see `RadixSortOptions`).
 * @tparam RandIter      Random‑access iterator type.
 * @tparam Allocator     Allocator type (must satisfy `standard_allocator`).
 * @tparam KeyMap        A callable `value_t -> arithmetic` (default: identity).
 * @param first          Inclusive start of the range.
 * @param last           Exclusive end of the range.
 * @param allocator      Allocator instance for temporary buffer.
 * @param keyMap         Key extraction functor.
 *
 * @example
 * @code
 *   struct Item { double score; int id; };
 *   std::vector<Item> items = ...;
 *   constexpr RadixSortOptions opts{
 *       .SortStrategy = RadixSortStrategy::Msd,
 *       .SortOrder = RadixSortOrder::Descending};
 *   folly::radixSort<opts>(items.begin(), items.end(),
 *                          std::allocator<Item>{},
 *                          [](const Item& x) { return x.score; });
 * @endcode
 */
template <
    RadixSortOptions RadixOptions,
    std::random_access_iterator RandIter,
    detail::standard_allocator Allocator,
    detail::arithmetic_key_map<std::iter_value_t<RandIter>> KeyMap =
        detail::radix_key_map_fn>
constexpr void radixSort(
    RandIter first,
    RandIter last,
    const Allocator& allocator,
    KeyMap keyMap = {}) {
  using value_t = std::iter_value_t<RandIter>;

  // Construct the projection functor that maps value -> unsigned key.
  auto projection =
      detail::radix_uint_projection.template
      operator()<RadixOptions.NaNsPosHandling>(
          std::in_place_type<value_t>, std::move(keyMap));

  // Reverse iterators invert semantic order, so adjust the options before
  // passing the underlying base iterators to the implementation.
  constexpr auto adjustedOptions = [] {
    RadixSortOptions opts = RadixOptions;
    opts.SortOrder =
        detail::RadixRealSortOrder<RandIter, RadixOptions.SortOrder>::value;
    return opts;
  }();

  using RadixTraits = detail::RadixSortTraits<adjustedOptions, value_t, KeyMap>;

  // If the iterator is a reverse_iterator, unwrap to base and swap first/last.
  if constexpr (is_reverse_iterator_v<RandIter>) {
    detail::radixSortImpl<RadixTraits>(
        last.base(), first.base(), allocator, std::move(projection));
  } else {
    detail::radixSortImpl<RadixTraits>(
        first, last, allocator, std::move(projection));
  }
}

/**
 * @brief Radix sort with explicit options but defaulted allocator.
 *
 * @details
 * Convenience overload that uses `std::allocator<value_t>` internally.
 * All other parameters are the same as the four‑argument overload.
 *
 * @tparam RadixOptions  Compile‑time sorting options.
 * @tparam RandIter      Random‑access iterator type.
 * @tparam KeyMap        A callable `value_t -> arithmetic` (default: identity).
 * @param first          Inclusive start of the range.
 * @param last           Exclusive end of the range.
 * @param keyMap         Key extraction functor.
 */
template <
    RadixSortOptions RadixOptions,
    std::random_access_iterator RandIter,
    detail::arithmetic_key_map<std::iter_value_t<RandIter>> KeyMap =
        detail::radix_key_map_fn>
constexpr void radixSort(RandIter first, RandIter last, KeyMap keyMap = {}) {
  using value_t = typename std::iter_value_t<RandIter>;
  std::allocator<value_t> allocator{};
  radixSort<RadixOptions>(first, last, allocator, std::move(keyMap));
}

/**
 * @brief Radix sort with default options and defaulted allocator.
 *
 * @details
 * The simplest entry point: sorts `[first, last)` in ascending order
 * using LSD radix sort with 8‑bit chunks and all other options at their
 * defaults. This overload is ideal for quick, generic use.
 *
 * @tparam RandIter  Random‑access iterator type.
 * @tparam KeyMap    A callable `value_t -> arithmetic` (default: identity).
 * @param first      Inclusive start of the range.
 * @param last       Exclusive end of the range.
 * @param keyMap     Key extraction functor.
 */
template <
    std::random_access_iterator RandIter,
    detail::arithmetic_key_map<std::iter_value_t<RandIter>> KeyMap =
        detail::radix_key_map_fn>
constexpr void radixSort(RandIter first, RandIter last, KeyMap keyMap = {}) {
  using value_t = typename std::iter_value_t<RandIter>;
  std::allocator<value_t> allocator{};
  constexpr RadixSortOptions radixOptions{};
  radixSort<radixOptions>(first, last, allocator, std::move(keyMap));
}

} // namespace folly
