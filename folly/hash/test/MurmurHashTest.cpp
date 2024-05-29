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

#include <cstdint>
#include <string_view>

#include <folly/hash/MurmurHash.h>

#include <folly/portability/GTest.h>

namespace {

std::uint64_t murmurHash64(std::string_view input) {
  return folly::hash::murmurHash64(input.data(), input.size(), /* seed */ 0);
}

} // namespace

TEST(MurmurHash, Empty) {
  EXPECT_EQ(murmurHash64(""), 0ULL);
}

TEST(MurmurHash, Tail) {
  // Short sequences, no execution of primary loop, only tail handling.
  EXPECT_EQ(murmurHash64("0"), 5533571732986600803ULL);
  EXPECT_EQ(murmurHash64("01"), 2988402087957123519ULL);
  EXPECT_EQ(murmurHash64("012"), 18121251311279197961ULL);
  EXPECT_EQ(murmurHash64("0123"), 3086299600550921888ULL);
  EXPECT_EQ(murmurHash64("01234"), 12373468686010462630ULL);
  EXPECT_EQ(murmurHash64("012345"), 8037360064841115407ULL);
  EXPECT_EQ(murmurHash64("0123456"), 12284635732915976134ULL);
}

TEST(MurmurHash, PrimaryLoop) {
  EXPECT_EQ(murmurHash64("01234567"), 9778579411364587418ULL);
  EXPECT_EQ(murmurHash64("0123456789ABCDEF"), 8277819783762704778ULL);
  EXPECT_EQ(murmurHash64("0123456789ABCDEF01234567"), 9980960296277708772ULL);
}

TEST(MurmurHash, PrimaryLoopAndTail) {
  EXPECT_EQ(murmurHash64("0123456789ABCDEF0"), 654503456484488283ULL);
  EXPECT_EQ(murmurHash64("0123456789ABCDEF01"), 10240825431821950816ULL);
  EXPECT_EQ(murmurHash64("0123456789ABCDEF012"), 6811778381211949987ULL);
  EXPECT_EQ(murmurHash64("0123456789ABCDEF0123"), 10791461727592423385ULL);
  EXPECT_EQ(murmurHash64("0123456789ABCDEF01234"), 11236139906480711106ULL);
  EXPECT_EQ(murmurHash64("0123456789ABCDEF012345"), 8264417865430344363ULL);
  EXPECT_EQ(murmurHash64("0123456789ABCDEF0123456"), 2915833106541791378ULL);
}
