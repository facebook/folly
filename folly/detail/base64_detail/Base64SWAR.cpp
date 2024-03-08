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

#include <array>
#include <cstring>
#include <folly/Portability.h>
#include <folly/detail/base64_detail/Base64Constants.h>
#include <folly/detail/base64_detail/Base64HiddenConstants.h>
#include <folly/detail/base64_detail/Base64SWAR.h>
#include <folly/detail/base64_detail/Base64Scalar.h>

namespace folly::detail::base64_detail {
namespace {

// Practically same code that is for constexpr version, but these use SWAR
// tables to avoid having extra constants.
char* base64DecodeTailSWAR(
    const char* f, char* o, std::uint32_t& errorAccumulator) {
  const auto& table = constants::kBase64SwarDecodeTable[0];

  std::uint32_t aaa = table[atAsU8(f, 0)];
  std::uint32_t bbb = table[atAsU8(f, 1)];

  *o++ = static_cast<char>((aaa) | (bbb >> 6));
  errorAccumulator |= aaa | bbb;

  if (f[2] == '=' && f[3] == '=') {
    if (bbb & 0x3c) {
      errorAccumulator = constants::kSwarDecodeErrorMarker;
    }
    return o;
  }

  std::uint32_t ccc = table[atAsU8(f, 2)];
  *o++ = static_cast<char>((bbb << 2) | (ccc >> 4));
  errorAccumulator |= ccc;

  if (f[3] == '=') {
    if (ccc & 0xc) {
      errorAccumulator = constants::kSwarDecodeErrorMarker;
    }
    return o;
  }

  std::uint32_t ddd = table[atAsU8(f, 3)];
  *o++ = static_cast<char>((ccc << 4) | ddd >> 2);
  errorAccumulator |= ddd;

  return o;
}

char* base64URLDecodeTailSWAR(
    const char* f, const char* l, char* o, std::uint32_t& errorAccumulator) {
  base64URLDecodeStripValidPadding(f, l);

  const auto& table = constants::kBase64SwarURLDecodeTable[0];

  std::uint32_t aaa = table[atAsU8(f, 0)];
  std::uint32_t bbb = table[atAsU8(f, 1)];

  *o++ = static_cast<char>((aaa) | (bbb >> 6));
  errorAccumulator |= aaa | bbb;

  f += 2;
  if (f == l) {
    return o;
  }

  std::uint32_t ccc = table[atAsU8(f, 0)];
  *o++ = static_cast<char>((bbb << 2) | (ccc >> 4));
  errorAccumulator |= ccc;
  ++f;

  if (f == l) {
    return o;
  }

  std::uint32_t ddd = table[atAsU8(f, 0)];
  *o++ = static_cast<char>((ccc << 4) | ddd >> 2);
  errorAccumulator |= ddd;

  return o;
}

template <bool isURL>
constexpr auto kBase64SwarDecodeTable =
    isURL ? constants::kBase64SwarURLDecodeTable
          : constants::kBase64SwarDecodeTable;

template <bool isURL>
std::uint32_t base64DecodeSWARMainLoop(
    const char*& f, const char* l, char*& o) noexcept {
  std::uint32_t errorAccumulator = 0;

  while (l - f > 4) {
    std::uint32_t r = //
        kBase64SwarDecodeTable<isURL>[0][atAsU8(f, 0)] |
        kBase64SwarDecodeTable<isURL>[1][atAsU8(f, 1)] |
        kBase64SwarDecodeTable<isURL>[2][atAsU8(f, 2)] |
        kBase64SwarDecodeTable<isURL>[3][atAsU8(f, 3)];

    errorAccumulator |= r;
    std::memcpy(o, &r, sizeof(r));

    f += 4;
    o += 3;
  }

  return errorAccumulator;
}

} // namespace

Base64DecodeResult base64DecodeSWAR(
    const char* f, const char* l, char* o) noexcept {
  if (f == l) {
    return {true, o};
  }
  if ((l - f) % 4) {
    return {false, o};
  }

  std::uint32_t errorAccumulator =
      base64DecodeSWARMainLoop</*isURL*/ false>(f, l, o);

  o = base64DecodeTailSWAR(f, o, errorAccumulator);

  return {errorAccumulator != constants::kSwarDecodeErrorMarker, o};
}

Base64DecodeResult base64URLDecodeSWAR(
    const char* f, const char* l, char* o) noexcept {
  if (f == l) {
    return {true, o};
  }

  if ((l - f) % 4 == 1) {
    return {false, o};
  }

  std::uint32_t errorAccumulator =
      base64DecodeSWARMainLoop</*isURL*/ true>(f, l, o);

  o = base64URLDecodeTailSWAR(f, l, o, errorAccumulator);
  return {errorAccumulator != constants::kSwarDecodeErrorMarker, o};
}

} // namespace folly::detail::base64_detail
