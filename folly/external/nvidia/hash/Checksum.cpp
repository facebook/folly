/*
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

#if FOLLY_ARM_FEATURE_CRC32
#include <arm_acle.h>
#include <cstring>
#include <stddef.h>

namespace folly::detail {

uint32_t crc32_hw(const uint8_t* buf, size_t len, uint32_t crc) {
  while (len >= 8) {
    uint64_t val = 0;
    std::memcpy(&val, buf, 8);
    crc = __crc32d(crc, val);
    len -= 8;
    buf += 8;
  }

  if (len % 8 >= 4) {
    uint32_t val = 0;
    std::memcpy(&val, buf, 4);
    crc = __crc32w(crc, val);
    buf += 4;
  }

  if (len % 4 >= 2) {
    uint16_t val = 0;
    std::memcpy(&val, buf, 2);
    crc = __crc32h(crc, val);
    buf += 2;
  }

  if (len % 2 >= 1) {
    crc = __crc32b(crc, *buf);
  }

  return crc;
}

} // namespace folly::detail

#endif // FOLLY_ARM_FEATURE_CRC32