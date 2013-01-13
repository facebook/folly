/*
 * Copyright 2013 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
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

#ifndef FOLLY_EXPERIMENTAL_ELIAS_FANO_CODING_H
#define FOLLY_EXPERIMENTAL_ELIAS_FANO_CODING_H

#ifndef __GNUC__
#error EliasFanoCoding.h requires GCC
#endif

#if !defined(__x86_64__)
#error EliasFanoCoding.h requires x86_64
#endif

#include <cstdlib>
#include <endian.h>
#include <algorithm>
#include <limits>
#include <type_traits>
#include <boost/noncopyable.hpp>
#include <glog/logging.h>
#include "folly/Bits.h"
#include "folly/CpuId.h"
#include "folly/Likely.h"
#include "folly/Range.h"

#if __BYTE_ORDER != __LITTLE_ENDIAN
#error EliasFanoCoding.h requires little endianness
#endif

namespace folly { namespace compression {

template <class Value,
          class SkipValue = size_t,
          size_t kSkipQuantum = 0,     // 0 = disabled
          size_t kForwardQuantum = 0>  // 0 = disabled
struct EliasFanoCompressedList {
  static_assert(std::is_integral<Value>::value &&
                std::is_unsigned<Value>::value,
                "Value should be unsigned integral");

  typedef Value ValueType;
  typedef SkipValue SkipValueType;

  EliasFanoCompressedList()
    : size(0), numLowerBits(0) { }

  static constexpr size_t skipQuantum = kSkipQuantum;
  static constexpr size_t forwardQuantum = kForwardQuantum;

  size_t size;
  uint8_t numLowerBits;

  // WARNING: EliasFanoCompressedList has no ownership of
  // lower, upper, skipPointers and forwardPointers.
  // The 7 bytes following the last byte of lower and upper
  // sequences should be readable.
  folly::ByteRange lower;
  folly::ByteRange upper;

  folly::ByteRange skipPointers;
  folly::ByteRange forwardPointers;

  void free() {
    ::free(const_cast<unsigned char*>(lower.data()));
    ::free(const_cast<unsigned char*>(upper.data()));
    ::free(const_cast<unsigned char*>(skipPointers.data()));
    ::free(const_cast<unsigned char*>(forwardPointers.data()));
  }

  static uint8_t defaultNumLowerBits(size_t upperBound, size_t size) {
    if (size == 0 || upperBound < size) {
      return 0;
    }
    // floor(log(upperBound / size));
    return folly::findLastSet(upperBound / size) - 1;
  }

  // WARNING: encode() mallocates lower, upper, skipPointers
  // and forwardPointers. As EliasFanoCompressedList has
  // no ownership of them, you need to call free() explicitly.
  static void encode(const ValueType* list, size_t size,
                     EliasFanoCompressedList& result) {
    encode(list, list + size, result);
  }

  template <class RandomAccessIterator>
  static void encode(RandomAccessIterator begin,
                     RandomAccessIterator end,
                     EliasFanoCompressedList& result) {
    auto list = begin;
    const size_t size = end - begin;

    if (size == 0) {
      result = EliasFanoCompressedList();
      return;
    }

    DCHECK(std::is_sorted(list, list + size));

    const ValueType upperBound = list[size - 1];
    uint8_t numLowerBits = defaultNumLowerBits(upperBound, size);

    // This is detail::writeBits56 limitation.
    numLowerBits = std::min<uint8_t>(numLowerBits, 56);
    CHECK_LT(numLowerBits, 8 * sizeof(Value));  // As we shift by numLowerBits.

    // WARNING: Current read/write logic assumes that the 7 bytes
    // following the last byte of lower and upper sequences are
    // readable (stored value doesn't matter and won't be changed),
    // so we allocate additional 7B, but do not include them in size
    // of returned value.

    // *** Lower bits.
    const size_t lowerSize = (numLowerBits * size + 7) / 8;
    unsigned char* const lower =
      static_cast<unsigned char*>(calloc(lowerSize + 7, 1));
    const ValueType lowerMask = (ValueType(1) << numLowerBits) - 1;
    for (size_t i = 0; i < size; ++i) {
      const ValueType lowerBits = list[i] & lowerMask;
      writeBits56(lower, i * numLowerBits, numLowerBits, lowerBits);
    }

    // *** Upper bits.
    // Upper bits are stored using unary delta encoding.
    // For example, (3 5 5 9) will be encoded as 1000011001000_2.
    const size_t upperSizeBits =
      (upperBound >> numLowerBits) +  // Number of 0-bits to be stored.
      size;                           // 1-bits.
    const size_t upperSize = (upperSizeBits + 7) / 8;
    unsigned char* const upper =
      static_cast<unsigned char*>(calloc(upperSize + 7, 1));
    for (size_t i = 0; i < size; ++i) {
      const ValueType upperBits = list[i] >> numLowerBits;
      const size_t pos = upperBits + i;  // upperBits 0-bits and (i + 1) 1-bits.
      upper[pos / 8] |= 1U << (pos % 8);
    }

    // *** Skip pointers.
    // Store (1-indexed) position of every skipQuantum-th
    // 0-bit in upper bits sequence.
    SkipValueType* skipPointers = nullptr;
    size_t numSkipPointers = 0;
    /* static */ if (skipQuantum != 0) {
      // Workaround to avoid 'division by zero' compile-time error.
      constexpr size_t q = skipQuantum ?: 1;
      CHECK_LT(upperSizeBits, std::numeric_limits<SkipValueType>::max());
      // 8 * upperSize is used here instead of upperSizeBits, as that is
      // more serialization-friendly way.
      numSkipPointers = (8 * upperSize - size) / q;
      skipPointers = static_cast<SkipValueType*>(
          numSkipPointers == 0
            ? nullptr
            : calloc(numSkipPointers, sizeof(SkipValueType)));

      for (size_t i = 0, pos = 0; i < size; ++i) {
        const ValueType upperBits = list[i] >> numLowerBits;
        for (; (pos + 1) * q <= upperBits; ++pos) {
          skipPointers[pos] = i + (pos + 1) * q;
        }
      }
    }

    // *** Forward pointers.
    // Store (1-indexed) position of every forwardQuantum-th
    // 1-bit in upper bits sequence.
    SkipValueType* forwardPointers = nullptr;
    size_t numForwardPointers = 0;
    /* static */ if (forwardQuantum != 0) {
      // Workaround to avoid 'division by zero' compile-time error.
      constexpr size_t q = forwardQuantum ?: 1;
      CHECK_LT(upperSizeBits, std::numeric_limits<SkipValueType>::max());

      numForwardPointers = size / q;
      forwardPointers = static_cast<SkipValueType*>(
        numForwardPointers == 0
          ? nullptr
          : malloc(numForwardPointers * sizeof(SkipValueType)));

      for (size_t i = q - 1, pos = 0; i < size; i += q, ++pos) {
        const ValueType upperBits = list[i] >> numLowerBits;
        forwardPointers[pos] = upperBits + i + 1;
      }
    }

    // *** Result.
    result.size = size;
    result.numLowerBits = numLowerBits;
    result.lower.reset(lower, lowerSize);
    result.upper.reset(upper, upperSize);
    result.skipPointers.reset(
        reinterpret_cast<unsigned char*>(skipPointers),
        numSkipPointers * sizeof(SkipValueType));
    result.forwardPointers.reset(
        reinterpret_cast<unsigned char*>(forwardPointers),
        numForwardPointers * sizeof(SkipValueType));
  }

 private:
  // Writes value (with len up to 56 bits) to data starting at pos-th bit.
  static void writeBits56(unsigned char* data, size_t pos,
                          uint8_t len, uint64_t value) {
    DCHECK_LE(uint32_t(len), 56);
    DCHECK_EQ(0, value & ~((uint64_t(1) << len) - 1));
    unsigned char* const ptr = data + (pos / 8);
    uint64_t ptrv = folly::loadUnaligned<uint64_t>(ptr);
    ptrv |=  value << (pos % 8);
    folly::storeUnaligned<uint64_t>(ptr, ptrv);
  }
};

