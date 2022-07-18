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

#include <array>
#include <cstdint>
#include <limits>
#include <folly/detail/base64_detail/Base64Constants.h>

namespace folly::detail::base64_detail::constants {

// Some constants we have to expose for everyone in order to
// support constexpr operations.
// These we can internalize.

// SWAR -----------------------------------------

constexpr std::uint32_t kSwarDecodeErrorMarker = 0xff'ff'ff'ff;

template <typename DecodeChar>
constexpr std::array<std::array<std::uint32_t, 256>, 4> buildSWARDecodeTable(
    DecodeChar decodeChar) {
  std::array<std::array<std::uint32_t, 256>, 4> res = {};

  for (std::uint32_t c = std::numeric_limits<std::uint8_t>::min();
       c != std::numeric_limits<std::uint8_t>::max() + 1;
       ++c) {
    char decoded = decodeChar(static_cast<char>(c));
    if (decoded == kDecodeErrorMarker) {
      res[0][c] = res[1][c] = res[2][c] = res[3][c] = kSwarDecodeErrorMarker;
      continue;
    }

    // What we want? '____'cddd'bbcc'aaab (LE)
    // clang-format off
    const std::uint32_t d = static_cast<std::uint32_t>(decoded);
    res[0][c] = d << 2;                                   // 0000'0000'aaa0
    res[1][c] = d >> 4 | (d << 12 & 0xff00);              // 0000'bb00'000b
    res[2][c] = (d << 6 & 0xff00) | (d << 22 & 0xff0000); // c000'00cc'0000
    res[3][c] = d << 16;                                  // 0ddd'0000'0000
    // clang-format on
  }

  return res;
}

constexpr auto kBase64SwarDecodeTable = buildSWARDecodeTable(base64DecodeRule);
constexpr auto kBase64SwarURLDecodeTable =
    buildSWARDecodeTable(base64URLDecodeRule);

// Simd -----------------------------------------

// clang-format off
constexpr std::array<std::int8_t, 16> kEncodeTable {{
  'A' - 0,  'a' - 26,
  '0' - 52, '1' - 53, '2' - 54, '3' - 55, '4' - 56,
  '5' - 57, '6' - 58, '7' - 59, '8' - 60, '9' - 61,
  '+' - 62, '/' - 63,
  '=' - 64, '\0' - 65
}};
// clang-format on

constexpr auto kEncodeURLTable = [] {
  auto res = kEncodeTable;
  res[12] += '-' - '+';
  res[13] += '_' - '/';
  return res;
}();

constexpr auto kValidHighByLowNibble = [] {
  auto build = [](auto... nibbles) {
    std::uint8_t nibblesArr[] = {static_cast<std::uint8_t>(nibbles)...};
    std::uint8_t res = 0;
    for (std::uint8_t nibble : nibblesArr) {
      res |= 1 << nibble;
    }
    return res;
  };

  std::array<std::uint8_t, 16> res{};

  res[0] = build(3, 5, 7);

  // 1 - 9
  res[1] = build(3, 4, 5, 6, 7);
  for (int i = 1; i != 0xA; ++i) {
    res[i] = res[1];
  }

  res[0xA] = build(4, 5, 6, 7);
  res[0xB] = build(4, 6);
  res[0xC] = build(1, 4, 6);
  res[0xD] = res[0xB];
  res[0xE] = res[0xB];
  res[0xF] = build(2, 4, 6);

  return res;
}();

// clang-format off
constexpr std::array<std::int8_t, 16> kOffsetByHighNibbleDecodeTable {{
  0,          // invalid
  62 - 0x1C,  // 1: '+'
  63 - '/',   // 2: '/'
  52 - '0',   // 3: '0' - '9'
  0  - 'A',   // 4: 'A' - 'O'
  0  - 'A',   // 5: 'P' - 'Z'
  26 - 'a',   // 6: 'a' - 'o'
  26 - 'a',   // 7: 'p' - 'z'
  0, 0, 0, 0, // 8, 9, 10, A: invalid
  0, 0, 0, 0, // B, C, D, E: invalid
}};
// clang-format on

} // namespace folly::detail::base64_detail::constants
