#pragma once
#include <cstddef>
#include <cstdint>

#include <folly/Portability.h>
#if defined(FOLLY_X64) && FOLLY_SSE_PREREQ(4, 2) && defined(__AVX512VL__) && defined(__AVX512F__)
#define FOLLY_ENABLE_AVX512_CRC32C_V8S3X4 1
#endif

namespace folly::detail {
uint32_t avx512_crc32c_v8s3x4(const uint8_t* buf, size_t len, uint32_t crc0);
}
