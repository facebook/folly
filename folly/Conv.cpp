/*
 * Copyright 2016 Facebook, Inc.
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
#define FOLLY_CONV_INTERNAL
#include <folly/Conv.h>

namespace folly {
namespace detail {

extern const char digit1[101] =
  "00000000001111111111222222222233333333334444444444"
  "55555555556666666666777777777788888888889999999999";
extern const char digit2[101] =
  "01234567890123456789012345678901234567890123456789"
  "01234567890123456789012345678901234567890123456789";

template <> const char *const MaxString<bool>::value = "true";
template <> const char *const MaxString<uint8_t>::value = "255";
template <> const char *const MaxString<uint16_t>::value = "65535";
template <> const char *const MaxString<uint32_t>::value = "4294967295";
#if __SIZEOF_LONG__ == 4
template <> const char *const MaxString<unsigned long>::value =
  "4294967295";
#else
template <> const char *const MaxString<unsigned long>::value =
  "18446744073709551615";
#endif
static_assert(sizeof(unsigned long) >= 4,
              "Wrong value for MaxString<unsigned long>::value,"
              " please update.");
template <> const char *const MaxString<unsigned long long>::value =
  "18446744073709551615";
static_assert(sizeof(unsigned long long) >= 8,
              "Wrong value for MaxString<unsigned long long>::value"
              ", please update.");

#ifdef FOLLY_HAVE_INT128_T
template <> const char *const MaxString<__uint128_t>::value =
  "340282366920938463463374607431768211455";
#endif

namespace {
/*
 * Lookup tables that converts from a decimal character value to an integral
 * binary value, shifted by a decimal "shift" multiplier.
 * For all character values in the range '0'..'9', the table at those
 * index locations returns the actual decimal value shifted by the multiplier.
 * For all other values, the lookup table returns an invalid OOR value.
 */
// Out-of-range flag value, larger than the largest value that can fit in
// four decimal bytes (9999), but four of these added up together should
// still not overflow uint16_t.
constexpr int32_t OOR = 10000;

FOLLY_ALIGNED(16) constexpr uint16_t shift1[] = {
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 0-9
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  //  10
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  //  20
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  //  30
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, 0,         //  40
  1, 2, 3, 4, 5, 6, 7, 8, 9, OOR, OOR,
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  //  60
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  //  70
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  //  80
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  //  90
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 100
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 110
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 120
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 130
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 140
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 150
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 160
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 170
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 180
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 190
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 200
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 210
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 220
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 230
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 240
  OOR, OOR, OOR, OOR, OOR, OOR                       // 250
};

FOLLY_ALIGNED(16) constexpr uint16_t shift10[] = {
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 0-9
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  //  10
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  //  20
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  //  30
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, 0,         //  40
  10, 20, 30, 40, 50, 60, 70, 80, 90, OOR, OOR,
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  //  60
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  //  70
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  //  80
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  //  90
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 100
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 110
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 120
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 130
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 140
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 150
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 160
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 170
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 180
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 190
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 200
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 210
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 220
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 230
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 240
  OOR, OOR, OOR, OOR, OOR, OOR                       // 250
};

FOLLY_ALIGNED(16) constexpr uint16_t shift100[] = {
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 0-9
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  //  10
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  //  20
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  //  30
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, 0,         //  40
  100, 200, 300, 400, 500, 600, 700, 800, 900, OOR, OOR,
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  //  60
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  //  70
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  //  80
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  //  90
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 100
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 110
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 120
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 130
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 140
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 150
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 160
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 170
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 180
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 190
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 200
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 210
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 220
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 230
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 240
  OOR, OOR, OOR, OOR, OOR, OOR                       // 250
};

FOLLY_ALIGNED(16) constexpr uint16_t shift1000[] = {
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 0-9
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  //  10
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  //  20
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  //  30
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, 0,         //  40
  1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, OOR, OOR,
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  //  60
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  //  70
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  //  80
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  //  90
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 100
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 110
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 120
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 130
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 140
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 150
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 160
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 170
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 180
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 190
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 200
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 210
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 220
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 230
  OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR, OOR,  // 240
  OOR, OOR, OOR, OOR, OOR, OOR                       // 250
};
}

inline bool bool_str_cmp(const char** b, size_t len, const char* value) {
  // Can't use strncasecmp, since we want to ensure that the full value matches
  const char* p = *b;
  const char* e = *b + len;
  const char* v = value;
  while (*v != '\0') {
    if (p == e || tolower(*p) != *v) { // value is already lowercase
      return false;
    }
    ++p;
    ++v;
  }

  *b = p;
  return true;
}

