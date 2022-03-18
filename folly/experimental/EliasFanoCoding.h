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

/**
 * @author Philip Pronin (philipp@fb.com)
 *
 * Based on the paper by Sebastiano Vigna,
 * "Quasi-succinct indices" (arxiv:1206.4300).
 */

#pragma once

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <type_traits>

#include <glog/logging.h>

#include <folly/Likely.h>
#include <folly/Portability.h>
#include <folly/Range.h>
#include <folly/experimental/CodingDetail.h>
#include <folly/experimental/Instructions.h>
#include <folly/experimental/Select64.h>
#include <folly/lang/Assume.h>
#include <folly/lang/Bits.h>

#if !FOLLY_X64
#error EliasFanoCoding.h requires x86_64
#endif

namespace folly {
namespace compression {

static_assert(kIsLittleEndian, "EliasFanoCoding.h requires little endianness");

constexpr size_t kCacheLineSize = 64;

template <class Pointer>
struct EliasFanoCompressedListBase {
  EliasFanoCompressedListBase() = default;

  template <class OtherPointer>
  EliasFanoCompressedListBase(
      const EliasFanoCompressedListBase<OtherPointer>& other)
      : size(other.size),
        numLowerBits(other.numLowerBits),
        upperSizeBytes(other.upperSizeBytes),
        data(other.data),
        skipPointers(reinterpret_cast<Pointer>(other.skipPointers)),
        forwardPointers(reinterpret_cast<Pointer>(other.forwardPointers)),
        lower(reinterpret_cast<Pointer>(other.lower)),
        upper(reinterpret_cast<Pointer>(other.upper)) {}

  template <class T = Pointer>
  auto free() -> decltype(::free(T(nullptr))) {
    return ::free(data.data());
  }

  size_t size = 0;
  uint8_t numLowerBits = 0;
  size_t upperSizeBytes = 0;

  // WARNING: EliasFanoCompressedList has no ownership of data. The 7 bytes
  // following the last byte should be readable if kUpperFirst = false, 8 bytes
  // otherwise.
  Range<Pointer> data;

  Pointer skipPointers = nullptr;
  Pointer forwardPointers = nullptr;
  Pointer lower = nullptr;
  Pointer upper = nullptr;
};

using EliasFanoCompressedList = EliasFanoCompressedListBase<const uint8_t*>;
using MutableEliasFanoCompressedList = EliasFanoCompressedListBase<uint8_t*>;

template <
    class Value,
    // SkipValue must be wide enough to be able to represent the list length.
    class SkipValue = uint64_t,
    size_t kSkipQuantum = 0, // 0 = disabled
    size_t kForwardQuantum = 0, // 0 = disabled
    bool kUpperFirst = false>
struct EliasFanoEncoder {
  static_assert(
      std::is_integral_v<Value> && std::is_unsigned_v<Value>,
      "Value should be unsigned integral");

  using CompressedList = EliasFanoCompressedList;
  using MutableCompressedList = MutableEliasFanoCompressedList;

  using ValueType = Value;
  using SkipValueType = SkipValue;
  struct Layout;

  static constexpr size_t skipQuantum = kSkipQuantum;
  static constexpr size_t forwardQuantum = kForwardQuantum;

  static uint8_t defaultNumLowerBits(size_t upperBound, size_t size) {
    if (UNLIKELY(size == 0 || upperBound < size)) {
      return 0;
    }
    // Result that should be returned is "floor(log(upperBound / size))".
    // In order to avoid expensive division, we rely on
    // "floor(a) - floor(b) - 1 <= floor(a - b) <= floor(a) - floor(b)".
    // Assuming "candidate = floor(log(upperBound)) - floor(log(upperBound))",
    // then result is either "candidate - 1" or "candidate".
    auto candidate = findLastSet(upperBound) - findLastSet(size);
    // NOTE: As size != 0, "candidate" is always < 64.
    return (size > (upperBound >> candidate)) ? candidate - 1 : candidate;
  }

  // Requires: input range (begin, end) is sorted (encoding
  // crashes if it's not).
  // WARNING: encode() mallocates EliasFanoCompressedList::data. As
  // EliasFanoCompressedList has no ownership of it, you need to call
  // free() explicitly.
  template <class RandomAccessIterator>
  static MutableCompressedList encode(
      RandomAccessIterator begin, RandomAccessIterator end) {
    if (begin == end) {
      return MutableCompressedList();
    }
    EliasFanoEncoder encoder(size_t(end - begin), *(end - 1));
    for (; begin != end; ++begin) {
      encoder.add(*begin);
    }
    return encoder.finish();
  }