// NOTE: It's recommended to compile EF coding with -msse4.2, starting
// with Nehalem, Intel CPUs support POPCNT instruction and gcc will emit
// it for __builtin_popcountll intrinsic.
// But we provide an alternative way for the client code: it can switch to
// the appropriate version of EliasFanoReader<> in realtime (client should
// implement this switching logic itself) by specifying instruction set to
// use explicitly.
namespace instructions {

struct Default {
  static bool supported() {
    return true;
  }
  static inline uint64_t popcount(uint64_t value) {
    return __builtin_popcountll(value);
  }
  static inline int ctz(uint64_t value) {
    DCHECK_GT(value, 0);
    return __builtin_ctzll(value);
  }
};

struct Fast : public Default {
  static bool supported() {
    folly::CpuId cpuId;
    return cpuId.popcnt();
  }
  static inline uint64_t popcount(uint64_t value) {
    uint64_t result;
    asm ("popcntq %1, %0" : "=r" (result) : "r" (value));
    return result;
  }
};

}  // namespace instructions

namespace detail {

template <class CompressedList, class Instructions>
class UpperBitsReader {
  typedef typename CompressedList::SkipValueType SkipValueType;
 public:
  typedef typename CompressedList::ValueType ValueType;

  explicit UpperBitsReader(const CompressedList& list)
    : forwardPointers_(list.forwardPointers.data()),
      skipPointers_(list.skipPointers.data()),
      start_(list.upper.data()),
      block_(start_ != nullptr ? folly::loadUnaligned<block_t>(start_) : 0),
      outer_(0),  // outer offset: number of consumed bytes in upper.
      inner_(-1),  // inner offset: (bit) position in current block.
      position_(-1),  // index of current value (= #reads - 1).
      value_(0) { }

