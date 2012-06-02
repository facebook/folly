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

#include "folly/Range.h"

namespace folly {

/**
 * Population count (number of bits set), using __builtin_popcount or
 * __builtin_popcountll, depending on size.
 */
template <class T>
inline typename std::enable_if<
  (std::is_integral<T>::value &&
   std::is_unsigned<T>::value &&
   sizeof(T) <= sizeof(unsigned int)),
  size_t>::type
  popcount(T x) {
  return __builtin_popcount(x);
}

template <class T>
inline typename std::enable_if<
  (std::is_integral<T>::value &&
   std::is_unsigned<T>::value &&
   sizeof(T) > sizeof(unsigned int) &&
   sizeof(T) <= sizeof(unsigned long long)),
  size_t>::type
  popcount(T x) {
  return __builtin_popcountll(x);
}

template <class T>
struct Bits {
  static_assert(std::is_integral<T>::value &&
                std::is_unsigned<T>::value,
                "Unsigned integral type required");

  typedef T type;
  static constexpr size_t bitsPerBlock = std::numeric_limits<T>::digits;

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
   * Count the number of bits set in a range of blocks.
   */
  static size_t count(const T* begin, const T* end);

 private:
  static constexpr T one = T(1);
};

template <class T>
inline void Bits<T>::set(T* p, size_t bit) {
  p[blockIndex(bit)] |= (one << bitOffset(bit));
}

template <class T>
inline void Bits<T>::clear(T* p, size_t bit) {
  p[blockIndex(bit)] &= ~(one << bitOffset(bit));
}

template <class T>
inline bool Bits<T>::test(const T* p, size_t bit) {
  return p[blockIndex(bit)] & (one << bitOffset(bit));
}

template <class T>
inline size_t Bits<T>::count(const T* begin, const T* end) {
  size_t n = 0;
  for (; begin != end; ++begin) {
    n += popcount(*begin);
  }
  return n;
}

}  // namespace folly

#endif /* FOLLY_EXPERIMENTAL_BITS_H_ */

