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

#include <folly/detail/base64_detail/Base64_SSE4_2.h>

#include <folly/Portability.h>
#include <folly/detail/base64_detail/Base64Simd.h>
#include <folly/detail/base64_detail/Base64_SSE4_2_Platform.h>

#if FOLLY_SSE_PREREQ(4, 2)

namespace folly::detail::base64_detail {

char* base64Encode_SSE4_2(const char* f, const char* l, char* o) noexcept {
  return base64SimdEncode<Base64_SSE4_2_Platform>(f, l, o);
}

char* base64URLEncode_SSE4_2(const char* f, const char* l, char* o) noexcept {
  return base64URLSimdEncode<Base64_SSE4_2_Platform>(f, l, o);
}

Base64DecodeResult base64Decode_SSE4_2(
    const char* f, const char* l, char* o) noexcept {
  return base64SimdDecode<Base64_SSE4_2_Platform>(f, l, o);
}

} // namespace folly::detail::base64_detail

#endif // defined(__SSE4_2__)
