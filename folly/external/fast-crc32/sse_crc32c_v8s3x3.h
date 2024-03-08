#pragma once
#include <cstddef>
#include <cstdint>

#include <folly/Portability.h>
#if defined(FOLLY_X64) && FOLLY_SSE_PREREQ(4, 2)
#define FOLLY_ENABLE_SSE42_CRC32C_V8S3X3 1
#endif

namespace folly::detail {
uint32_t sse_crc32c_v8s3x3(const uint8_t* buf, size_t len, uint32_t crc0);
}
