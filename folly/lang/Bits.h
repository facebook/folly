/*
 * Copyright 2011-present Facebook, Inc.
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
 * @author Tudor Bosman (tudorb@fb.com)
 */

#pragma once

// MSVC does not support intrinsics constexpr
#if defined(_MSC_VER)
#define FOLLY_INTRINSIC_CONSTEXPR const
#else
#define FOLLY_INTRINSIC_CONSTEXPR constexpr
#endif

#include <cassert>
#include <cinttypes>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

#include <folly/Portability.h>
#include <folly/lang/Assume.h>
#include <folly/portability/Builtins.h>

namespace folly {

// Generate overloads for findFirstSet as wrappers around
// appropriate ffs, ffsl, ffsll gcc builtins
template <class T>
inline FOLLY_INTRINSIC_CONSTEXPR
typename std::enable_if<
  (std::is_integral<T>::value &&
   std::is_unsigned<T>::value &&
   sizeof(T) <= sizeof(unsigned int)),
  unsigned int>::type
  findFirstSet(T x) {
  return static_cast<unsigned int>(__builtin_ffs(static_cast<int>(x)));
}

template <class T>
inline FOLLY_INTRINSIC_CONSTEXPR
typename std::enable_if<
  (std::is_integral<T>::value &&
   std::is_unsigned<T>::value &&
   sizeof(T) > sizeof(unsigned int) &&
   sizeof(T) <= sizeof(unsigned long)),
  unsigned int>::type
  findFirstSet(T x) {
  return static_cast<unsigned int>(__builtin_ffsl(static_cast<long>(x)));
}

template <class T>
inline FOLLY_INTRINSIC_CONSTEXPR
typename std::enable_if<
  (std::is_integral<T>::value &&
   std::is_unsigned<T>::value &&
   sizeof(T) > sizeof(unsigned long) &&
   sizeof(T) <= sizeof(unsigned long long)),
  unsigned int>::type
  findFirstSet(T x) {
  return static_cast<unsigned int>(__builtin_ffsll(static_cast<long long>(x)));
}

template <class T>
inline FOLLY_INTRINSIC_CONSTEXPR
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
inline FOLLY_INTRINSIC_CONSTEXPR
typename std::enable_if<
  (std::is_integral<T>::value &&
   std::is_unsigned<T>::value &&
   sizeof(T) <= sizeof(unsigned int)),
  unsigned int>::type
  findLastSet(T x) {
  // If X is a power of two X - Y = ((X - 1) ^ Y) + 1. Doing this transformation
  // allows GCC to remove its own xor that it adds to implement clz using bsr
  return x ? ((8 * sizeof(unsigned int) - 1) ^ __builtin_clz(x)) + 1 : 0;
}

template <class T>
inline FOLLY_INTRINSIC_CONSTEXPR
typename std::enable_if<
  (std::is_integral<T>::value &&
   std::is_unsigned<T>::value &&
   sizeof(T) > sizeof(unsigned int) &&
   sizeof(T) <= sizeof(unsigned long)),
  unsigned int>::type
  findLastSet(T x) {
  return x ? ((8 * sizeof(unsigned long) - 1) ^ __builtin_clzl(x)) + 1 : 0;
}

template <class T>
inline FOLLY_INTRINSIC_CONSTEXPR
typename std::enable_if<
  (std::is_integral<T>::value &&
   std::is_unsigned<T>::value &&
   sizeof(T) > sizeof(unsigned long) &&
   sizeof(T) <= sizeof(unsigned long long)),
  unsigned int>::type
  findLastSet(T x) {
  return x ? ((8 * sizeof(unsigned long long) - 1) ^ __builtin_clzll(x)) + 1
           : 0;
}

template <class T>
inline FOLLY_INTRINSIC_CONSTEXPR
typename std::enable_if<
  (std::is_integral<T>::value &&
   std::is_signed<T>::value),
  unsigned int>::type
  findLastSet(T x) {
  return findLastSet(static_cast<typename std::make_unsigned<T>::type>(x));
}

template <class T>
inline FOLLY_INTRINSIC_CONSTEXPR
typename std::enable_if<
  std::is_integral<T>::value && std::is_unsigned<T>::value,
  T>::type
nextPowTwo(T v) {
  return v ? (T(1) << findLastSet(v - 1)) : 1;
}

template <class T>
inline FOLLY_INTRINSIC_CONSTEXPR typename std::
    enable_if<std::is_integral<T>::value && std::is_unsigned<T>::value, T>::type
    prevPowTwo(T v) {
  return v ? (T(1) << (findLastSet(v) - 1)) : 0;
}

template <class T>
inline constexpr typename std::enable_if<
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
  return size_t(__builtin_popcount(x));
}

template <class T>
inline typename std::enable_if<
  (std::is_integral<T>::value &&
   std::is_unsigned<T>::value &&
   sizeof(T) > sizeof(unsigned int) &&
   sizeof(T) <= sizeof(unsigned long long)),
  size_t>::type
  popcount(T x) {
  return size_t(__builtin_popcountll(x));
}

