#pragma once
#include <cstddef>
#include <cstdint>
namespace folly::detail {
uint32_t avx512_crc32c_v8s3x4(const uint8_t* buf, size_t len, uint32_t crc0);
}
