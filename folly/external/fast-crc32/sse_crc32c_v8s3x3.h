#pragma once
#include <cstdint>
extern "C" {
uint32_t sse_crc32c_v8s3x3(const uint8_t* buf, size_t len, uint32_t crc0);
}