  explicit EliasFanoEncoder(const MutableCompressedList& result)
      : lower_(result.lower),
        upper_(result.upper),
        skipPointers_(reinterpret_cast<SkipValueType*>(result.skipPointers)),
        forwardPointers_(
            reinterpret_cast<SkipValueType*>(result.forwardPointers)),
        result_(result) {
    std::fill(result.data.begin(), result.data.end(), '\0');
  }

  EliasFanoEncoder(size_t size, ValueType upperBound)
      : EliasFanoEncoder(
            Layout::fromUpperBoundAndSize(upperBound, size).allocList()) {}

  void add(ValueType value) {
    CHECK_GE(value, lastValue_);

    const auto numLowerBits = result_.numLowerBits;
    const ValueType upperBits = value >> numLowerBits;

    // Upper sequence consists of upperBits 0-bits and (size_ + 1) 1-bits.
    const size_t pos = upperBits + size_;
    upper_[pos / 8] |= 1U << (pos % 8);
    // Append numLowerBits bits to lower sequence.
    if (numLowerBits != 0) {
      const ValueType lowerBits = value & ((ValueType(1) << numLowerBits) - 1);
      writeBits56(lower_, size_ * numLowerBits, numLowerBits, lowerBits);
    }

    fillSkipPointersUpTo(upperBits);

    if constexpr (forwardQuantum != 0) {
      if ((size_ + 1) % forwardQuantum == 0) {
        DCHECK_LE(upperBits, std::numeric_limits<SkipValueType>::max());
        const auto k = size_ / forwardQuantum;
        // Store the number of preceding 0-bits.
        forwardPointers_[k] = upperBits;
      }
    }

    lastValue_ = value;
    ++size_;
  }

  const MutableCompressedList& finish() {
    CHECK_EQ(size_, result_.size);
    const ValueType upperBitsUniverse =
        (8 * result_.upperSizeBytes - result_.size);
    // Populate skip pointers up to the universe upper bound (inclusive).
    fillSkipPointersUpTo(upperBitsUniverse);
    return result_;
  }

 private:
  void fillSkipPointersUpTo(ValueType fillBoundary) {
    if constexpr (skipQuantum != 0) {
      DCHECK_LE(size_, std::numeric_limits<SkipValueType>::max());
      // The first skip pointer is omitted (it would always be 0), so the
      // calculation is shifted by 1.
      while ((skipPointersSize_ + 1) * skipQuantum <= fillBoundary) {
        // Store the number of preceding 1-bits.
        skipPointers_[skipPointersSize_++] = static_cast<SkipValueType>(size_);
      }
    }
  }
  // Writes value (with len up to 56 bits) to data starting at pos-th bit.
  static void writeBits56(
      unsigned char* data, size_t pos, uint8_t len, uint64_t value) {
    DCHECK_LE(uint32_t(len), 56);
    DCHECK_EQ(0, value & ~((uint64_t(1) << len) - 1));
    unsigned char* const ptr = data + (pos / 8);
    uint64_t ptrv = loadUnaligned<uint64_t>(ptr);
    ptrv |= value << (pos % 8);
    storeUnaligned<uint64_t>(ptr, ptrv);
  }

  unsigned char* lower_ = nullptr;
  unsigned char* upper_ = nullptr;
  SkipValueType* skipPointers_ = nullptr;
  SkipValueType* forwardPointers_ = nullptr;

  ValueType lastValue_ = 0;
  size_t size_ = 0;
  size_t skipPointersSize_ = 0;

