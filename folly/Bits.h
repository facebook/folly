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
 * Various low-level, bit-manipulation routines.
 *
 * findFirstSet(x)  [constexpr]
 *    find first (least significant) bit set in a value of an integral type,
 *    1-based (like ffs()).  0 = no bits are set (x == 0)
 *
 * findLastSet(x)  [constexpr]
 *    find last (most significant) bit set in a value of an integral type,
 *    1-based.  0 = no bits are set (x == 0)
 *    for x != 0, findLastSet(x) == 1 + floor(log2(x))
 *
 * nextPowTwo(x)  [constexpr]
 *    Finds the next power of two >= x.
 *
 * isPowTwo(x)  [constexpr]
 *    return true iff x is a power of two
 *
 * popcount(x)
 *    return the number of 1 bits in x
 *
 * Endian
 *    convert between native, big, and little endian representation
 *    Endian::big(x)      big <-> native
 *    Endian::little(x)   little <-> native
 *    Endian::swap(x)     big <-> little
 *
 * BitIterator
 *    Wrapper around an iterator over an integral type that iterates
 *    over its underlying bits in MSb to LSb order
 *
 * findFirstSet(BitIterator begin, BitIterator end)
 *    return a BitIterator pointing to the first 1 bit in [begin, end), or
 *    end if all bits in [begin, end) are 0
 *
 * @author Tudor Bosman (tudorb@fb.com)
 */

#ifndef FOLLY_BITS_H_
#define FOLLY_BITS_H_

#include "folly/Portability.h"

#ifndef __GNUC__
#error GCC required
#endif

#ifndef FOLLY_NO_CONFIG
#include "folly/folly-config.h"
#endif

#include "folly/detail/BitsDetail.h"
#include "folly/detail/BitIteratorDetail.h"
#include "folly/Likely.h"

#if FOLLY_HAVE_BYTESWAP_H
# include <byteswap.h>
#endif

#include <cassert>
#include <cinttypes>
#include <iterator>
#include <limits>
#include <type_traits>
#include <boost/iterator/iterator_adaptor.hpp>
#include <stdint.h>

