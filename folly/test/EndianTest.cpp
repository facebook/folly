/*
 * Copyright 2012 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "folly/Bits.h"

#include <gtest/gtest.h>

using namespace folly;

TEST(Endian, Basic) {
  uint8_t v8 = 0x12;
  uint8_t v8s = v8;
  uint16_t v16 = 0x1234;
  uint16_t v16s = bswap_16(v16);
  uint32_t v32 = 0x12345678;
  uint32_t v32s = bswap_32(v32);
  uint64_t v64 = 0x123456789abcdef0ULL;
  uint64_t v64s = bswap_64(v64);

#if __BYTE_ORDER == __LITTLE_ENDIAN

#define GEN1(sz) \
  EXPECT_EQ(v##sz, Endian::little(v##sz)); \
  EXPECT_EQ(v##sz, Endian::little##sz(v##sz)); \
  EXPECT_EQ(v##sz##s, Endian::big(v##sz)); \
  EXPECT_EQ(v##sz##s, Endian::big##sz(v##sz));

#elif __BYTE_ORDER == __BIG_ENDIAN

#define GEN1(sz) \
  EXPECT_EQ(v##sz##s, Endian::little(v##sz)); \
  EXPECT_EQ(v##sz##s, Endian::little##sz(v##sz)); \
  EXPECT_EQ(v##sz, Endian::big(v##sz)); \
  EXPECT_EQ(v##sz, Endian::big##sz(v##sz));

#else
# error Your machine uses a weird endianness!
#endif  /* __BYTE_ORDER */

#define GEN(sz) \
  EXPECT_EQ(v##sz##s, Endian::swap(v##sz)); \
  EXPECT_EQ(v##sz##s, Endian::swap##sz(v##sz)); \
  GEN1(sz);

  GEN(8);
  GEN(16)
  GEN(32)
  GEN(64)

#undef GEN
#undef GEN1
}
