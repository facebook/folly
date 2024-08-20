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

#define TEST_CASES(X)                                                      \
  /* Empty string */                                                       \
  X("", UINT64_C(0))                                                       \
  /* Short sequences, no execution of primary loop, only tail handling. */ \
  X("0", UINT64_C(5533571732986600803))                                    \
  X("01", UINT64_C(2988402087957123519))                                   \
  X("012", UINT64_C(18121251311279197961))                                 \
  X("0123", UINT64_C(3086299600550921888))                                 \
  X("01234", UINT64_C(12373468686010462630))                               \
  X("012345", UINT64_C(8037360064841115407))                               \
  X("0123456", UINT64_C(12284635732915976134))                             \
  /* Only primary loop. */                                                 \
  X("01234567", UINT64_C(9778579411364587418))                             \
  X("0123456789ABCDEF", UINT64_C(8277819783762704778))                     \
  X("0123456789ABCDEF01234567", UINT64_C(9980960296277708772))             \
  /* Primary loop and tail. */                                             \
  X("0123456789ABCDEF0", UINT64_C(654503456484488283))                     \
  X("0123456789ABCDEF01", UINT64_C(10240825431821950816))                  \
  X("0123456789ABCDEF012", UINT64_C(6811778381211949987))                  \
  X("0123456789ABCDEF0123", UINT64_C(10791461727592423385))                \
  X("0123456789ABCDEF01234", UINT64_C(11236139906480711106))               \
  X("0123456789ABCDEF012345", UINT64_C(8264417865430344363))               \
  X("0123456789ABCDEF0123456", UINT64_C(2915833106541791378))              \
  /* Sequences with bytes represented as negative chars. */                \
  X("\x80", UINT64_C(13393303071874499911))                                \
  X("\x80\x81", UINT64_C(3896321919913970216))                             \
  X("\x80\x81\x82\x83\x84\x85\x86\x87", UINT64_C(2468552239318681156))     \
  X("\x61\x80\x81\x82\x83\x84\x85\x86\x87\x62", UINT64_C(836019401831928519))

namespace {

constexpr std::uint64_t murmurHash64(std::string_view input) {
  return folly::hash::murmurHash64(input.data(), input.size(), /* seed */ 0);
}

} // namespace

TEST(MurmurHash, Runtime){
#define X(s, expected) EXPECT_EQ(murmurHash64(s), expected);
    TEST_CASES(X)
#undef X
}

TEST(MurmurHash, Constexpr) {
#define X(s, expected)                      \
  {                                         \
    constexpr uint64_t h = murmurHash64(s); \
    static_assert(h == expected);           \
  }

  TEST_CASES(X)
#undef X
}
