#pragma once
#include <cstddef>
#include <cstdint>

namespace folly::detail {
uint32_t neon_eor3_crc32_v8s2x4e_s1x2(const uint8_t* buf, size_t len, uint32_t crc0);
uint32_t neon_eor3_crc32_small(const uint8_t* buf, size_t len, uint32_t crc0);
bool has_neon_eor3_crc32_v8s2x4e_s1x2();
}
