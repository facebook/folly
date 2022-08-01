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

#include <folly/CpuId.h>
#include <folly/Portability.h>
#include <folly/detail/base64_detail/Base64Api.h>
#include <folly/detail/base64_detail/Base64SWAR.h>
#include <folly/detail/base64_detail/Base64_SSE4_2.h>

namespace folly::detail::base64_detail {
Base64RuntimeImpl base64EncodeSelectImplementation() {
#if FOLLY_SSE_PREREQ(4, 2)
  if (folly::CpuId().sse42()) {
    return {
        base64Encode_SSE4_2,
        base64URLEncode_SSE4_2,
        base64Decode_SSE4_2,
        base64URLDecodeSWAR};
  }
#endif
  return {
      base64EncodeScalar,
      base64URLEncodeScalar,
      base64DecodeSWAR,
      base64URLDecodeSWAR};
}
} // namespace folly::detail::base64_detail