  MutableCompressedList result_;
};

template <
    class Value,
    class SkipValue,
    size_t kSkipQuantum,
    size_t kForwardQuantum,
    bool kUpperFirst>
struct EliasFanoEncoder<
    Value,
    SkipValue,
    kSkipQuantum,
    kForwardQuantum,
    kUpperFirst>::Layout {
  static Layout fromUpperBoundAndSize(size_t upperBound, size_t size) {
    // numLowerBits can be at most 56 because of detail::writeBits56.
    const uint8_t numLowerBits =
        std::min(defaultNumLowerBits(upperBound, size), uint8_t(56));
    // *** Upper bits.
    // Upper bits are stored using unary delta encoding.
    // For example, (3 5 5 9) will be encoded as 1000011001000_2.
    const size_t upperSizeBits =
        (upperBound >> numLowerBits) + // Number of 0-bits to be stored.
        size; // 1-bits.
    const size_t upper = (upperSizeBits + 7) / 8;

    // *** Validity checks.
    // Shift by numLowerBits must be valid.
    CHECK_LT(static_cast<int>(numLowerBits), 8 * sizeof(Value));
    CHECK_LE(
        upperBound >> numLowerBits, std::numeric_limits<SkipValueType>::max());

    return fromInternalSizes(numLowerBits, upper, size);
  }

  static Layout fromInternalSizes(
      uint8_t numLowerBits, size_t upper, size_t size) {
    Layout layout;
    layout.size = size;
    layout.numLowerBits = numLowerBits;

    layout.lower = (numLowerBits * size + 7) / 8;
    layout.upper = upper;

    // *** Skip pointers.
    // Store (1-indexed) position of every skipQuantum-th
    // 0-bit in upper bits sequence.
    if constexpr (skipQuantum != 0) {
      // 8 * upper is used here instead of upperSizeBits, as that is
      // more serialization-friendly way (upperSizeBits doesn't need
      // to be known by this function, unlike upper).

      size_t numSkipPointers = (8 * upper - size) / skipQuantum;
      layout.skipPointers = numSkipPointers * sizeof(SkipValueType);
    }

    // *** Forward pointers.
    // Store (1-indexed) position of every forwardQuantum-th
    // 1-bit in upper bits sequence.
    if constexpr (forwardQuantum != 0) {
      size_t numForwardPointers = size / forwardQuantum;
      layout.forwardPointers = numForwardPointers * sizeof(SkipValueType);
    }

    return layout;
  }

  size_t bytes() const {
    return lower + upper + skipPointers + forwardPointers;
  }

  template <class Range>
  EliasFanoCompressedListBase<typename Range::iterator> openList(
      Range& buf) const {
    EliasFanoCompressedListBase<typename Range::iterator> result;
    result.size = size;
    result.numLowerBits = numLowerBits;
    result.upperSizeBytes = upper;
    result.data = buf.subpiece(0, bytes());

    auto advance = [&](size_t n) {
      auto begin = buf.data();
      buf.advance(n);
      return begin;
    };

    result.skipPointers = advance(skipPointers);
    result.forwardPointers = advance(forwardPointers);
    if constexpr (kUpperFirst) {
      result.upper = advance(upper);
      result.lower = advance(lower);
    } else {
      result.lower = advance(lower);
      result.upper = advance(upper);
    }

    return result;
  }

  MutableCompressedList allocList() const {
    uint8_t* buf = nullptr;
    // WARNING: Current read/write logic assumes that the 7 bytes
    // following the upper bytes and the 8 bytes following the lower bytes
    // sequences are readable (stored value doesn't matter and won't be
    // changed), so we allocate additional 8 bytes, but do not include them in
    // size of returned value.
    if (size > 0) {
      buf = static_cast<uint8_t*>(malloc(bytes() + 8));
    }
    MutableByteRange bufRange(buf, bytes());
    return openList(bufRange);
  }

  size_t size = 0;
  uint8_t numLowerBits = 0;

  // Sizes in bytes.
  size_t lower = 0;
  size_t upper = 0;
  size_t skipPointers = 0;
  size_t forwardPointers = 0;
};

namespace detail {

// Add a and b in the domain of T. This guarantees that if T is a sub-int type,
// we cast away the promotion to int, so that unsigned overflow and underflow
// work as expected.
template <class T, class U>
FOLLY_ALWAYS_INLINE T addT(T a, U b) {
  static_assert(std::is_unsigned_v<T>);
  return static_cast<T>(a + static_cast<T>(b));
}

template <
    class Encoder,
    class Instructions,
    class SizeType,
    bool kUnchecked = false>
class UpperBitsReader : ForwardPointers<Encoder::forwardQuantum>,
                        SkipPointers<Encoder::skipQuantum> {
  using SkipValueType = typename Encoder::SkipValueType;

 public:
  using ValueType = typename Encoder::ValueType;
  static constexpr SizeType kBeforeFirstPos = -1;

  explicit UpperBitsReader(const typename Encoder::CompressedList& list)
      : ForwardPointers<Encoder::forwardQuantum>(list.forwardPointers),
        SkipPointers<Encoder::skipQuantum>(list.skipPointers),
        start_(list.upper),
        size_(list.size),
        upperBound_(estimateUpperBound(list)) {
    reset();
  }

  void reset() {
    // Pretend the bitvector is prefixed by a block of zeroes.
    block_ = 0;
    position_ = kBeforeFirstPos;
    outer_ = static_cast<OuterType>(-sizeof(block_t));
    value_ = 0;
  }

  FOLLY_ALWAYS_INLINE SizeType position() const { return position_; }

  FOLLY_ALWAYS_INLINE ValueType value() const { return value_; }

  FOLLY_ALWAYS_INLINE bool valid() const {
    // Also checks that position() != kBeforeFirstPos.
    return position() < size();
  }

  FOLLY_ALWAYS_INLINE SizeType size() const { return size_; }

  FOLLY_ALWAYS_INLINE bool previous() {
    if (!kUnchecked && UNLIKELY(position() == 0)) {
      return false;
    }

    size_t inner;
    block_t block;
    DCHECK_GE(outer_, 0);
    getPreviousInfo(block, inner, outer_); // Updates outer_.
    block_ = loadUnaligned<block_t>(start_ + outer_);
    block_ ^= block;
    --position_;
    return setValue(inner);
  }

  FOLLY_ALWAYS_INLINE bool next() {
    if (!kUnchecked && UNLIKELY(addT(position(), 1) >= size())) {
      return setDone();
    }

    // Skip to the first non-zero block.
    while (UNLIKELY(block_ == 0)) {
      outer_ += sizeof(block_t);
      block_ = loadUnaligned<block_t>(start_ + outer_);
    }

    ++position_;
    size_t inner = Instructions::ctz(block_);
    block_ = Instructions::blsr(block_);

    return setValue(inner);
  }

  FOLLY_ALWAYS_INLINE bool skip(SizeType n) {
    DCHECK_GT(n, 0);
    if (!kUnchecked && UNLIKELY(addT(position_, n) >= size())) {
      return setDone();
    }

    position_ += n; // n 1-bits will be read.

    // Use forward pointer.
    if constexpr (Encoder::forwardQuantum > 0) {
      if (UNLIKELY(n > Encoder::forwardQuantum)) {
        const size_t steps = position_ / Encoder::forwardQuantum;
        const size_t dest = loadUnaligned<SkipValueType>(
            this->forwardPointers_ + (steps - 1) * sizeof(SkipValueType));

        reposition(dest + steps * Encoder::forwardQuantum);
        n = position_ + 1 - steps * Encoder::forwardQuantum; // n is > 0.
      }
    }

    size_t cnt;
    // Find necessary block.
    while ((cnt = Instructions::popcount(block_)) < n) {
      n -= cnt;
      outer_ += sizeof(block_t);
      block_ = loadUnaligned<block_t>(start_ + outer_);
    }

    // Skip to the n-th one in the block.
    DCHECK_GT(n, 0);
    size_t inner = select64<Instructions>(block_, n - 1);
    block_ &= (block_t(-1) << inner) << 1;

    return setValue(inner);
  }

  // Skip to the first element that is >= v and located *after* the current
  // one (so even if current value equals v, position will be increased by 1).
  FOLLY_ALWAYS_INLINE bool skipToNext(ValueType v) {
    DCHECK_GE(v, value_);
    if (!kUnchecked && UNLIKELY(v > upperBound_)) {
      return setDone();
    }

    // Use skip pointer.
    if constexpr (Encoder::skipQuantum > 0) {
      // NOTE: The addition can overflow here, but that means value_ is within
      // skipQuantum_ distance from the maximum representable value, and thus
      // the last value, so the comparison is still correct.
      if (UNLIKELY(v >= addT(value_, Encoder::skipQuantum))) {
        const size_t steps = v / Encoder::skipQuantum;
        const size_t dest = loadUnaligned<SkipValueType>(
            this->skipPointers_ + (steps - 1) * sizeof(SkipValueType));

        DCHECK_LE(dest, size());
        if (!kUnchecked && UNLIKELY(dest == size())) {
          return setDone();
        }

        reposition(dest + Encoder::skipQuantum * steps);
        position_ = dest - 1;

        // Correct value_ will be set during the next() call at the end.

        // NOTE: Corresponding block of lower bits sequence may be
        // prefetched here (via __builtin_prefetch), but experiments
        // didn't show any significant improvements.
      }
    }

    // Skip by blocks.
    size_t cnt;
    // outer_ and position_ rely on negative sentinel values. We enforce the
    // overflown bits are dropped by explicitly casting the final value to
    // SizeType first, followed by a potential implicit cast to size_t.
    size_t skip = static_cast<SizeType>(v - (8 * outer_ - position_ - 1));

    constexpr size_t kBitsPerBlock = 8 * sizeof(block_t);
    while ((cnt = Instructions::popcount(~block_)) < skip) {
      skip -= cnt;
      position_ += kBitsPerBlock - cnt;
      outer_ += sizeof(block_t);
      DCHECK_LT(outer_, (static_cast<size_t>(upperBound_) + size() + 7) / 8);
      block_ = loadUnaligned<block_t>(start_ + outer_);
    }

    if (LIKELY(skip)) {
      auto inner = select64<Instructions>(~block_, skip - 1);
      position_ += inner - skip + 1;
      block_ &= block_t(-1) << inner;
    }

    DCHECK_LT(addT(position(), 1), addT(size(), 1));
    return next();
  }

  /**
   * Try to prepare to skip to value. This is a constant-time operation that
   * will attempt to prefetch memory required for a subsequent skipTo(value)
   * call if the value to skip to is within this list.
   *
   * Returns:
   *   {true, position of the reader} if the skip is valid,
   *   {false, size()} otherwise.
   */
  FOLLY_ALWAYS_INLINE std::pair<bool, SizeType> prepareSkipTo(
      ValueType v) const {
    if (!kUnchecked && UNLIKELY(v > upperBound_)) {
      return std::make_pair(false, size());
    }
    auto position = position_;

    if constexpr (Encoder::skipQuantum > 0) {
      if (v >= addT(value_, Encoder::skipQuantum)) {
        auto outer = outer_;
        const size_t steps = v / Encoder::skipQuantum;
        const size_t dest = loadUnaligned<SkipValueType>(
            this->skipPointers_ + (steps - 1) * sizeof(SkipValueType));

        DCHECK_LE(dest, size());
        if (!kUnchecked && UNLIKELY(dest == size())) {
          return std::make_pair(false, size());
        }

        position = dest - 1;
        outer = (dest + Encoder::skipQuantum * steps) / 8;

        // Prefetch up to the beginning of where we linear search. After that,
        // hardware prefetching will outperform our own. In addition, this
        // simplifies calculating what to prefetch as we don't have to calculate
        // the entire destination address. Two cache lines are prefetched
        // because this results in fewer cycles used (based on practical
        // results) than one. However, three cache lines does not have any
        // additional effect.
        const auto addr = start_ + outer;
        __builtin_prefetch(addr);
        __builtin_prefetch(addr + kCacheLineSize);
      }
    }

    return std::make_pair(true, position);
  }

  FOLLY_ALWAYS_INLINE ValueType previousValue() const {
    block_t block;
    size_t inner;
    OuterType outer;
    getPreviousInfo(block, inner, outer);
    return static_cast<ValueType>(8 * outer + inner - (position_ - 1));
  }

  // Returns true if we're at the beginning of the list, or previousValue() !=
  // value().
  FOLLY_ALWAYS_INLINE bool isAtBeginningOfRun() const {
    DCHECK_NE(position(), kBeforeFirstPos);
    if (position_ == 0) {
      return true;
    }
    size_t bitPos = size_t(value_) + position_ - 1;
    return (start_[bitPos / 8] & (1 << (bitPos % 8))) == 0;
  }

 private:
  using block_t = uint64_t;
  // The size in bytes of the upper bits is limited by n + universe / 8,
  // so a type that can hold either sizes or values is sufficient.
  using OuterType = typename std::common_type_t<ValueType, SizeType>;

  static ValueType estimateUpperBound(
      const typename Encoder::CompressedList& list) {
    size_t upperBound = 8 * list.upperSizeBytes - list.size;
    // The bitvector is byte-aligned, so we may be overestimating the universe
    // size. Make sure it fits in ValueType.
    return static_cast<ValueType>(std::min<size_t>(
        upperBound,
        std::numeric_limits<ValueType>::max() >> list.numLowerBits));
  }

  FOLLY_ALWAYS_INLINE bool setValue(size_t inner) {
    value_ = static_cast<ValueType>(8 * outer_ + inner - position_);
    return true;
  }

  FOLLY_ALWAYS_INLINE bool setDone() {
    position_ = size_;
    return false;
  }

  // NOTE: dest is a position in the bit vector, use size_t as SizeType may
  // not be sufficient here.
  FOLLY_ALWAYS_INLINE void reposition(size_t dest) {
    outer_ = dest / 8;
    DCHECK_LT(outer_, (static_cast<size_t>(upperBound_) + size() + 7) / 8);
    block_ = loadUnaligned<block_t>(start_ + outer_);
    block_ &= ~((block_t(1) << (dest % 8)) - 1);
  }

  FOLLY_ALWAYS_INLINE void getPreviousInfo(
      block_t& block, size_t& inner, OuterType& outer) const {
    DCHECK_GT(position(), 0);
    DCHECK_LT(position(), size());

    outer = outer_;
    block = loadUnaligned<block_t>(start_ + outer);
    inner = size_t(value_) - 8 * outer_ + position_;
    block &= (block_t(1) << inner) - 1;
    while (UNLIKELY(block == 0)) {
      DCHECK_GT(outer, 0);
      outer -= std::min<OuterType>(sizeof(block_t), outer);
      block = loadUnaligned<block_t>(start_ + outer);
    }
    inner = 8 * sizeof(block_t) - 1 - Instructions::clz(block);
  }

  const unsigned char* const start_;
  const SizeType size_; // Size of the list.
  const ValueType upperBound_; // Upper bound of values in this list.
  block_t block_;
  SizeType position_; // Index of current value (= #reads - 1).
  OuterType outer_; // Outer offset: number of consumed bytes in upper.
  ValueType value_;
};

} // namespace detail

// If kUnchecked = true the caller must guarantee that all the operations return
// valid elements, i.e., they would never return false if checked.
//
// If the list length is known to be representable with a type narrower than the
// SkipValueType used in the format, the reader footprint can be reduced by
// passing the type as SizeType.
template <
    class Encoder,
    class Instructions = instructions::Default,
    bool kUnchecked = false,
    class SizeT = typename Encoder::SkipValueType>
class EliasFanoReader {
  using UpperBitsReader =
      detail::UpperBitsReader<Encoder, Instructions, SizeT, kUnchecked>;