  size_t position() const { return position_; }
  ValueType value() const { return value_; }

  ValueType next() {
    // Skip to the first non-zero block.
    while (block_ == 0) {
      outer_ += sizeof(block_t);
      block_ = folly::loadUnaligned<block_t>(start_ + outer_);
    }

    ++position_;
    inner_ = Instructions::ctz(block_);
    block_ &= block_ - 1;

    return setValue();
  }

  ValueType skip(size_t n) {
    DCHECK_GT(n, 0);

    position_ += n;  // n 1-bits will be read.

    // Use forward pointer.
    if (CompressedList::forwardQuantum > 0 &&
        n > CompressedList::forwardQuantum) {
      // Workaround to avoid 'division by zero' compile-time error.
      constexpr size_t q = CompressedList::forwardQuantum ?: 1;

      const size_t steps = position_ / q;
      const size_t dest =
        folly::loadUnaligned<SkipValueType>(
            forwardPointers_ + (steps - 1) * sizeof(SkipValueType));

      reposition(dest);
      n = position_ + 1 - steps * q;  // n is > 0.
      // correct inner_ will be set at the end.
    }

    size_t cnt;
    // Find necessary block.
    while ((cnt = Instructions::popcount(block_)) < n) {
      n -= cnt;
      outer_ += sizeof(block_t);
      block_ = folly::loadUnaligned<block_t>(start_ + outer_);
    }

    // NOTE: Trying to skip half-block here didn't show any
    // performance improvements.

    DCHECK_GT(n, 0);

    // Kill n - 1 least significant 1-bits.
    for (size_t i = 0; i < n - 1; ++i) {
      block_ &= block_ - 1;
    }

    inner_ = Instructions::ctz(block_);
    block_ &= block_ - 1;

    return setValue();
  }

  // Skip to the first element that is >= v and located *after* the current
  // one (so even if current value equals v, position will be increased by 1).
  ValueType skipToNext(ValueType v) {
    DCHECK_GE(v, value_);

    // Use skip pointer.
    if (CompressedList::skipQuantum > 0 &&
        v >= value_ + CompressedList::skipQuantum) {
      // Workaround to avoid 'division by zero' compile-time error.
      constexpr size_t q = CompressedList::skipQuantum ?: 1;

      const size_t steps = v / q;
      const size_t dest =
        folly::loadUnaligned<SkipValueType>(
            skipPointers_ + (steps - 1) * sizeof(SkipValueType));

      reposition(dest);
      position_ = dest - q * steps - 1;
      // Correct inner_ and value_ will be set during the next()
      // call at the end.

      // NOTE: Corresponding block of lower bits sequence may be
      // prefetched here (via __builtin_prefetch), but experiments
      // didn't show any significant improvements.
    }

    // Skip by blocks.
    size_t cnt;
    size_t skip = v - (8 * outer_ - position_ - 1);

    constexpr size_t kBitsPerBlock = 8 * sizeof(block_t);
    while ((cnt = Instructions::popcount(~block_)) < skip) {
      skip -= cnt;
      position_ += kBitsPerBlock - cnt;
      outer_ += sizeof(block_t);
      block_ = folly::loadUnaligned<block_t>(start_ + outer_);
    }

    // Try to skip half-block.
    constexpr size_t kBitsPerHalfBlock = 4 * sizeof(block_t);
    constexpr block_t halfBlockMask = (block_t(1) << kBitsPerHalfBlock) - 1;
    if ((cnt = Instructions::popcount(~block_ & halfBlockMask)) < skip) {
      position_ += kBitsPerHalfBlock - cnt;
      block_ &= ~halfBlockMask;
    }

    // Just skip until we see expected value.
    while (next() < v) { }
    return value_;
  }

