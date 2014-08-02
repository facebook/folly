/*
 * Copyright 2014 Facebook, Inc.
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

#if !FOLLY_X64
#error EliasFanoCoding.h requires x86_64
#endif

#include <cstdlib>
#include <limits>
#include <type_traits>
#include <boost/noncopyable.hpp>
#include <glog/logging.h>

#include <folly/Bits.h>
#include <folly/CpuId.h>
#include <folly/Likely.h>
#include <folly/Range.h>

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error EliasFanoCoding.h requires little endianness
#endif

namespace folly { namespace compression {

struct EliasFanoCompressedList {
  EliasFanoCompressedList() { }

  void free() {
    ::free(const_cast<unsigned char*>(lower.data()));
    ::free(const_cast<unsigned char*>(upper.data()));
    ::free(const_cast<unsigned char*>(skipPointers.data()));
    ::free(const_cast<unsigned char*>(forwardPointers.data()));
  }

  size_t size = 0;
  uint8_t numLowerBits = 0;

  // WARNING: EliasFanoCompressedList has no ownership of
  // lower, upper, skipPointers and forwardPointers.
  // The 7 bytes following the last byte of lower and upper
  // sequences should be readable.
  folly::ByteRange lower;
  folly::ByteRange upper;

  folly::ByteRange skipPointers;
  folly::ByteRange forwardPointers;
};

// Version history:
// In version 1 skip / forward pointers encoding has been changed,
// so SkipValue = uint32_t is able to address up to ~4B elements,
// instead of only ~2B.
template <class Value,
          class SkipValue = size_t,
          size_t kSkipQuantum = 0,     // 0 = disabled
          size_t kForwardQuantum = 0,  // 0 = disabled
          size_t kVersion = 0>
struct EliasFanoEncoder {
  static_assert(std::is_integral<Value>::value &&
                std::is_unsigned<Value>::value,
                "Value should be unsigned integral");

  typedef EliasFanoCompressedList CompressedList;

  typedef Value ValueType;
  typedef SkipValue SkipValueType;

  static constexpr size_t skipQuantum = kSkipQuantum;
  static constexpr size_t forwardQuantum = kForwardQuantum;
  static constexpr size_t version = kVersion;

  static uint8_t defaultNumLowerBits(size_t upperBound, size_t size) {
    if (size == 0 || upperBound < size) {
      return 0;
    }
    // floor(log(upperBound / size));
    return folly::findLastSet(upperBound / size) - 1;
  }

  // Requires: input range (begin, end) is sorted (encoding
  // crashes if it's not).
  // WARNING: encode() mallocates lower, upper, skipPointers
  // and forwardPointers. As EliasFanoCompressedList has
  // no ownership of them, you need to call free() explicitly.
  template <class RandomAccessIterator>
  static EliasFanoCompressedList encode(RandomAccessIterator begin,
                                        RandomAccessIterator end) {
    if (begin == end) {
      return EliasFanoCompressedList();
    }
    EliasFanoEncoder encoder(end - begin, *(end - 1));
    for (; begin != end; ++begin) {
      encoder.add(*begin);
    }
    return encoder.finish();
  }

  EliasFanoEncoder(size_t size, ValueType upperBound) {
    if (size == 0) {
      return;
    }

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
    if (lowerSize > 0) {  // numLowerBits != 0
      lower_ = static_cast<unsigned char*>(calloc(lowerSize + 7, 1));
    }

    // *** Upper bits.
    // Upper bits are stored using unary delta encoding.
    // For example, (3 5 5 9) will be encoded as 1000011001000_2.
    const size_t upperSizeBits =
      (upperBound >> numLowerBits) +  // Number of 0-bits to be stored.
      size;                           // 1-bits.
    const size_t upperSize = (upperSizeBits + 7) / 8;
    upper_ = static_cast<unsigned char*>(calloc(upperSize + 7, 1));

    // *** Skip pointers.
    // Store (1-indexed) position of every skipQuantum-th
    // 0-bit in upper bits sequence.
    size_t numSkipPointers = 0;
    /* static */ if (skipQuantum != 0) {
      /* static */ if (kVersion > 0) {
        CHECK_LT(size, std::numeric_limits<SkipValueType>::max());
      } else {
        CHECK_LT(upperSizeBits, std::numeric_limits<SkipValueType>::max());
      }
      // 8 * upperSize is used here instead of upperSizeBits, as that is
      // more serialization-friendly way (upperSizeBits isn't known outside of
      // this function, unlike upperSize; thus numSkipPointers could easily be
      // deduced from upperSize).
      numSkipPointers = (8 * upperSize - size) / (skipQuantum ?: 1);
      skipPointers_ = static_cast<SkipValueType*>(
          numSkipPointers == 0
            ? nullptr
            : calloc(numSkipPointers, sizeof(SkipValueType)));
    }

    // *** Forward pointers.
    // Store (1-indexed) position of every forwardQuantum-th
    // 1-bit in upper bits sequence.
    size_t numForwardPointers = 0;
    /* static */ if (forwardQuantum != 0) {
      /* static */ if (kVersion > 0) {
        CHECK_LT(upperBound >> numLowerBits,
                 std::numeric_limits<SkipValueType>::max());
      } else {
        CHECK_LT(upperSizeBits, std::numeric_limits<SkipValueType>::max());
      }

      // '?: 1' is a workaround for false 'division by zero' compile-time error.
      numForwardPointers = size / (forwardQuantum ?: 1);
      forwardPointers_ = static_cast<SkipValueType*>(
        numForwardPointers == 0
          ? nullptr
          : malloc(numForwardPointers * sizeof(SkipValueType)));
    }

    // *** Result.
    result_.size = size;
    result_.numLowerBits = numLowerBits;
    result_.lower.reset(lower_, lowerSize);
    result_.upper.reset(upper_, upperSize);
    result_.skipPointers.reset(
        reinterpret_cast<unsigned char*>(skipPointers_),
        numSkipPointers * sizeof(SkipValueType));
    result_.forwardPointers.reset(
        reinterpret_cast<unsigned char*>(forwardPointers_),
        numForwardPointers * sizeof(SkipValueType));
  }

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

    /* static */ if (skipQuantum != 0) {
      while ((skipPointersSize_ + 1) * skipQuantum <= upperBits) {
        /* static */ if (kVersion > 0) {
          // Since version 1, just the number of preceding 1-bits is stored.
          skipPointers_[skipPointersSize_] = size_;
        } else {
          skipPointers_[skipPointersSize_] =
            size_ + (skipPointersSize_ + 1) * skipQuantum;
        }
        ++skipPointersSize_;
      }
    }

    /* static */ if (forwardQuantum != 0) {
      if ((size_ + 1) % forwardQuantum == 0) {
        const auto pos = size_ / forwardQuantum;
        /* static */ if (kVersion > 0) {
          // Since version 1, just the number of preceding 0-bits is stored.
          forwardPointers_[pos] = upperBits;
        } else {
          forwardPointers_[pos] = upperBits + size_ + 1;
        }
      }
    }

    lastValue_ = value;
    ++size_;
  }

  const EliasFanoCompressedList& finish() const {
    CHECK_EQ(size_, result_.size);
    return result_;
  }

 private:
  // Writes value (with len up to 56 bits) to data starting at pos-th bit.
  static void writeBits56(unsigned char* data, size_t pos,
                          uint8_t len, uint64_t value) {
    DCHECK_LE(uint32_t(len), 56);
    DCHECK_EQ(0, value & ~((uint64_t(1) << len) - 1));
    unsigned char* const ptr = data + (pos / 8);
    uint64_t ptrv = folly::loadUnaligned<uint64_t>(ptr);
    ptrv |= value << (pos % 8);
    folly::storeUnaligned<uint64_t>(ptr, ptrv);
  }

  unsigned char* lower_ = nullptr;
  unsigned char* upper_ = nullptr;
  SkipValueType* skipPointers_ = nullptr;
  SkipValueType* forwardPointers_ = nullptr;

  ValueType lastValue_ = 0;
  size_t size_ = 0;
  size_t skipPointersSize_ = 0;

  EliasFanoCompressedList result_;
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

