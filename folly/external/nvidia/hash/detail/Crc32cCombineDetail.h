/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include <folly/Portability.h>

#if FOLLY_NEON && FOLLY_ARM_FEATURE_CRC32 && FOLLY_ARM_FEATURE_AES && \
    FOLLY_ARM_FEATURE_SHA2

#include <arm_acle.h>
#include <arm_neon.h>

namespace folly::detail {

inline uint32_t gf_multiply_crc32c_hw(uint64_t crc1, uint64_t crc2, uint32_t) {
  const uint64x2_t count = vsetq_lane_u64(0, vdupq_n_u64(1), 1);

  const poly128_t res0 = vmull_p64(crc2, crc1);
  const uint64x2_t res1 =
      vshlq_u64(vreinterpretq_u64_p128(res0), vreinterpretq_s64_u64(count));

  // Use hardware crc32c to do reduction from 64 -> 32 bytes
  const uint64_t res2 = vgetq_lane_u64(res1, 0);
  const uint32_t res3 = __crc32cw(0, res2);
  const uint32_t res4 = vgetq_lane_u32(vreinterpretq_u32_u64(res1), 1);

  return res3 ^ res4;
}

inline uint32_t gf_multiply_crc32_hw(uint64_t crc1, uint64_t crc2, uint32_t) {
  const uint64x2_t count = vsetq_lane_u64(0, vdupq_n_u64(1), 1);

  const poly128_t res0 = vmull_p64(crc2, crc1);
  const uint64x2_t res1 =
      vshlq_u64(vreinterpretq_u64_p128(res0), vreinterpretq_s64_u64(count));

  // Use hardware crc32 to do reduction from 64 -> 32 bytes
  const uint64_t res2 = vgetq_lane_u64(res1, 0);
  const uint32_t res3 = __crc32w(0, res2);
  const uint32_t res4 = vgetq_lane_u32(vreinterpretq_u32_u64(res1), 1);

  return res3 ^ res4;
}

} // namespace folly

#endif // FOLLY_ARM_FEATURE_CRC32