/**
 * Endianness detection and manipulation primitives.
 */
namespace detail {

template <size_t Size>
struct uint_types_by_size;

#define FB_GEN(sz, fn)                                      \
  static inline uint##sz##_t byteswap_gen(uint##sz##_t v) { \
    return fn(v);                                           \
  }                                                         \
  template <>                                               \
  struct uint_types_by_size<sz / 8> {                       \
    using type = uint##sz##_t;                              \
  };

FB_GEN(8, uint8_t)
#ifdef _MSC_VER
FB_GEN(64, _byteswap_uint64)
FB_GEN(32, _byteswap_ulong)
FB_GEN(16, _byteswap_ushort)
#else
FB_GEN(64, __builtin_bswap64)
FB_GEN(32, __builtin_bswap32)
FB_GEN(16, __builtin_bswap16)
#endif

#undef FB_GEN

template <class T>
struct EndianInt {
  static_assert(
      (std::is_integral<T>::value && !std::is_same<T, bool>::value) ||
          std::is_floating_point<T>::value,
      "template type parameter must be non-bool integral or floating point");
  static T swap(T x) {
    // we implement this with memcpy because that is defined behavior in C++
    // we rely on compilers to optimize away the memcpy calls
    constexpr auto s = sizeof(T);
    using B = typename uint_types_by_size<s>::type;
    B b;
    std::memcpy(&b, &x, s);
    b = byteswap_gen(b);
    std::memcpy(&x, &b, s);
    return x;
  }
  static T big(T x) {
    return kIsLittleEndian ? EndianInt::swap(x) : x;
  }
  static T little(T x) {
    return kIsBigEndian ? EndianInt::swap(x) : x;
  }
};

} // namespace detail

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

  static constexpr Order order = kIsLittleEndian ? Order::LITTLE : Order::BIG;

  template <class T> static T swap(T x) {
    return folly::detail::EndianInt<T>::swap(x);
  }
  template <class T> static T big(T x) {
    return folly::detail::EndianInt<T>::big(x);
  }
  template <class T> static T little(T x) {
    return folly::detail::EndianInt<T>::little(x);
  }

#if !defined(__ANDROID__)
  FB_GEN(64)
  FB_GEN(32)
  FB_GEN(16)
  FB_GEN(8)
#endif
};

#undef FB_GEN
#undef FB_GEN2
#undef FB_GEN1


template <class T, class Enable=void> struct Unaligned;

/**
 * Representation of an unaligned value of a POD type.
 */
FOLLY_PACK_PUSH
template <class T>
struct Unaligned<
    T,
    typename std::enable_if<std::is_pod<T>::value>::type> {
  Unaligned() = default;  // uninitialized
  /* implicit */ Unaligned(T v) : value(v) { }
  T value;
} FOLLY_PACK_ATTR;
FOLLY_PACK_POP

/**
 * Read an unaligned value of type T and return it.
 */
template <class T>
inline T loadUnaligned(const void* p) {
  static_assert(sizeof(Unaligned<T>) == sizeof(T), "Invalid unaligned size");
  static_assert(alignof(Unaligned<T>) == 1, "Invalid alignment");
  if (kHasUnalignedAccess) {
    return static_cast<const Unaligned<T>*>(p)->value;
  } else {
    T value;
    memcpy(&value, p, sizeof(T));
    return value;
  }
}

/**
 * Write an unaligned value of type T.
 */
template <class T>
inline void storeUnaligned(void* p, T value) {
  static_assert(sizeof(Unaligned<T>) == sizeof(T), "Invalid unaligned size");
  static_assert(alignof(Unaligned<T>) == 1, "Invalid alignment");
  if (kHasUnalignedAccess) {
    // Prior to C++14, the spec says that a placement new like this
    // is required to check that p is not nullptr, and to do nothing
    // if p is a nullptr. By assuming it's not a nullptr, we get a
    // nice loud segfault in optimized builds if p is nullptr, rather
    // than just silently doing nothing.
    folly::assume(p != nullptr);
    new (p) Unaligned<T>(value);
  } else {
    memcpy(p, &value, sizeof(T));
  }
}

template <typename T>
T bitReverse(T n) {
  auto m = static_cast<typename std::make_unsigned<T>::type>(n);
  m = ((m & 0xAAAAAAAAAAAAAAAA) >> 1) | ((m & 0x5555555555555555) << 1);
  m = ((m & 0xCCCCCCCCCCCCCCCC) >> 2) | ((m & 0x3333333333333333) << 2);
  m = ((m & 0xF0F0F0F0F0F0F0F0) >> 4) | ((m & 0x0F0F0F0F0F0F0F0F) << 4);
  return static_cast<T>(Endian::swap(m));
}

} // namespace folly