template <class Encoder, class Instructions>
class UpperBitsReader {
  typedef typename Encoder::SkipValueType SkipValueType;
 public:
  typedef typename Encoder::ValueType ValueType;

  explicit UpperBitsReader(const EliasFanoCompressedList& list)
    : forwardPointers_(list.forwardPointers.data()),
      skipPointers_(list.skipPointers.data()),
      start_(list.upper.data()) {
    reset();
  }

  void reset() {
    block_ = start_ != nullptr ? folly::loadUnaligned<block_t>(start_) : 0;
    outer_ = 0;
    inner_ = -1;
    position_ = -1;
    value_ = 0;
  }

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
    if (Encoder::forwardQuantum > 0 && n > Encoder::forwardQuantum) {
      // Workaround to avoid 'division by zero' compile-time error.
      constexpr size_t q = Encoder::forwardQuantum ?: 1;

      const size_t steps = position_ / q;
      const size_t dest =
        folly::loadUnaligned<SkipValueType>(
            forwardPointers_ + (steps - 1) * sizeof(SkipValueType));

      /* static */ if (Encoder::version > 0) {
        reposition(dest + steps * q);
      } else {
        reposition(dest);
      }
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
    if (Encoder::skipQuantum > 0 && v >= value_ + Encoder::skipQuantum) {
      // Workaround to avoid 'division by zero' compile-time error.
      constexpr size_t q = Encoder::skipQuantum ?: 1;

      const size_t steps = v / q;
      const size_t dest =
        folly::loadUnaligned<SkipValueType>(
            skipPointers_ + (steps - 1) * sizeof(SkipValueType));

      /* static */ if (Encoder::version > 0) {
        reposition(dest + q * steps);
        position_ = dest - 1;
      } else {
        reposition(dest);
        position_ = dest - q * steps - 1;
      }
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

  ValueType jump(size_t n) {
    if (Encoder::forwardQuantum == 0 || n <= Encoder::forwardQuantum) {
      reset();
    } else {
      position_ = -1;  // Avoid reading the head, skip() will reposition.
    }
    return skip(n);
  }

  ValueType jumpToNext(ValueType v) {
    if (Encoder::skipQuantum == 0 || v < Encoder::skipQuantum) {
      reset();
    } else {
      value_ = 0;  // Avoid reading the head, skipToNext() will reposition.
    }
    return skipToNext(v);
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
  size_t outer_;  // Outer offset: number of consumed bytes in upper.
  size_t inner_;  // Inner offset: (bit) position in current block.
  size_t position_;  // Index of current value (= #reads - 1).
  ValueType value_;
};

}  // namespace detail

template <class Encoder,
          class Instructions = instructions::Default>
class EliasFanoReader : private boost::noncopyable {
 public:
  typedef Encoder EncoderType;
  typedef typename Encoder::ValueType ValueType;

  explicit EliasFanoReader(const EliasFanoCompressedList& list)
    : list_(list),
      lowerMask_((ValueType(1) << list_.numLowerBits) - 1),
      upper_(list_) {
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

  void reset() {
    upper_.reset();
    progress_ = 0;
    value_ = 0;
  }

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

    progress_ += n;
    if (LIKELY(progress_ <= list_.size)) {
      value_ = readLowerPart(progress_ - 1) |
               (upper_.skip(n) << list_.numLowerBits);
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
    } else if (value > lastValue_) {
      progress_ = list_.size;
      value_ = std::numeric_limits<ValueType>::max();
      return false;
    }

    upper_.skipToNext(value >> list_.numLowerBits);
    iterateTo(value);
    return true;
  }

  bool jump(size_t n) {
    if (LIKELY(n - 1 < list_.size)) {  // n > 0 && n <= list_.size
      progress_ = n;
      value_ = readLowerPart(n - 1) | (upper_.jump(n) << list_.numLowerBits);
      return true;
    } else if (n == 0) {
      reset();
      return true;
    }
    progress_ = list_.size;
    value_ = std::numeric_limits<ValueType>::max();
    return false;
  }

  ValueType jumpTo(ValueType value) {
    if (value <= 0) {
      reset();
      return true;
    } else if (value > lastValue_) {
      progress_ = list_.size;
      value_ = std::numeric_limits<ValueType>::max();
      return false;
    }

    upper_.jumpToNext(value >> list_.numLowerBits);
    iterateTo(value);
    return true;
  }

  size_t size() const { return list_.size; }

  size_t position() const { return progress_ - 1; }
  ValueType value() const { return value_; }

 private:
  ValueType readLowerPart(size_t i) const {
    DCHECK_LT(i, list_.size);
    const size_t pos = i * list_.numLowerBits;
    const unsigned char* ptr = list_.lower.data() + (pos / 8);
    const uint64_t ptrv = folly::loadUnaligned<uint64_t>(ptr);
    return lowerMask_ & (ptrv >> (pos % 8));
  }

  void iterateTo(ValueType value) {
    progress_ = upper_.position();
    value_ = readLowerPart(progress_) | (upper_.value() << list_.numLowerBits);
    ++progress_;
    while (value_ < value) {
      value_ = readLowerPart(progress_) | (upper_.next() << list_.numLowerBits);
      ++progress_;
    }
  }

  const EliasFanoCompressedList list_;
  const ValueType lowerMask_;
  detail::UpperBitsReader<Encoder, Instructions> upper_;
  size_t progress_ = 0;
  ValueType value_ = 0;
  ValueType lastValue_;
};

}}  // namespaces

#endif  // FOLLY_EXPERIMENTAL_ELIAS_FANO_CODING_H
