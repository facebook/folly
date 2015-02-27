/*
 * Copyright 2015 Facebook, Inc.
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

#ifndef FOLLY_EXPERIMENTAL_BITS_H_
#define FOLLY_EXPERIMENTAL_BITS_H_

#include <cstddef>
#include <type_traits>
#include <limits>

#include <folly/Bits.h>
#include <folly/Portability.h>
#include <folly/Range.h>

namespace folly {

template <class T>
struct UnalignedNoASan : public Unaligned<T> { };

// As a general rule, bit operations work on unsigned values only;
// right-shift is arithmetic for signed values, and that can lead to
// unpleasant bugs.

namespace detail {

/**
 * Helper class to make Bits<T> (below) work with both aligned values
 * (T, where T is an unsigned integral type) or unaligned values
 * (Unaligned<T>, where T is an unsigned integral type)
 */
template <class T, class Enable = void>
struct BitsTraits;

// Partial specialization for Unaligned<T>, where T is unsigned integral
// loadRMW is the same as load, but it indicates that it loads for a
// read-modify-write operation (we write back the bits we won't change);
// silence the GCC warning in that case.
template <class T>
struct BitsTraits<Unaligned<T>, typename std::enable_if<
    (std::is_integral<T>::value)>::type> {
  typedef T UnderlyingType;
  static T load(const Unaligned<T>& x) { return x.value; }
  static void store(Unaligned<T>& x, T v) { x.value = v; }
  static T loadRMW(const Unaligned<T>& x) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
// make sure we compile without warning on gcc 4.6 with -Wpragmas
#if __GNUC_PREREQ(4, 7)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
    return x.value;
#pragma GCC diagnostic pop
  }
};

// Special version that allows to disable address sanitizer on demand.
template <class T>
struct BitsTraits<UnalignedNoASan<T>, typename std::enable_if<
    (std::is_integral<T>::value)>::type> {
  typedef T UnderlyingType;
  static T FOLLY_DISABLE_ADDRESS_SANITIZER
  load(const UnalignedNoASan<T>& x) { return x.value; }
  static void FOLLY_DISABLE_ADDRESS_SANITIZER
  store(UnalignedNoASan<T>& x, T v) { x.value = v; }
  static T FOLLY_DISABLE_ADDRESS_SANITIZER
  loadRMW(const UnalignedNoASan<T>& x) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
// make sure we compile without warning on gcc 4.6 with -Wpragmas
#if __GNUC_PREREQ(4, 7)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
    return x.value;
#pragma GCC diagnostic pop
  }
};

// Partial specialization for T, where T is unsigned integral
template <class T>
struct BitsTraits<T, typename std::enable_if<
    (std::is_integral<T>::value)>::type> {
  typedef T UnderlyingType;
  static T load(const T& x) { return x; }
  static void store(T& x, T v) { x = v; }
  static T loadRMW(const T& x) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
#if __GNUC_PREREQ(4, 7)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
    return x;
#pragma GCC diagnostic pop
  }
};

}  // namespace detail

/**
 * Wrapper class with static methods for various bit-level operations,
 * treating an array of T as an array of bits (in little-endian order).
 * (T is either an unsigned integral type or Unaligned<X>, where X is
 * an unsigned integral type)
 */
template <class T, class Traits=detail::BitsTraits<T>>
struct Bits {
  typedef typename Traits::UnderlyingType UnderlyingType;
  typedef T type;
  static_assert(sizeof(T) == sizeof(UnderlyingType), "Size mismatch");

  /**
   * Number of bits in a block.
   */
  static constexpr size_t bitsPerBlock = std::numeric_limits<
      typename std::make_unsigned<UnderlyingType>::type>::digits;

  /**
   * Byte index of the given bit.
   */
  static constexpr size_t blockIndex(size_t bit) {
    return bit / bitsPerBlock;
  }

  /**
   * Offset in block of the given bit.
   */
  static constexpr size_t bitOffset(size_t bit) {
    return bit % bitsPerBlock;
  }

  /**
   * Number of blocks used by the given number of bits.
   */
  static constexpr size_t blockCount(size_t nbits) {
    return nbits / bitsPerBlock + (nbits % bitsPerBlock != 0);
  }

  /**
   * Set the given bit.
   */
  static void set(T* p, size_t bit);

  /**
   * Clear the given bit.
   */
  static void clear(T* p, size_t bit);

  /**
   * Test the given bit.
   */
  static bool test(const T* p, size_t bit);

  /**
   * Set count contiguous bits starting at bitStart to the values
   * from the least significant count bits of value; little endian.
   * (value & 1 becomes the bit at bitStart, etc)
   * Precondition: count <= sizeof(T) * 8
   * Precondition: value can fit in 'count' bits
   */
  static void set(T* p, size_t bitStart, size_t count, UnderlyingType value);

  /**
   * Get count contiguous bits starting at bitStart.
   * Precondition: count <= sizeof(T) * 8
   */
  static UnderlyingType get(const T* p, size_t bitStart, size_t count);

