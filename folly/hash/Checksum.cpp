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

#include <folly/hash/Checksum.h>

#include <algorithm>
#include <stdexcept>

#include <boost/crc.hpp>

#include <folly/CpuId.h>
#include <folly/detail/TrapOnAvx512.h>
#include <folly/external/fast-crc32/avx512_crc32c_v8s3x4.h> // @manual
#include <folly/external/fast-crc32/sse_crc32c_v8s3x3.h> // @manual
#include <folly/hash/detail/ChecksumDetail.h>

#if FOLLY_SSE_PREREQ(4, 2)
#include <emmintrin.h>
#include <nmmintrin.h>
#endif

#if FOLLY_ARM_FEATURE_CRC32
#include <arm_acle.h>
#include <stddef.h>
#endif

namespace folly {

namespace detail {

uint32_t crc32c_sw(
    const uint8_t* data, size_t nbytes, uint32_t startingChecksum);
#if FOLLY_SSE_PREREQ(4, 2)

uint32_t crc32_sw(
    const uint8_t* data, size_t nbytes, uint32_t startingChecksum);

// Fast SIMD implementation of CRC-32 for x86 with pclmul
uint32_t crc32_hw(
    const uint8_t* data, size_t nbytes, uint32_t startingChecksum) {
  uint32_t sum = startingChecksum;
  size_t offset = 0;

  // Process unaligned bytes
  if ((uintptr_t)data & 15) {
    size_t limit = std::min(nbytes, -(uintptr_t)data & 15);
    sum = crc32_sw(data, limit, sum);
    offset += limit;
    nbytes -= limit;
  }

  if (nbytes >= 16) {
    sum = crc32_hw_aligned(sum, (const __m128i*)(data + offset), nbytes / 16);
    offset += nbytes & ~15;
    nbytes &= 15;
  }

  // Remaining unaligned bytes
  if (nbytes == 0) {
    return sum;
  }
  return crc32_sw(data + offset, nbytes, sum);
}

bool crc32c_hw_supported() {
  return crc32c_hw_supported_sse42();
}

bool crc32c_hw_supported_sse42() {
  static folly::CpuId id;
  return id.sse42();
}

bool crc32c_hw_supported_avx512() {
  static folly::CpuId id;
  static bool supported = id.avx512vl() && !detail::hasTrapOnAvx512();
  return supported;
}

bool crc32_hw_supported() {
  static folly::CpuId id;
  return id.sse42();
}

#elif FOLLY_ARM_FEATURE_CRC32
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

bool crc32c_hw_supported() {
  return true;
}

bool crc32c_hw_supported_sse42() {
  return false;
}

bool crc32c_hw_supported_avx512() {
  return false;
}

bool crc32_hw_supported() {
  return true;
}

#else // FOLLY_ARM_FEATURE_CRC32

uint32_t crc32_hw(
    const uint8_t* /* data */,
    size_t /* nbytes */,
    uint32_t /* startingChecksum */) {
  throw std::runtime_error("crc32_hw is not implemented on this platform");
}

bool crc32c_hw_supported() {
  return false;
}

bool crc32c_hw_supported_sse42() {
  return false;
}

bool crc32c_hw_supported_avx512() {
  return false;
}

bool crc32_hw_supported() {
  return false;
}
#endif

template <uint32_t CRC_POLYNOMIAL>
uint32_t crc_sw(const uint8_t* data, size_t nbytes, uint32_t startingChecksum) {
  // Reverse the bits in the starting checksum so they'll be in the
  // right internal format for Boost's CRC engine.
  //     O(1)-time, branchless bit reversal algorithm from
  //     http://graphics.stanford.edu/~seander/bithacks.html
  startingChecksum = ((startingChecksum >> 1) & 0x55555555) |
      ((startingChecksum & 0x55555555) << 1);
  startingChecksum = ((startingChecksum >> 2) & 0x33333333) |
      ((startingChecksum & 0x33333333) << 2);
  startingChecksum = ((startingChecksum >> 4) & 0x0f0f0f0f) |
      ((startingChecksum & 0x0f0f0f0f) << 4);
  startingChecksum = ((startingChecksum >> 8) & 0x00ff00ff) |
      ((startingChecksum & 0x00ff00ff) << 8);
  startingChecksum = (startingChecksum >> 16) | (startingChecksum << 16);

  boost::crc_optimal<32, CRC_POLYNOMIAL, ~0U, 0, true, true> sum(
      startingChecksum);
  sum.process_bytes(data, nbytes);
  return sum.checksum();
}

uint32_t crc32c_sw(
    const uint8_t* data, size_t nbytes, uint32_t startingChecksum) {
  constexpr uint32_t CRC32C_POLYNOMIAL = 0x1EDC6F41;
  return crc_sw<CRC32C_POLYNOMIAL>(data, nbytes, startingChecksum);
}

uint32_t crc32_sw(
    const uint8_t* data, size_t nbytes, uint32_t startingChecksum) {
  constexpr uint32_t CRC32_POLYNOMIAL = 0x04C11DB7;
  return crc_sw<CRC32_POLYNOMIAL>(data, nbytes, startingChecksum);
}

} // namespace detail

uint32_t crc32c(const uint8_t* data, size_t nbytes, uint32_t startingChecksum) {
#if defined(FOLLY_ENABLE_AVX512_CRC32C_V8S3X4)
  if (detail::crc32c_hw_supported_avx512() && nbytes > 4096) {
    return detail::avx512_crc32c_v8s3x4(data, nbytes, startingChecksum);
  }
#endif

  if (detail::crc32c_hw_supported()) {
#if defined(FOLLY_ENABLE_SSE42_CRC32C_V8S3X3)
    if (nbytes > 4096) {
      return detail::sse_crc32c_v8s3x3(data, nbytes, startingChecksum);
    }
#endif
    return detail::crc32c_hw(data, nbytes, startingChecksum);
  } else {
    return detail::crc32c_sw(data, nbytes, startingChecksum);
  }
}

uint32_t crc32(const uint8_t* data, size_t nbytes, uint32_t startingChecksum) {
  if (detail::crc32_hw_supported()) {
    return detail::crc32_hw(data, nbytes, startingChecksum);
  } else {
    return detail::crc32_sw(data, nbytes, startingChecksum);
  }
}

uint32_t crc32_type(
    const uint8_t* data, size_t nbytes, uint32_t startingChecksum) {
  return ~crc32(data, nbytes, startingChecksum);
}

uint32_t crc32_combine(uint32_t crc1, uint32_t crc2, size_t crc2len) {
  // Append up to 32 bits of zeroes in the normal way
  uint8_t data[4] = {0, 0, 0, 0};
  auto len = crc2len & 3;
  if (len) {
    crc1 = crc32(data, len, crc1);
  }

  if (detail::crc32_hw_supported()) {
    return detail::crc32_combine_hw(crc1, crc2, crc2len);
  } else {
    return detail::crc32_combine_sw(crc1, crc2, crc2len);
  }
}

uint32_t crc32c_combine(uint32_t crc1, uint32_t crc2, size_t crc2len) {
  // Append up to 32 bits of zeroes in the normal way
  uint8_t data[4] = {0, 0, 0, 0};
  auto len = crc2len & 3;
  if (len) {
    crc1 = crc32c(data, len, crc1);
  }

  if (detail::crc32c_hw_supported()) {
    return detail::crc32c_combine_hw(crc1, crc2, crc2len - len);
  } else {
    return detail::crc32c_combine_sw(crc1, crc2, crc2len - len);
  }
}

} // namespace folly
