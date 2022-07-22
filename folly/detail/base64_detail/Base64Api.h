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

#include <folly/Portability.h>
#include <folly/detail/base64_detail/Base64Common.h>
#include <folly/detail/base64_detail/Base64Scalar.h>

namespace folly::detail::base64_detail {

struct Base64RuntimeImpl {
  using Encode = char* (*)(const char*, const char*, char*) noexcept;
  using Decode =
      Base64DecodeResult (*)(const char*, const char*, char*) noexcept;

  Encode encode;
  Encode encodeURL;
  Decode decode;
  Decode decodeURL;
};

Base64RuntimeImpl base64EncodeSelectImplementation();

inline const auto& base64EncodeRuntime() {
  static const auto r = base64EncodeSelectImplementation();
  return r;
}

#define BASE64_IS_CONSTANT_EVALUATED true

#ifdef __has_builtin
#if __has_builtin(__builtin_is_constant_evaluated)
#undef BASE64_IS_CONSTANT_EVALUATED
#define BASE64_IS_CONSTANT_EVALUATED __builtin_is_constant_evaluated()
#endif
#endif

inline FOLLY_CXX20_CONSTEXPR char* base64Encode(
    const char* f, const char* l, char* o) noexcept {
  if (BASE64_IS_CONSTANT_EVALUATED) {
    return base64EncodeScalar(f, l, o);
  } else {
    return base64EncodeRuntime().encode(f, l, o);
  }
}

inline FOLLY_CXX20_CONSTEXPR char* base64URLEncode(
    const char* f, const char* l, char* o) noexcept {
  if (BASE64_IS_CONSTANT_EVALUATED) {
    return base64URLEncodeScalar(f, l, o);
  } else {
    return base64EncodeRuntime().encodeURL(f, l, o);
  }
}

inline FOLLY_CXX20_CONSTEXPR Base64DecodeResult
base64Decode(const char* f, const char* l, char* o) noexcept {
  if (BASE64_IS_CONSTANT_EVALUATED) {
    return base64DecodeScalar(f, l, o);
  } else {
    return base64EncodeRuntime().decode(f, l, o);
  }
}

inline FOLLY_CXX20_CONSTEXPR Base64DecodeResult
base64URLDecode(const char* f, const char* l, char* o) noexcept {
  if (BASE64_IS_CONSTANT_EVALUATED) {
    return base64URLDecodeScalar(f, l, o);
  } else {
    return base64EncodeRuntime().decodeURL(f, l, o);
  }
}

#undef BASE64_IS_CONSTANT_EVALUATED

} // namespace folly::detail::base64_detail