bool str_to_bool(StringPiece* src) {
  auto b = src->begin(), e = src->end();
  for (;; ++b) {
    FOLLY_RANGE_CHECK_STRINGPIECE(
      b < e, "No non-whitespace characters found in input string", *src);
    if (!isspace(*b)) break;
  }

  bool result;
  size_t len = e - b;
  switch (*b) {
    case '0':
    case '1': {
      result = false;
      for (; b < e && isdigit(*b); ++b) {
        FOLLY_RANGE_CHECK_STRINGPIECE(
          !result && (*b == '0' || *b == '1'),
          "Integer overflow when parsing bool: must be 0 or 1", *src);
        result = (*b == '1');
      }
      break;
    }
    case 'y':
    case 'Y':
      result = true;
      if (!bool_str_cmp(&b, len, "yes")) {
        ++b;  // accept the single 'y' character
      }
      break;
    case 'n':
    case 'N':
      result = false;
      if (!bool_str_cmp(&b, len, "no")) {
        ++b;
      }
      break;
    case 't':
    case 'T':
      result = true;
      if (!bool_str_cmp(&b, len, "true")) {
        ++b;
      }
      break;
    case 'f':
    case 'F':
      result = false;
      if (!bool_str_cmp(&b, len, "false")) {
        ++b;
      }
      break;
    case 'o':
    case 'O':
      if (bool_str_cmp(&b, len, "on")) {
        result = true;
      } else if (bool_str_cmp(&b, len, "off")) {
        result = false;
      } else {
        FOLLY_RANGE_CHECK_STRINGPIECE(false, "Invalid value for bool", *src);
      }
      break;
    default:
      FOLLY_RANGE_CHECK_STRINGPIECE(false, "Invalid value for bool", *src);
  }

  src->assign(b, e);
  return result;
}

/**
 * String represented as a pair of pointers to char to unsigned
 * integrals. Assumes NO whitespace before or after, and also that the
 * string is composed entirely of digits. Tgt must be unsigned, and no
 * sign is allowed in the string (even it's '+'). String may be empty,
 * in which case digits_to throws.
 */
template <class Tgt>
Tgt digits_to(const char* b, const char* e) {

  static_assert(!std::is_signed<Tgt>::value, "Unsigned type expected");
  assert(b <= e);

  const size_t size = e - b;

  /* Although the string is entirely made of digits, we still need to
   * check for overflow.
   */
  if (size >= std::numeric_limits<Tgt>::digits10 + 1) {
    // Leading zeros? If so, recurse to keep things simple
    if (b < e && *b == '0') {
      for (++b;; ++b) {
        if (b == e)
          return 0; // just zeros, e.g. "0000"
        if (*b != '0')
          return digits_to<Tgt>(b, e);
      }
    }
    FOLLY_RANGE_CHECK_BEGIN_END(
        size == std::numeric_limits<Tgt>::digits10 + 1 &&
            strncmp(b, detail::MaxString<Tgt>::value, size) <= 0,
        "Numeric overflow upon conversion",
        b,
        e);
  }

  // Here we know that the number won't overflow when
  // converted. Proceed without checks.

  Tgt result = 0;

  for (; e - b >= 4; b += 4) {
    result *= 10000;
    const int32_t r0 = shift1000[static_cast<size_t>(b[0])];
    const int32_t r1 = shift100[static_cast<size_t>(b[1])];
    const int32_t r2 = shift10[static_cast<size_t>(b[2])];
    const int32_t r3 = shift1[static_cast<size_t>(b[3])];
    const auto sum = r0 + r1 + r2 + r3;
    assert(sum < OOR && "Assumption: string only has digits");
    result += sum;
  }

  switch (e - b) {
  case 3: {
    const int32_t r0 = shift100[static_cast<size_t>(b[0])];
    const int32_t r1 = shift10[static_cast<size_t>(b[1])];
    const int32_t r2 = shift1[static_cast<size_t>(b[2])];
    const auto sum = r0 + r1 + r2;
    assert(sum < OOR && "Assumption: string only has digits");
    return result * 1000 + sum;
  }
  case 2: {
    const int32_t r0 = shift10[static_cast<size_t>(b[0])];
    const int32_t r1 = shift1[static_cast<size_t>(b[1])];
    const auto sum = r0 + r1;
    assert(sum < OOR && "Assumption: string only has digits");
    return result * 100 + sum;
  }
  case 1: {
    const int32_t sum = shift1[static_cast<size_t>(b[0])];
    assert(sum < OOR && "Assumption: string only has digits");
    return result * 10 + sum;
  }
  }

  assert(b == e);
  FOLLY_RANGE_CHECK_BEGIN_END(
      size > 0, "Found no digits to convert in input", b, e);
  return result;
}

template unsigned char digits_to<unsigned char>(const char* b, const char* e);
template unsigned short digits_to<unsigned short>(const char* b, const char* e);
template unsigned int digits_to<unsigned int>(const char* b, const char* e);
template unsigned long digits_to<unsigned long>(const char* b, const char* e);
template unsigned long long digits_to<unsigned long long>(const char* b,
                                                          const char* e);
#if FOLLY_HAVE_INT128_T
template unsigned __int128 digits_to<unsigned __int128>(const char* b,
                                                        const char* e);
#endif

} // namespace detail
} // namespace folly
