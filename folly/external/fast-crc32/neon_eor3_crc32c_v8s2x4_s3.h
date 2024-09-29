#pragma once
#include <cstddef>
#include <cstdint>

namespace folly::detail {
uint32_t neon_eor3_crc32c_v8s2x4_s3(const uint8_t* buf, size_t len, uint32_t crc0);
bool has_neon_eor3_crc32c_v8s2x4_s3();
}
