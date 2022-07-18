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
#include <cstring>
#include <folly/CPortability.h>
#include <folly/detail/base64_detail/Base64Common.h>
#include <folly/detail/base64_detail/Base64Constants.h>
#include <folly/detail/base64_detail/Base64HiddenConstants.h>
#include <folly/detail/base64_detail/Base64SWAR.h>
#include <folly/detail/base64_detail/Base64Scalar.h>

namespace folly::detail::base64_detail {

// The funcitons are marked ALWAYS INLINE because there are platform specific
// single instantiations actually inlined in the platform call

template <typename PlatformDelegate, bool isURL>
FOLLY_ALWAYS_INLINE char* base64SimdEncodeImpl(
    const char* f, const char* l, char* o) noexcept {
  static constexpr std::size_t kRegisterSize = PlatformDelegate::kRegisterSize;
  static constexpr std::size_t kInputAdvance = kRegisterSize * 3 / 4;
  static constexpr auto* kEncodeTable = isURL
      ? constants::kEncodeURLTable.data()
      : constants::kEncodeTable.data();

  while (static_cast<std::size_t>(l - f) >= kRegisterSize) {
    auto loaded = PlatformDelegate::loadu(f);
    auto idxs = PlatformDelegate::encodeToIndexes(loaded);
    auto encoded = PlatformDelegate::lookupByIndex(idxs, kEncodeTable);
    PlatformDelegate::storeu(o, encoded);
    f += kInputAdvance;
    o += kRegisterSize;
  }

  if constexpr (isURL) {
    return base64URLEncodeScalar(f, l, o);
  } else {
    return base64EncodeScalar(f, l, o);
  }
}

template <typename PlatformDelegate>
FOLLY_ALWAYS_INLINE char* base64SimdEncode(
    const char* f, const char* l, char* o) noexcept {
  return base64SimdEncodeImpl<PlatformDelegate, /*isURL*/ false>(f, l, o);
}

template <typename PlatformDelegate>
FOLLY_ALWAYS_INLINE char* base64URLSimdEncode(
    const char* f, const char* l, char* o) noexcept {
  return base64SimdEncodeImpl<PlatformDelegate, /*isURL*/ true>(f, l, o);
}

template <typename PlatformDelegate>
FOLLY_ALWAYS_INLINE Base64DecodeResult
base64SimdDecode(const char* f, const char* l, char* o) noexcept {
  static constexpr std::size_t kRegisterSize = PlatformDelegate::kRegisterSize;
  static constexpr std::size_t kOutputAdvance = kRegisterSize * 3 / 4;

  static_assert(kRegisterSize >= 16);
  static_assert(kRegisterSize % 4 == 0);
  static_assert(kOutputAdvance * 2 > kRegisterSize);

  // See proof why this is good enough in the readme.
  static constexpr std::size_t kSimdLimit = kRegisterSize * 3 / 2;

  auto errorAccumulator = PlatformDelegate::initError();
  while (static_cast<std::size_t>(l - f) >= kSimdLimit) {
    auto reg = PlatformDelegate::loadu(f);
    auto idxs = PlatformDelegate::decodeToIndex(reg, errorAccumulator);
    auto cvtd = PlatformDelegate::packIndexesToBytes(idxs);
    PlatformDelegate::storeu(o, cvtd);

    f += kRegisterSize;
    o += kOutputAdvance;
  }

  if (PlatformDelegate::hasErrors(errorAccumulator)) {
    return {false, o};
  }

  return base64DecodeSWAR(f, l, o);
}

} // namespace folly::detail::base64_detail
