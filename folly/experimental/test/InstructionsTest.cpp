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

#include <folly/experimental/Instructions.h>

#include <glog/logging.h>

#include <folly/portability/GTest.h>

using namespace folly;
using namespace folly::compression::instructions;

template <typename Arch>
struct InstructionsTest : ::testing::Test {};

#if FOLLY_X64
using InstructionsTestTypes = ::testing::Types<Default, Haswell>;
#else
using InstructionsTestTypes = ::testing::Types<Default>;
#endif

TYPED_TEST_SUITE(InstructionsTest, InstructionsTestTypes);

TYPED_TEST(InstructionsTest, BitExtraction) {
  using Arch = TypeParam;

  uint64_t value =
      0b11111110'11011100'10111010'10011000'01110110'01010100'00110010'00010000;

  if (!Arch::supported()) {
    return;
  }

  LOG(INFO) << "Testing " << Arch::name() << " Architecture";

  // Extract 4 bits a time, starting from bit 0
  uint64_t expected = 0;
  for (int i = 0; i < 64 - 4; i += 4) {
    EXPECT_EQ(expected, Arch::bextr(value, i, 4));
    ++expected;
  }

  // Extract 8 bits a time, starting from bit 1
  uint64_t value2 = value << 1;
  uint64_t lower = 0;
  uint64_t upper = 1;
  for (int i = 1; i < 64 - 8; i += 4) {
    expected = (lower & 0xF) | ((upper & 0xF) << 4);
    EXPECT_EQ(expected, Arch::bextr(value2, i, 8));
    ++lower;
    ++upper;
  }

  // Extract 16 bits a time, starting from bit 2
  uint64_t value3 = value << 2;
  uint64_t part0 = 0;
  uint64_t part1 = 1;
  uint64_t part2 = 2;
  uint64_t part3 = 3;
  for (int i = 2; i < 64 - 16; i += 4) {
    expected = (part0 & 0xF) | ((part1 & 0xF) << 4) | ((part2 & 0xF) << 8) |
        ((part3 & 0xF) << 12);
    EXPECT_EQ(expected, Arch::bextr(value3, i, 16));
    ++part0;
    ++part1;
    ++part2;
    ++part3;
  }

  // Extract 32 bits
  expected = 0b1011'1010'1001'1000'0111'0110'0101'0100;
  EXPECT_EQ(expected, Arch::bextr(value, 16, 32));

  // Extract all 64 bits
  EXPECT_EQ(value, Arch::bextr(value, 0, 64));

  // Extract 0 bits
  EXPECT_EQ(0, Arch::bextr(value, 4, 0));

  // Make sure only up to 63-th bits will be extracted
  EXPECT_EQ(0b1111, Arch::bextr(value, 60, 5));

  EXPECT_EQ(0, Arch::bextr(value, 64, 8));

  EXPECT_EQ(value, Arch::bextr(value, 0, 65));
}