namespace folly {

// Generate overloads for findFirstSet as wrappers around
// appropriate ffs, ffsl, ffsll gcc builtins
template <class T>
inline constexpr
typename std::enable_if<
  (std::is_integral<T>::value &&
   std::is_unsigned<T>::value &&
   sizeof(T) <= sizeof(unsigned int)),
  unsigned int>::type
  findFirstSet(T x) {
  return __builtin_ffs(x);
}

template <class T>
inline constexpr
typename std::enable_if<
  (std::is_integral<T>::value &&
   std::is_unsigned<T>::value &&
   sizeof(T) > sizeof(unsigned int) &&
   sizeof(T) <= sizeof(unsigned long)),
  unsigned int>::type
  findFirstSet(T x) {
  return __builtin_ffsl(x);
}

template <class T>
inline constexpr
typename std::enable_if<
  (std::is_integral<T>::value &&
   std::is_unsigned<T>::value &&
   sizeof(T) > sizeof(unsigned long) &&
   sizeof(T) <= sizeof(unsigned long long)),
  unsigned int>::type
  findFirstSet(T x) {
  return __builtin_ffsll(x);
}

template <class T>
inline constexpr
typename std::enable_if<
  (std::is_integral<T>::value && std::is_signed<T>::value),
  unsigned int>::type
  findFirstSet(T x) {
  // Note that conversion from a signed type to the corresponding unsigned
  // type is technically implementation-defined, but will likely work
  // on any impementation that uses two's complement.
  return findFirstSet(static_cast<typename std::make_unsigned<T>::type>(x));
}

// findLastSet: return the 1-based index of the highest bit set
// for x > 0, findLastSet(x) == 1 + floor(log2(x))
template <class T>
inline constexpr
typename std::enable_if<
  (std::is_integral<T>::value &&
   std::is_unsigned<T>::value &&
   sizeof(T) <= sizeof(unsigned int)),
  unsigned int>::type
  findLastSet(T x) {
  return x ? 8 * sizeof(unsigned int) - __builtin_clz(x) : 0;
}

template <class T>
inline constexpr
typename std::enable_if<
  (std::is_integral<T>::value &&
   std::is_unsigned<T>::value &&
   sizeof(T) > sizeof(unsigned int) &&
   sizeof(T) <= sizeof(unsigned long)),
  unsigned int>::type
  findLastSet(T x) {
  return x ? 8 * sizeof(unsigned long) - __builtin_clzl(x) : 0;
}

template <class T>
inline constexpr
typename std::enable_if<
  (std::is_integral<T>::value &&
   std::is_unsigned<T>::value &&
   sizeof(T) > sizeof(unsigned long) &&
   sizeof(T) <= sizeof(unsigned long long)),
  unsigned int>::type
  findLastSet(T x) {
  return x ? 8 * sizeof(unsigned long long) - __builtin_clzll(x) : 0;
}

template <class T>
inline constexpr
typename std::enable_if<
  (std::is_integral<T>::value &&
   std::is_signed<T>::value),
  unsigned int>::type
  findLastSet(T x) {
  return findLastSet(static_cast<typename std::make_unsigned<T>::type>(x));
}

template <class T>
inline constexpr
typename std::enable_if<
  std::is_integral<T>::value && std::is_unsigned<T>::value,
  T>::type
nextPowTwo(T v) {
  return v ? (1ul << findLastSet(v - 1)) : 1;
}

template <class T>
inline constexpr
typename std::enable_if<
  std::is_integral<T>::value && std::is_unsigned<T>::value,
  bool>::type
isPowTwo(T v) {
  return (v != 0) && !(v & (v - 1));
}

/**
 * Population count
 */
template <class T>
inline typename std::enable_if<
  (std::is_integral<T>::value &&
   std::is_unsigned<T>::value &&
   sizeof(T) <= sizeof(unsigned int)),
  size_t>::type
  popcount(T x) {
  return detail::popcount(x);
}

template <class T>
inline typename std::enable_if<
  (std::is_integral<T>::value &&
   std::is_unsigned<T>::value &&
   sizeof(T) > sizeof(unsigned int) &&
   sizeof(T) <= sizeof(unsigned long long)),
  size_t>::type
  popcount(T x) {
  return detail::popcountll(x);
}

/**
 * Endianness detection and manipulation primitives.
 */
namespace detail {

template <class T>
struct EndianIntBase {
 public:
  static T swap(T x);
};

/**
 * If we have the bswap_16 macro from byteswap.h, use it; otherwise, provide our
 * own definition.
 */
#ifdef bswap_16
# define our_bswap16 bswap_16
#else

template<class Int16>
inline constexpr typename std::enable_if<
  sizeof(Int16) == 2,
  Int16>::type
our_bswap16(Int16 x) {
  return ((x >> 8) & 0xff) | ((x & 0xff) << 8);
}
#endif

#define FB_GEN(t, fn) \
template<> inline t EndianIntBase<t>::swap(t x) { return fn(x); }

// fn(x) expands to (x) if the second argument is empty, which is exactly
// what we want for [u]int8_t. Also, gcc 4.7 on Intel doesn't have
// __builtin_bswap16 for some reason, so we have to provide our own.
FB_GEN( int8_t,)
FB_GEN(uint8_t,)
FB_GEN( int64_t, __builtin_bswap64)
FB_GEN(uint64_t, __builtin_bswap64)
FB_GEN( int32_t, __builtin_bswap32)
FB_GEN(uint32_t, __builtin_bswap32)
FB_GEN( int16_t, our_bswap16)
FB_GEN(uint16_t, our_bswap16)

#undef FB_GEN

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__

template <class T>
struct EndianInt : public detail::EndianIntBase<T> {
 public:
  static T big(T x) { return EndianInt::swap(x); }
  static T little(T x) { return x; }
};

#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__

template <class T>
struct EndianInt : public detail::EndianIntBase<T> {
 public:
  static T big(T x) { return x; }
  static T little(T x) { return EndianInt::swap(x); }
};

#else
# error Your machine uses a weird endianness!
#endif  /* __BYTE_ORDER__ */

}  // namespace detail

// big* convert between native and big-endian representations
// little* convert between native and little-endian representations
// swap* convert between big-endian and little-endian representations
//
// ntohs, htons == big16
// ntohl, htonl == big32
#define FB_GEN1(fn, t, sz) \
  static t fn##sz(t x) { return fn<t>(x); } \

#define FB_GEN2(t, sz) \
  FB_GEN1(swap, t, sz) \
  FB_GEN1(big, t, sz) \
  FB_GEN1(little, t, sz)

#define FB_GEN(sz) \
  FB_GEN2(uint##sz##_t, sz) \
  FB_GEN2(int##sz##_t, sz)

class Endian {
 public:
  enum class Order : uint8_t {
    LITTLE,
    BIG
  };

  static constexpr Order order =
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    Order::LITTLE;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    Order::BIG;
#else
# error Your machine uses a weird endianness!
#endif  /* __BYTE_ORDER__ */

  template <class T> static T swap(T x) {
    return detail::EndianInt<T>::swap(x);
  }
  template <class T> static T big(T x) {
    return detail::EndianInt<T>::big(x);
  }
  template <class T> static T little(T x) {
    return detail::EndianInt<T>::little(x);
  }

  FB_GEN(64)
  FB_GEN(32)
  FB_GEN(16)
  FB_GEN(8)
};

#undef FB_GEN
#undef FB_GEN2
#undef FB_GEN1

/**
 * Fast bit iteration facility.
 */


template <class BaseIter> class BitIterator;
template <class BaseIter>
BitIterator<BaseIter> findFirstSet(BitIterator<BaseIter>,
                                   BitIterator<BaseIter>);
/**
 * Wrapper around an iterator over an integer type that iterates
 * over its underlying bits in LSb to MSb order.
 *
 * BitIterator models the same iterator concepts as the base iterator.
 */
template <class BaseIter>
class BitIterator
  : public bititerator_detail::BitIteratorBase<BaseIter>::type {
 public:
  /**
   * Return the number of bits in an element of the underlying iterator.
   */
  static size_t bitsPerBlock() {
    return std::numeric_limits<
      typename std::make_unsigned<
        typename std::iterator_traits<BaseIter>::value_type
      >::type
    >::digits;
  }

  /**
   * Construct a BitIterator that points at a given bit offset (default 0)
   * in iter.
   */
  explicit BitIterator(const BaseIter& iter, size_t bitOffset=0)
    : bititerator_detail::BitIteratorBase<BaseIter>::type(iter),
      bitOffset_(bitOffset) {
    assert(bitOffset_ < bitsPerBlock());
  }

  size_t bitOffset() const {
    return bitOffset_;
  }

  void advanceToNextBlock() {
    bitOffset_ = 0;
    ++this->base_reference();
  }

  BitIterator& operator=(const BaseIter& other) {
    this->~BitIterator();
    new (this) BitIterator(other);
    return *this;
  }

 private:
  friend class boost::iterator_core_access;
  friend BitIterator findFirstSet<>(BitIterator, BitIterator);

  typedef bititerator_detail::BitReference<
      typename std::iterator_traits<BaseIter>::reference,
      typename std::iterator_traits<BaseIter>::value_type
    > BitRef;

  void advanceInBlock(size_t n) {
    bitOffset_ += n;
    assert(bitOffset_ < bitsPerBlock());
  }

  BitRef dereference() const {
    return BitRef(*this->base_reference(), bitOffset_);
  }

  void advance(ssize_t n) {
    size_t bpb = bitsPerBlock();
    ssize_t blocks = n / bpb;
    bitOffset_ += n % bpb;
    if (bitOffset_ >= bpb) {
      bitOffset_ -= bpb;
      ++blocks;
    }
    this->base_reference() += blocks;
  }

  void increment() {
    if (++bitOffset_ == bitsPerBlock()) {
      advanceToNextBlock();
    }
  }

  void decrement() {
    if (bitOffset_-- == 0) {
      bitOffset_ = bitsPerBlock() - 1;
      --this->base_reference();
    }
  }

  bool equal(const BitIterator& other) const {
    return (bitOffset_ == other.bitOffset_ &&
            this->base_reference() == other.base_reference());
  }

  ssize_t distance_to(const BitIterator& other) const {
    return
      (other.base_reference() - this->base_reference()) * bitsPerBlock() +
      (other.bitOffset_ - bitOffset_);
  }

  ssize_t bitOffset_;
};

/**
 * Helper function, so you can write
 * auto bi = makeBitIterator(container.begin());
 */
template <class BaseIter>
BitIterator<BaseIter> makeBitIterator(const BaseIter& iter) {
  return BitIterator<BaseIter>(iter);
}


/**
 * Find first bit set in a range of bit iterators.
 * 4.5x faster than the obvious std::find(begin, end, true);
 */
template <class BaseIter>
BitIterator<BaseIter> findFirstSet(BitIterator<BaseIter> begin,
                                   BitIterator<BaseIter> end) {
  // shortcut to avoid ugly static_cast<>
  static const typename BaseIter::value_type one = 1;

  while (begin.base() != end.base()) {
    typename BaseIter::value_type v = *begin.base();
    // mask out the bits that don't matter (< begin.bitOffset)
    v &= ~((one << begin.bitOffset()) - 1);
    size_t firstSet = findFirstSet(v);
    if (firstSet) {
      --firstSet;  // now it's 0-based
      assert(firstSet >= begin.bitOffset());
      begin.advanceInBlock(firstSet - begin.bitOffset());
      return begin;
    }
    begin.advanceToNextBlock();
  }

  // now begin points to the same block as end
  if (end.bitOffset() != 0) {  // assume end is dereferenceable
    typename BaseIter::value_type v = *begin.base();
    // mask out the bits that don't matter (< begin.bitOffset)
    v &= ~((one << begin.bitOffset()) - 1);
    // mask out the bits that don't matter (>= end.bitOffset)
    v &= (one << end.bitOffset()) - 1;
    size_t firstSet = findFirstSet(v);
    if (firstSet) {
      --firstSet;  // now it's 0-based
      assert(firstSet >= begin.bitOffset());
      begin.advanceInBlock(firstSet - begin.bitOffset());
      return begin;
    }
  }

  return end;
}


template <class T, class Enable=void> struct Unaligned;

/**
 * Representation of an unaligned value of a POD type.
 */
template <class T>
struct Unaligned<
    T,
    typename std::enable_if<std::is_pod<T>::value>::type> {
  Unaligned() = default;  // uninitialized
  /* implicit */ Unaligned(T v) : value(v) { }
  T value;
} __attribute__((packed));

/**
 * Read an unaligned value of type T and return it.
 */
template <class T>
inline T loadUnaligned(const void* p) {
  static_assert(sizeof(Unaligned<T>) == sizeof(T), "Invalid unaligned size");
  static_assert(alignof(Unaligned<T>) == 1, "Invalid alignment");
  return static_cast<const Unaligned<T>*>(p)->value;
}

/**
 * Write an unaligned value of type T.
 */
template <class T>
inline void storeUnaligned(void* p, T value) {
  static_assert(sizeof(Unaligned<T>) == sizeof(T), "Invalid unaligned size");
  static_assert(alignof(Unaligned<T>) == 1, "Invalid alignment");
  new (p) Unaligned<T>(value);
}

}  // namespace folly

#endif /* FOLLY_BITS_H_ */

