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

#include <cstddef>
#include <cstdint>

namespace folly::detail::base64_detail {

constexpr std::size_t base64EncodedSize(std::size_t inSize) {
  return ((inSize + 2) / 3) * 4;
}

constexpr std::size_t base64URLEncodedSize(std::size_t inSize) {
  return (inSize / 3) * 4 + inSize % 3 + (inSize % 3 > 0);
}

// More incorrect usage of '=' padding will be detected during decoding
constexpr std::size_t base64PaddingToSubtract(const char* l) {
  bool isL_1Padding = *(l - 1) == '=';
  bool isL_2Padding = *(l - 2) == '=';

  return isL_1Padding + (isL_1Padding && isL_2Padding);
}

// Does not detect errors, all of them will be detected during actual decoding.
constexpr std::size_t base64DecodedSize(const char* f, const char* l) {
  std::size_t n = static_cast<std::size_t>(l - f);
  if (n < 4) { // correctness is checked when decoding
    return 0;
  }
  std::size_t res = n / 4 * 3;
  res -= base64PaddingToSubtract(l);

  return res;
}

constexpr std::size_t base64URLDecodedSize(const char* f, const char* l) {
  std::size_t n = static_cast<std::size_t>(l - f);

  // Unfortunatly, we cannot reuse the base64DecodedSize here.
  if (n < 2) {
    return 0;
  }

  std::size_t res = n / 4 * 3;

  if (n % 4 == 0) {
    // The invalid padding causes a lot of assumptions break.
    // Introduced an extra check to only subtract padding when it's
    // in a valid position.
    res -= base64PaddingToSubtract(l);
  }

  std::size_t extra = (n % 4) > 0 ? n % 4 - 1 : 0;
  res += extra;

  return res;
}

struct Base64DecodeResult {
  bool isSuccess = false;
  char* o;
};

} // namespace folly::detail::base64_detail