 public:
  using EncoderType = Encoder;
  using ValueType = typename Encoder::ValueType;
  using SizeType = SizeT;

  explicit EliasFanoReader(const typename Encoder::CompressedList& list)
      : upper_(list),
        lower_(list.lower),
        value_(),
        numLowerBits_(list.numLowerBits) {
    DCHECK_LE(list.size, std::numeric_limits<SizeType>::max());
    DCHECK(Instructions::supported());
  }

  void reset() { upper_.reset(); }

  bool previous() {
    if (LIKELY(upper_.previous())) {
      return setValue(readCurrentValue());
    }
    reset();
    return false;
  }

  bool next() {
    if (LIKELY(upper_.next())) {
      return setValue(readCurrentValue());
    }
    return false;
  }

  /**
   * Advances by n elements. n = 0 is allowed and has no effect. Returns false
   * if the end of the list is reached. position() + n must be representable by
   * SizeType.
   */
  bool skip(SizeType n) {
    if (n == 0) {
      return valid();
    }
    if (!upper_.skip(n)) {
      return false;
    }
    return setValue(readCurrentValue());
  }

  /**
   * Skips to the first element >= value whose position is greater or equal to
   * the current position.
   * Requires that value >= value() (or that the reader is positioned before the
   * first element). Returns false if no such element exists.
   * If kCanBeAtValue is false, the requirement above becomes value > value().
   */
  template <bool kCanBeAtValue = true>
  bool skipTo(ValueType value) {
    if (valid()) {
      if constexpr (kCanBeAtValue) {
        DCHECK_GE(value, value_);
        if (UNLIKELY(value == value_)) {
          return true;
        }
      } else {
        DCHECK_GT(value, value_);
      }
    }

    ValueType upperValue = value >> numLowerBits_;

    if (UNLIKELY(!upper_.skipToNext(upperValue))) {
      return false;
    }

    do {
      if (auto cur = readCurrentValue(); LIKELY(cur >= value)) {
        return setValue(cur);
      }
    } while (LIKELY(upper_.next()));

    return false;
  }

