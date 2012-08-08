/*
 * Copyright 2012 Facebook, Inc.
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

#include "folly/Bits.h"
#include "folly/Range.h"

namespace folly {

// As a general rule, bit operations work on unsigned values only;
// right-shift is arithmetic for signed values, and that can lead to
// unpleasant bugs.

namespace detail {

/**
 * Helper class to make Bits<T> (below) work with both aligned values
 * (T, where T is an unsigned integral type) or unaligned values
 * (Unaligned<T>, where T is an unsigned integral type)
 */
template <class T, class Enable=void> struct BitsTraits;

// Partial specialization for Unaligned<T>, where T is unsigned integral
template <class T>
struct BitsTraits<Unaligned<T>, typename std::enable_if<
    (std::is_integral<T>::value && std::is_unsigned<T>::value)>::type> {
  typedef T UnderlyingType;
  static T load(const Unaligned<T>& x) { return x.value; }
  static void store(Unaligned<T>& x, T v) { x.value = v; }
};

// Partial specialization for T, where T is unsigned integral
template <class T>
struct BitsTraits<T, typename std::enable_if<
    (std::is_integral<T>::value && std::is_unsigned<T>::value)>::type> {
  typedef T UnderlyingType;
  static T load(const T& x) { return x; }
  static void store(T& x, T v) { x = v; }
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
  static constexpr size_t bitsPerBlock =
    std::numeric_limits<UnderlyingType>::digits;

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

  static constexpr UnderlyingType one = UnderlyingType(1);
};

template <class T, class Traits>
inline void Bits<T, Traits>::set(T* p, size_t bit) {
  T& block = p[blockIndex(bit)];
  Traits::store(block, Traits::load(block) | (one << bitOffset(bit)));
}

template <class T, class Traits>
inline void Bits<T, Traits>::clear(T* p, size_t bit) {
  T& block = p[blockIndex(bit)];
  Traits::store(block, Traits::load(block) & ~(one << bitOffset(bit)));
}

template <class T, class Traits>
inline bool Bits<T, Traits>::test(const T* p, size_t bit) {
  return Traits::load(p[blockIndex(bit)]) & (one << bitOffset(bit));
}

template <class T, class Traits>
inline void Bits<T, Traits>::set(T* p, size_t bitStart, size_t count,
                                 UnderlyingType value) {
  assert(count <= sizeof(UnderlyingType) * 8);
  assert(count == sizeof(UnderlyingType) ||
         (value & ~((one << count) - 1)) == 0);
  size_t idx = blockIndex(bitStart);
  size_t offset = bitOffset(bitStart);
  if (offset + count <= bitsPerBlock) {
    innerSet(p + idx, offset, count, value);
  } else {
    size_t countInThisBlock = bitsPerBlock - offset;
    size_t countInNextBlock = count - countInThisBlock;
    innerSet(p + idx, offset, countInThisBlock,
             value & ((one << countInThisBlock) - 1));
    innerSet(p + idx + 1, 0, countInNextBlock, value >> countInThisBlock);
  }
}

template <class T, class Traits>
inline auto Bits<T, Traits>::get(const T* p, size_t bitStart, size_t count)
  -> UnderlyingType {
  assert(count <= sizeof(UnderlyingType) * 8);
  size_t idx = blockIndex(bitStart);
  size_t offset = bitOffset(bitStart);
  if (offset + count <= bitsPerBlock) {
    return innerGet(p + idx, offset, count);
  } else {
    size_t countInThisBlock = bitsPerBlock - offset;
    size_t countInNextBlock = count - countInThisBlock;
    UnderlyingType thisBlockValue = innerGet(p + idx, offset, countInThisBlock);
    UnderlyingType nextBlockValue = innerGet(p + idx + 1, 0, countInNextBlock);
    return (nextBlockValue << countInThisBlock) | thisBlockValue;
  }
}

template <class T, class Traits>
inline void Bits<T, Traits>::innerSet(T* p, size_t offset, size_t count,
                                      UnderlyingType value) {
  // Mask out bits and set new value
  UnderlyingType v = Traits::load(*p);
  v &= ~(((one << count) - 1) << offset);
  v |= (value << offset);
  Traits::store(*p, v);
}

template <class T, class Traits>
inline auto Bits<T, Traits>::innerGet(const T* p, size_t offset, size_t count)
  -> UnderlyingType {
  return (Traits::load(*p) >> offset) & ((one << count) - 1);
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

