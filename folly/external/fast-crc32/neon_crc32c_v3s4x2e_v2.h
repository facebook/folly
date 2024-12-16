#pragma once
#include <cstddef>
#include <cstdint>

namespace folly::detail {
uint32_t neon_crc32c_v3s4x2e_v2(const uint8_t* buf, size_t len, uint32_t crc0);
bool has_neon_crc32c_v3s4x2e_v2();
}