  /**
   * Prepare to skip to `value` by prefetching appropriate memory in both the
   * upper and lower bits.
   */
  template <bool kCanBeAtValue = true>
  void prepareSkipTo(ValueType value) const {
    if (valid()) {
      if constexpr (kCanBeAtValue) {
        DCHECK_GE(value, value_);
        if (UNLIKELY(value == value_)) {
          return;
        }
      } else {
        DCHECK_GT(value, value_);
      }
    }

    // Do minimal computation required to prefetch address used in
    // `readLowerPart()`.
    ValueType upperValue = value >> numLowerBits_;
    const auto [valid, upperPosition] = upper_.prepareSkipTo(upperValue);
    if (!valid) {
      return;
    }
    const auto addr = lower_ + (upperPosition * numLowerBits_ / 8);
    __builtin_prefetch(addr);
    __builtin_prefetch(addr + kCacheLineSize);
  }

  /**
   * Jumps to the element at position n. The reader can be in any state. Returns
   * false if n >= size().
   */
  bool jump(SizeType n) {
    // Also works if position() == -1.
    if (detail::addT(n, 1) < detail::addT(position(), 1)) {
      reset();
      n += 1; // Initial position is -1.
    } else {
      n -= position();
    }
    return skip(n);
  }

  /**
   * Jumps to the first element >= value. The reader can be in any
   * state. Returns false if no such element exists.
   *
   * If all the values in the list can be assumed distinct, setting
   * assumeDistinct = true can enable some optimizations.
   */
  bool jumpTo(ValueType value, bool assumeDistinct = false) {
    if (valid() && value == value_) {
      if (assumeDistinct == true) {
        return true;
      }

      // We might be in the middle of a run of equal values, reposition by
      // iterating backwards to its first element.
      auto valueLower = Instructions::bzhi(value_, numLowerBits_);
      while (!upper_.isAtBeginningOfRun() &&
             readLowerPart(position() - 1) == valueLower) {
        upper_.previous();
      }
      return true;
    }

    // We need to reset if we're not in the initial state and the jump is
    // backwards.
    if (position() != UpperBitsReader::kBeforeFirstPos &&
        (position() == size() || value < value_)) {
      reset();
    }
    return skipTo(value);
  }