 private:
  ValueType setValue() {
    value_ = static_cast<ValueType>(8 * outer_ + inner_ - position_);
    return value_;
  }

  void reposition(size_t dest) {
    outer_ = dest / 8;
    block_ = folly::loadUnaligned<block_t>(start_ + outer_);
    block_ &= ~((block_t(1) << (dest % 8)) - 1);
  }

  typedef unsigned long long block_t;
  const unsigned char* const forwardPointers_;
  const unsigned char* const skipPointers_;
  const unsigned char* const start_;
  block_t block_;
  size_t outer_;
  size_t inner_;
  size_t position_;
  ValueType value_;
};

}  // namespace detail

template <class CompressedList,
          class Instructions = instructions::Default>
class EliasFanoReader : private boost::noncopyable {
 public:
  typedef typename CompressedList::ValueType ValueType;

  explicit EliasFanoReader(const CompressedList& list)
    : list_(list),
      lowerMask_((ValueType(1) << list_.numLowerBits) - 1),
      upper_(list),
      progress_(0),
      value_(0) {
    DCHECK(Instructions::supported());
    // To avoid extra branching during skipTo() while reading
    // upper sequence we need to know the last element.
    if (UNLIKELY(list_.size == 0)) {
      lastValue_ = 0;
      return;
    }
    ValueType lastUpperValue = 8 * list_.upper.size() - list_.size;
    auto it = list_.upper.end() - 1;
    DCHECK_NE(*it, 0);
    lastUpperValue -= 8 - folly::findLastSet(*it);
    lastValue_ = readLowerPart(list_.size - 1) |
                 (lastUpperValue << list_.numLowerBits);
  }

  size_t size() const { return list_.size; }

  size_t position() const { return progress_ - 1; }
  ValueType value() const { return value_; }

  bool next() {
    if (UNLIKELY(progress_ == list_.size)) {
      value_ = std::numeric_limits<ValueType>::max();
      return false;
    }
    value_ = readLowerPart(progress_) |
             (upper_.next() << list_.numLowerBits);
    ++progress_;
    return true;
  }

  bool skip(size_t n) {
    CHECK_GT(n, 0);

    progress_ += n - 1;
    if (LIKELY(progress_ < list_.size)) {
      value_ = readLowerPart(progress_) |
               (upper_.skip(n) << list_.numLowerBits);
      ++progress_;
      return true;
    }

    progress_ = list_.size;
    value_ = std::numeric_limits<ValueType>::max();
    return false;
  }

  bool skipTo(ValueType value) {
    DCHECK_GE(value, value_);
    if (value <= value_) {
      return true;
    }
    if (value > lastValue_) {
      progress_ = list_.size;
      value_ = std::numeric_limits<ValueType>::max();
      return false;
    }

    upper_.skipToNext(value >> list_.numLowerBits);
    progress_ = upper_.position();
    value_ = readLowerPart(progress_) |
             (upper_.value() << list_.numLowerBits);
    ++progress_;
    while (value_ < value) {
      value_ = readLowerPart(progress_) |
               (upper_.next() << list_.numLowerBits);
      ++progress_;
    }

    return true;
  }

 private:
  ValueType readLowerPart(size_t i) const {
    const size_t pos = i * list_.numLowerBits;
    const unsigned char* ptr = list_.lower.data() + (pos / 8);
    const uint64_t ptrv = folly::loadUnaligned<uint64_t>(ptr);
    return lowerMask_ & (ptrv >> (pos % 8));
  }

  const CompressedList list_;
  const ValueType lowerMask_;
  detail::UpperBitsReader<CompressedList, Instructions> upper_;
  size_t progress_;
  ValueType value_;
  ValueType lastValue_;
};

}}  // namespaces

#endif  // FOLLY_EXPERIMENTAL_ELIAS_FANO_CODING_H
