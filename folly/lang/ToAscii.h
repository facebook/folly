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

#include <cstring>

#include <folly/ConstexprMath.h>
#include <folly/Likely.h>
#include <folly/Portability.h>
#include <folly/Utility.h>
#include <folly/lang/Align.h>
#include <folly/lang/CArray.h>
#include <folly/portability/Builtins.h>

namespace folly {

//  to_ascii_alphabet
//
//  Used implicity by to_ascii_lower and to_ascii_upper below.
//
//  This alphabet translates digits to 0-9,a-z or 0-9,A-Z. The largest supported
//  base is 36; operator() presumes an argument less than that.
//
//  Alternative alphabets may be used with to_ascii_with provided they match
//  the constructibility/destructibility and the interface of this one.
template <bool Upper>
struct to_ascii_alphabet {
  //  operator()
  //
  //  Translates a single digit to 0-9,a-z or 0-9,A-Z.
  //
  //  async-signal-safe
  constexpr char operator()(uint8_t b) const {
    return b < 10 ? '0' + b : (Upper ? 'A' : 'a') + (b - 10);
  }
};
using to_ascii_alphabet_lower = to_ascii_alphabet<false>;
using to_ascii_alphabet_upper = to_ascii_alphabet<true>;

namespace detail {

template <uint64_t Base, typename Alphabet>
struct to_ascii_array {
  using data_type_ = c_array<uint8_t, Base>;
  static constexpr data_type_ data_() {
    data_type_ result{};
    Alphabet alpha;
    for (size_t i = 0; i < Base; ++i) {
      result.data[i] = alpha(static_cast<uint8_t>(i));
    }
    return result;
  }
  // @lint-ignore CLANGTIDY
  static data_type_ const data;
  constexpr char operator()(uint8_t index) const { // also an alphabet
    return data.data[index];
  }
};
template <uint64_t Base, typename Alphabet>
alignas(kIsMobile ? sizeof(size_t) : hardware_constructive_interference_size)
    typename to_ascii_array<Base, Alphabet>::data_type_ const
    to_ascii_array<Base, Alphabet>::data =
        to_ascii_array<Base, Alphabet>::data_();

extern template to_ascii_array<8, to_ascii_alphabet_lower>::data_type_ const
    to_ascii_array<8, to_ascii_alphabet_lower>::data;
extern template to_ascii_array<10, to_ascii_alphabet_lower>::data_type_ const
    to_ascii_array<10, to_ascii_alphabet_lower>::data;
extern template to_ascii_array<16, to_ascii_alphabet_lower>::data_type_ const
    to_ascii_array<16, to_ascii_alphabet_lower>::data;
extern template to_ascii_array<8, to_ascii_alphabet_upper>::data_type_ const
    to_ascii_array<8, to_ascii_alphabet_upper>::data;
extern template to_ascii_array<10, to_ascii_alphabet_upper>::data_type_ const
    to_ascii_array<10, to_ascii_alphabet_upper>::data;
extern template to_ascii_array<16, to_ascii_alphabet_upper>::data_type_ const
    to_ascii_array<16, to_ascii_alphabet_upper>::data;

template <uint64_t Base, typename Alphabet>
struct to_ascii_table {
  using data_type_ = c_array<uint16_t, Base * Base>;
  static constexpr data_type_ data_() {
    data_type_ result{};
    Alphabet alpha;
    for (size_t i = 0; i < Base * Base; ++i) {
      result.data[i] = //
          (alpha(uint8_t(i / Base)) << (kIsLittleEndian ? 0 : 8)) |
          (alpha(uint8_t(i % Base)) << (kIsLittleEndian ? 8 : 0));
    }
    return result;
  }
  // @lint-ignore CLANGTIDY
  static data_type_ const data;
};
template <uint64_t Base, typename Alphabet>
alignas(hardware_constructive_interference_size)
    typename to_ascii_table<Base, Alphabet>::data_type_ const
    to_ascii_table<Base, Alphabet>::data =
        to_ascii_table<Base, Alphabet>::data_();

extern template to_ascii_table<8, to_ascii_alphabet_lower>::data_type_ const
    to_ascii_table<8, to_ascii_alphabet_lower>::data;
extern template to_ascii_table<10, to_ascii_alphabet_lower>::data_type_ const
    to_ascii_table<10, to_ascii_alphabet_lower>::data;
extern template to_ascii_table<16, to_ascii_alphabet_lower>::data_type_ const
    to_ascii_table<16, to_ascii_alphabet_lower>::data;
extern template to_ascii_table<8, to_ascii_alphabet_upper>::data_type_ const
    to_ascii_table<8, to_ascii_alphabet_upper>::data;
extern template to_ascii_table<10, to_ascii_alphabet_upper>::data_type_ const
    to_ascii_table<10, to_ascii_alphabet_upper>::data;
extern template to_ascii_table<16, to_ascii_alphabet_upper>::data_type_ const
    to_ascii_table<16, to_ascii_alphabet_upper>::data;

template <uint64_t Base, typename Int>
struct to_ascii_powers {
  static constexpr size_t size_(Int v) {
    return 1 + (v < Base ? 0 : size_(v / Base));
  }
  static constexpr size_t const size = size_(~Int(0));
  using data_type_ = c_array<Int, size>;
  static constexpr data_type_ data_() {
    data_type_ result{};
    for (size_t i = 0; i < size; ++i) {
      result.data[i] = constexpr_pow(Base, i);
    }
    return result;
  }
  // @lint-ignore CLANGTIDY
  static data_type_ const data;
};
#if FOLLY_CPLUSPLUS < 201703L
template <uint64_t Base, typename Int>
constexpr size_t const to_ascii_powers<Base, Int>::size;
#endif
template <uint64_t Base, typename Int>
alignas(hardware_constructive_interference_size)
    typename to_ascii_powers<Base, Int>::data_type_ const
    to_ascii_powers<Base, Int>::data = to_ascii_powers<Base, Int>::data_();

extern template to_ascii_powers<8, uint64_t>::data_type_ const
    to_ascii_powers<8, uint64_t>::data;
extern template to_ascii_powers<10, uint64_t>::data_type_ const
    to_ascii_powers<10, uint64_t>::data;
extern template to_ascii_powers<16, uint64_t>::data_type_ const
    to_ascii_powers<16, uint64_t>::data;

template <uint64_t Base>
FOLLY_ALWAYS_INLINE size_t to_ascii_size_imuls(uint64_t v) {
  using powers = to_ascii_powers<Base, uint64_t>;
  uint64_t p = 1;
  for (size_t i = 0u; i < powers::size; ++i, p *= Base) {
    if (FOLLY_UNLIKELY(v < p)) {
      return i + size_t(i == 0);
    }
  }
  return powers::size;
}

template <uint64_t Base>
FOLLY_ALWAYS_INLINE size_t to_ascii_size_idivs(uint64_t v) {
  size_t i = 1;
  while (v >= Base) {
    i += 1;
    v /= Base;
  }
  return i;
}

template <uint64_t Base>
FOLLY_ALWAYS_INLINE size_t to_ascii_size_array(uint64_t v) {
  using powers = to_ascii_powers<Base, uint64_t>;
  for (size_t i = 0u; i < powers::size; ++i) {
    if (FOLLY_UNLIKELY(v < powers::data.data[i])) {
      return i + size_t(i == 0);
    }
  }
  return powers::size;
}

//  For some architectures, we can get a little help from clzll, the "count
//  leading zeros" builtin, which is backed by a single performant instruction.
//
//  Note that the compiler implements __builtin_clzll on all architectures, but
//  only emits a single clzll instruction when the architecture has one.
//
//  This implementation may be faster than the basic ones in the general case
//  because the time taken to compute this one is constant for non-zero v,
//  whereas the basic ones take time proportional to log<2>(v). Whether this one
//  is actually faster depends on the emitted code for this implementation and
//  on whether the loops in the basic implementations are unrolled.
template <uint64_t Base>
FOLLY_ALWAYS_INLINE size_t to_ascii_size_clzll(uint64_t v) {
  using powers = to_ascii_powers<Base, uint64_t>;

  //  clzll is undefined for 0; must special case this
  if (FOLLY_UNLIKELY(!v)) {
    return 1;
  }

  //  log2 is approx log<2>(v)
  size_t const vlog2 = 64 - static_cast<size_t>(__builtin_clzll(v));

  //  work around msvc warning C4127 (conditional expression is constant)
  bool false_ = false;

  //  handle directly when Base is power-of-two
  if (false_ || !(Base & (Base - 1))) {
    constexpr auto const blog2 = constexpr_log2(Base);
    return vlog2 / blog2 + size_t(vlog2 % blog2 != 0);
  }

  //  blog2r is approx 1 / log<2>(Base), used in log change-of-base just below
  constexpr auto const blog2m = constexpr_log2(constexpr_pow(Base, 8));
  constexpr auto const blog2r = 8. / double(blog2m);

  //  vlogb is approx log<Base>(v) = log<2>(v) / log<2>(Base)
  auto const vlogb = vlog2 * size_t(blog2r * 256) / 256;

  //  return vlogb, adjusted if necessary
  return vlogb + size_t(vlogb < powers::size && v >= powers::data.data[vlogb]);
}

template <uint64_t Base>
FOLLY_ALWAYS_INLINE size_t to_ascii_size_route(uint64_t v) {
  return kIsArchAmd64 && !(Base & (Base - 1)) //
      ? to_ascii_size_clzll<Base>(v)
      : to_ascii_size_array<Base>(v);
}

//  The straightforward implementation, assuming the size known in advance.
//
//  The straightforward implementation without the size known in advance would
//  entail emitting the bytes backward and then reversing them at the end, once
//  the size is known.
template <uint64_t Base, typename Alphabet>
FOLLY_ALWAYS_INLINE void to_ascii_with_basic(
    char* out, size_t size, uint64_t v) {
  Alphabet const xlate;
  for (auto pos = size - 1; pos; --pos) {
    //  keep /, % together so a peephole optimization computes them together
    auto const q = v / Base;
    auto const r = v % Base;
    out[pos] = xlate(uint8_t(r));
    v = q;
  }
  out[0] = xlate(uint8_t(v));
}

//  A variant of the straightforward implementation, but using a lookup table.
template <uint64_t Base, typename Alphabet>
FOLLY_ALWAYS_INLINE void to_ascii_with_array(
    char* out, size_t size, uint64_t v) {
  using array = to_ascii_array<Base, Alphabet>; // also an alphabet
  to_ascii_with_basic<Base, array>(out, size, v);
}

//  A trickier implementation which performs half as many divides as the other,
//  more straightforward, implementation. On modern hardware, the divides are
//  the bottleneck (even when the compiler emits a complicated sequence of add,
//  sub, and mul instructions with special constants to simulate a divide by a
//  fixed denominator).
//
//  The downside of this implementation is that the emitted code is larger,
//  especially when the divide is simulated, which affects inlining decisions.
template <uint64_t Base, typename Alphabet>
FOLLY_ALWAYS_INLINE void to_ascii_with_table(
    char* out, size_t size, uint64_t v) {
  using table = to_ascii_table<Base, Alphabet>;
  auto pos = size;
  while (FOLLY_UNLIKELY(pos > 2)) {
    pos -= 2;
    //  keep /, % together so a peephole optimization computes them together
    auto const q = v / (Base * Base);
    auto const r = v % (Base * Base);
    auto const val = table::data.data[size_t(r)];
    std::memcpy(out + pos, &val, 2);
    v = q;
  }

  auto const val = table::data.data[size_t(v)];
  if (FOLLY_UNLIKELY(pos == 2)) {
    std::memcpy(out, &val, 2);
  } else {
    *out = val >> (kIsLittleEndian ? 8 : 0);
  }
}
template <uint64_t Base, typename Alphabet>
FOLLY_ALWAYS_INLINE size_t to_ascii_with_table(char* out, uint64_t v) {
  auto const size = to_ascii_size_route<Base>(v);
  to_ascii_with_table<Base, Alphabet>(out, size, v);
  return size;
}

// Assumes that size >= number of digits in v. If >, the result is left-padded
// with 0s.
template <uint64_t Base, typename Alphabet>
FOLLY_ALWAYS_INLINE void to_ascii_with_route(
    char* outb, size_t size, uint64_t v) {
  kIsMobile //
      ? to_ascii_with_array<Base, Alphabet>(outb, size, v)
      : to_ascii_with_table<Base, Alphabet>(outb, size, v);
}
template <uint64_t Base, typename Alphabet>
FOLLY_ALWAYS_INLINE size_t
to_ascii_with_route(char* outb, char const* oute, uint64_t v) {
  auto const size = to_ascii_size_route<Base>(v);
  if (FOLLY_UNLIKELY(oute < outb || size_t(oute - outb) < size)) {
    return 0;
  }
  to_ascii_with_route<Base, Alphabet>(outb, size, v);
  return size;
}
template <uint64_t Base, typename Alphabet, size_t N>
FOLLY_ALWAYS_INLINE size_t to_ascii_with_route(char (&out)[N], uint64_t v) {
  static_assert(N >= to_ascii_powers<Base, decltype(v)>::size, "out too small");
  return to_ascii_with_table<Base, Alphabet>(out, v);
}

} // namespace detail

//  to_ascii_size_max
//
//  The maximum size buffer that might be required to hold the ascii-encoded
//  representation of any value of unsigned type I in base Base.
//
//  In base 10, u64 requires at most 20 bytes, u32 at most 10, u16 at most 5,
//  and u8 at most 3.
template <uint64_t Base, typename Int>
FOLLY_INLINE_VARIABLE constexpr size_t to_ascii_size_max =
    detail::to_ascii_powers<Base, Int>::size;

//  to_ascii_size_max_decimal
//
//  An alias to to_ascii_size_max<10>.
template <typename Int>
FOLLY_INLINE_VARIABLE constexpr size_t to_ascii_size_max_decimal =
    to_ascii_size_max<10, Int>;

//  to_ascii_size
//
//  Returns the number of digits in the base Base representation of a uint64_t.
//  Useful for preallocating buffers, etc.
//
//  async-signal-safe
template <uint64_t Base>
size_t to_ascii_size(uint64_t v) {
  return detail::to_ascii_size_route<Base>(v);
}

//  to_ascii_size_decimal
//
//  An alias to to_ascii_size<10>.
//
//  async-signal-safe
inline size_t to_ascii_size_decimal(uint64_t v) {
  return to_ascii_size<10>(v);
}

//  to_ascii_with
//
//  Copies the digits of v, in base Base, translated with Alphabet, into buffer
//  and returns the number of bytes written.
//
//  Does *not* append a null terminator. It is the caller's responsibility to
//  append a null terminator if one is required.
//
//  Assumes buffer points to at least to_ascii_size<Base>(v) bytes of writable
//  memory. It is the caller's responsibility to provide a writable buffer with
//  the required min size.
//
//  async-signal-safe
template <uint64_t Base, typename Alphabet>
size_t to_ascii_with(char* outb, char const* oute, uint64_t v) {
  return detail::to_ascii_with_route<Base, Alphabet>(outb, oute, v);
}
template <uint64_t Base, typename Alphabet, size_t N>
size_t to_ascii_with(char (&out)[N], uint64_t v) {
  return detail::to_ascii_with_route<Base, Alphabet>(out, v);
}

//  to_ascii_lower
//
//  Composes to_ascii_with with to_ascii_alphabet_lower.
//
//  async-signal-safe
template <uint64_t Base>
size_t to_ascii_lower(char* outb, char const* oute, uint64_t v) {
  return to_ascii_with<Base, to_ascii_alphabet_lower>(outb, oute, v);
}
template <uint64_t Base, size_t N>
size_t to_ascii_lower(char (&out)[N], uint64_t v) {
  return to_ascii_with<Base, to_ascii_alphabet_lower>(out, v);
}

//  to_ascii_upper
//
//  Composes to_ascii_with with to_ascii_alphabet_upper.
//
//  async-signal-safe
template <uint64_t Base>
size_t to_ascii_upper(char* outb, char const* oute, uint64_t v) {
  return to_ascii_with<Base, to_ascii_alphabet_upper>(outb, oute, v);
}
template <uint64_t Base, size_t N>
size_t to_ascii_upper(char (&out)[N], uint64_t v) {
  return to_ascii_with<Base, to_ascii_alphabet_upper>(out, v);
}

//  to_ascii_decimal
//
//  An alias to to_ascii<10, false>.
//
//  async-signal-safe
inline size_t to_ascii_decimal(char* outb, char const* oute, uint64_t v) {
  return to_ascii_lower<10>(outb, oute, v);
}
template <size_t N>
inline size_t to_ascii_decimal(char (&out)[N], uint64_t v) {
  return to_ascii_lower<10>(out, v);
}

} // namespace folly