  ValueType previousValue() const {
    DCHECK_GT(position(), 0);
    DCHECK_LT(position(), size());
    return readLowerPart(position() - 1) |
        (upper_.previousValue() << numLowerBits_);
  }

  SizeType size() const { return upper_.size(); }

  bool valid() const { return upper_.valid(); }

  SizeType position() const { return upper_.position(); }

  ValueType value() const {
    DCHECK(valid());
    return value_;
  }

 private:
  FOLLY_ALWAYS_INLINE bool setValue(ValueType value) {
    DCHECK(valid());
    value_ = value;
    return true;
  }

  FOLLY_ALWAYS_INLINE ValueType readLowerPart(SizeType i) const {
    DCHECK_LT(i, size());
    const size_t pos = i * numLowerBits_;
    const unsigned char* ptr = lower_ + (pos / 8);
    const uint64_t ptrv = loadUnaligned<uint64_t>(ptr);
    // This removes the branch in the fallback implementation of
    // bzhi. The condition is verified at encoding time.
    assume(numLowerBits_ < sizeof(ValueType) * 8);
    return Instructions::bzhi(ptrv >> (pos % 8), numLowerBits_);
  }

  FOLLY_ALWAYS_INLINE ValueType readCurrentValue() {
    return readLowerPart(position()) | (upper_.value() << numLowerBits_);
  }

  // Ordering of fields is counter-intutive but it optimizes the layout.
  UpperBitsReader upper_;
  const uint8_t* const lower_;
  ValueType value_;
  const uint8_t numLowerBits_;
};

} // namespace compression
} // namespace folly
