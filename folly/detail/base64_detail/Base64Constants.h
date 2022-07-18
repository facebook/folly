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

namespace folly::detail::base64_detail::constants {

// Scalar --------------------------------------=

constexpr char kBase64Charset[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

constexpr char kBase64URLCharset[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

// Special value that we can or with any valid value and that
// way keep track if we had encountered an error or not.
constexpr char kDecodeErrorMarker = 0xff;

constexpr char base64DecodeRule(char x) {
  if ('A' <= x && x <= 'Z') {
    return x - 'A';
  }
  if ('a' <= x && x <= 'z') {
    return x - 'a' + 26;
  }
  if ('0' <= x && x <= '9') {
    return x - '0' + 26 * 2;
  }
  if (x == '+') {
    return 62;
  }
  if (x == '/') {
    return 63;
  }
  return kDecodeErrorMarker;
}

constexpr char base64URLDecodeRule(char x) {
  if (x == '-') {
    return 62;
  }
  if (x == '_') {
    return 63;
  }
  return base64DecodeRule(x);
}

template <typename DecodeChar>
constexpr auto buildDecodeTable(DecodeChar decodeChar) {
  std::array<char, 256> res = {};
  for (std::size_t i = 0; i != res.size(); ++i) {
    res[i] = decodeChar(static_cast<char>(i));
  }
  return res;
}

constexpr std::array<char, 256> kBase64DecodeTable =
    buildDecodeTable(base64DecodeRule);
constexpr std::array<char, 256> kBase64URLDecodeTable =
    buildDecodeTable(base64URLDecodeRule);

} // namespace folly::detail::base64_detail::constants
