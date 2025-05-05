/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
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

#include <folly/lang/Byte.h>

#include <folly/portability/GTest.h>

class ByteTest : public testing::Test {};

using byte = folly::byte;
using base = unsigned char;

static_assert(sizeof(byte) == 1);
static_assert(!std::is_integral_v<byte>);
static_assert(std::is_standard_layout_v<byte>);
static_assert(std::is_trivial_v<byte>);
static_assert(std::is_same_v<std::underlying_type_t<byte>, base>);

TEST_F(ByteTest, operations) {
  for (int b = 0; b <= std::numeric_limits<base>::max(); ++b) {
    EXPECT_EQ(byte(~b), ~byte(b));

    for (int c = 0; c <= std::numeric_limits<base>::max(); ++c) {
      EXPECT_EQ(byte(b & c), byte(b) & byte(c));
      EXPECT_EQ(byte(b | c), byte(b) | byte(c));
      EXPECT_EQ(byte(b ^ c), byte(b) ^ byte(c));

      byte o{};
      EXPECT_EQ(byte(b & c), (o = byte(b)) &= byte(c));
      EXPECT_EQ(byte(b | c), (o = byte(b)) |= byte(c));
      EXPECT_EQ(byte(b ^ c), (o = byte(b)) ^= byte(c));
    }

    for (int s = 0; s < int(sizeof(byte)); ++s) {
      EXPECT_EQ(byte(b << s), byte(b) << s);
      EXPECT_EQ(byte(b >> s), byte(b) >> s);

      byte o{};
      EXPECT_EQ(byte(b << s), (o = byte(b)) <<= s);
      EXPECT_EQ(byte(b >> s), (o = byte(b)) >>= s);
    }
  }
}

TEST_F(ByteTest, to_integer) {
  EXPECT_EQ(7, folly::to_integer<int>(byte(7)));
}
