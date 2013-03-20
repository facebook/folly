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
#define FOLLY_CONV_INTERNAL
#include "folly/Conv.h"

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
    FOLLY_RANGE_CHECK(b < e,
                      "No non-whitespace characters found in input string");
    if (!isspace(*b)) break;
  }

  bool result;
  size_t len = e - b;
  switch (*b) {
    case '0':
    case '1': {
      // Attempt to parse the value as an integer
      StringPiece tmp(*src);
      uint8_t value = to<uint8_t>(&tmp);
      // Only accept 0 or 1
      FOLLY_RANGE_CHECK(value <= 1,
                        "Integer overflow when parsing bool: must be 0 or 1");
      b = tmp.begin();
      result = (value == 1);
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
        FOLLY_RANGE_CHECK(false, "Invalid value for bool");
      }
      break;
    default:
      FOLLY_RANGE_CHECK(false, "Invalid value for bool");
  }

  src->assign(b, e);
  return result;
}

} // namespace detail
} // namespace folly