  /**
   * Count the number of bits set in a range of blocks.
   */
  static size_t count(const T* begin, const T* end);

 private:
  // Same as set, assumes all bits are in the same block.
  // (bitStart < sizeof(T) * 8, bitStart + count <= sizeof(T) * 8)
  static void innerSet(T* p, size_t bitStart, size_t count,
                       UnderlyingType value);

  // Same as get, assumes all bits are in the same block.
  // (bitStart < sizeof(T) * 8, bitStart + count <= sizeof(T) * 8)
  static UnderlyingType innerGet(const T* p, size_t bitStart, size_t count);

  static constexpr UnderlyingType zero = UnderlyingType(0);
  static constexpr UnderlyingType one = UnderlyingType(1);

  static constexpr UnderlyingType ones(size_t count) {
    return count < bitsPerBlock ? (one << count) - 1 : ~zero;
  }
};

// gcc 4.8 needs more -Wmaybe-uninitialized tickling, as it propagates the
// taint upstream from loadRMW

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
#if __GNUC_PREREQ(4, 7)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

template <class T, class Traits>
inline void Bits<T, Traits>::set(T* p, size_t bit) {
  T& block = p[blockIndex(bit)];
  Traits::store(block, Traits::loadRMW(block) | (one << bitOffset(bit)));
}

template <class T, class Traits>
inline void Bits<T, Traits>::clear(T* p, size_t bit) {
  T& block = p[blockIndex(bit)];
  Traits::store(block, Traits::loadRMW(block) & ~(one << bitOffset(bit)));
}

template <class T, class Traits>
inline void Bits<T, Traits>::set(T* p, size_t bitStart, size_t count,
                                 UnderlyingType value) {
  assert(count <= sizeof(UnderlyingType) * 8);
  size_t cut = bitsPerBlock - count;
  assert(value == (value << cut >> cut));
  size_t idx = blockIndex(bitStart);
  size_t offset = bitOffset(bitStart);
  if (std::is_signed<UnderlyingType>::value) {
    value &= ones(count);
  }
  if (offset + count <= bitsPerBlock) {
    innerSet(p + idx, offset, count, value);
  } else {
    size_t countInThisBlock = bitsPerBlock - offset;
    size_t countInNextBlock = count - countInThisBlock;

    UnderlyingType thisBlock = value & ((one << countInThisBlock) - 1);
    UnderlyingType nextBlock = value >> countInThisBlock;
    if (std::is_signed<UnderlyingType>::value) {
      nextBlock &= ones(countInNextBlock);
    }
    innerSet(p + idx, offset, countInThisBlock, thisBlock);
    innerSet(p + idx + 1, 0, countInNextBlock, nextBlock);
  }
}

template <class T, class Traits>
inline void Bits<T, Traits>::innerSet(T* p, size_t offset, size_t count,
                                      UnderlyingType value) {
  // Mask out bits and set new value
  UnderlyingType v = Traits::loadRMW(*p);
  v &= ~(ones(count) << offset);
  v |= (value << offset);
  Traits::store(*p, v);
}

#pragma GCC diagnostic pop

template <class T, class Traits>
inline bool Bits<T, Traits>::test(const T* p, size_t bit) {
  return Traits::load(p[blockIndex(bit)]) & (one << bitOffset(bit));
}

template <class T, class Traits>
inline auto Bits<T, Traits>::get(const T* p, size_t bitStart, size_t count)
  -> UnderlyingType {
  assert(count <= sizeof(UnderlyingType) * 8);
  size_t idx = blockIndex(bitStart);
  size_t offset = bitOffset(bitStart);
  UnderlyingType ret;
  if (offset + count <= bitsPerBlock) {
    ret = innerGet(p + idx, offset, count);
  } else {
    size_t countInThisBlock = bitsPerBlock - offset;
    size_t countInNextBlock = count - countInThisBlock;
    UnderlyingType thisBlockValue = innerGet(p + idx, offset, countInThisBlock);
    UnderlyingType nextBlockValue = innerGet(p + idx + 1, 0, countInNextBlock);
    ret = (nextBlockValue << countInThisBlock) | thisBlockValue;
  }
  if (std::is_signed<UnderlyingType>::value) {
    size_t emptyBits = bitsPerBlock - count;
    ret <<= emptyBits;
    ret >>= emptyBits;
  }
  return ret;
}

template <class T, class Traits>
inline auto Bits<T, Traits>::innerGet(const T* p, size_t offset, size_t count)
    -> UnderlyingType {
  return (Traits::load(*p) >> offset) & ones(count);
}

template <class T, class Traits>
inline size_t Bits<T, Traits>::count(const T* begin, const T* end) {
  size_t n = 0;
  for (; begin != end; ++begin) {
    n += popcount(Traits::load(*begin));
  }
  return n;
}

}  // namespace folly

#endif /* FOLLY_EXPERIMENTAL_BITS_H_ */
