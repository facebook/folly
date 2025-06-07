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

#include <folly/hash/rapidhash.h>

#include <folly/portability/GTest.h>

#define TEST_CASES(X)                                                  \
  X("", UINT64_C(232177599295442350))                                  \
  X("0", UINT64_C(14193856657648385672))                               \
  X("01", UINT64_C(15549595023848265440))                              \
  X("012", UINT64_C(14036073547449753364))                             \
  X("0123", UINT64_C(2155398448399527240))                             \
  X("01234", UINT64_C(11595122963875691922))                           \
  X("012345", UINT64_C(12910097366968805346))                          \
  X("0123456", UINT64_C(2988730266698498992))                          \
  X("01234567", UINT64_C(7570412248888932898))                         \
  X("0123456789ABCDEF", UINT64_C(4286119474277594607))                 \
  X("0123456789ABCDEF01234567", UINT64_C(6602676763163752414))         \
  X("0123456789ABCDEF0", UINT64_C(12163985545246830987))               \
  X("0123456789ABCDEF01", UINT64_C(17633497820352341844))              \
  X("0123456789ABCDEF012", UINT64_C(5134914024862322698))              \
  X("0123456789ABCDEF0123", UINT64_C(15456488218748233591))            \
  X("0123456789ABCDEF01234", UINT64_C(8219044676438946980))            \
  X("0123456789ABCDEF012345", UINT64_C(2949818754802360919))           \
  X("0123456789ABCDEF0123456", UINT64_C(10100507821488338105))         \
  X("0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF",                \
    UINT64_C(5986613784938156867))                                     \
  /* Sequences with bytes represented as negative chars. */            \
  X("\x80", UINT64_C(7470186259668200490))                             \
  X("\x80\x81", UINT64_C(11863878210592514807))                        \
  X("\x80\x81\x82\x83\x84\x85\x86\x87", UINT64_C(4054026010566036770)) \
  X("\x61\x80\x81\x82\x83\x84\x85\x86\x87\x62", UINT64_C(7452325226268640525))

namespace {

constexpr std::uint64_t rapidhash(std::string_view input) {
  return folly::hash::rapidhash(input.data(), input.size());
}

constexpr std::uint64_t rapidhashMicro(std::string_view input) {
  return folly::hash::rapidhashMicro(input.data(), input.size());
}

constexpr std::uint64_t rapidhashNano(std::string_view input) {
  return folly::hash::rapidhashNano(input.data(), input.size());
}

} // namespace

TEST(RapidHash, Runtime){
#define X(s, expected) EXPECT_EQ(rapidhash(s), expected);
    TEST_CASES(X)
#undef X
}

TEST(RapidHashMicro, Runtime){
#define X(s, expected) EXPECT_EQ(rapidhashMicro(s), expected);
    TEST_CASES(X)
#undef X
}

TEST(RapidHashNano, Runtime){
#define X(s, expected) EXPECT_EQ(rapidhashNano(s), expected);
    TEST_CASES(X)
#undef X
}

TEST(RapidHash, Constexpr){
#define X(s, expected)                   \
  {                                      \
    constexpr uint64_t h = rapidhash(s); \
    static_assert(h == expected);        \
  }

    TEST_CASES(X)
#undef X
}

TEST(RapidHashMicro, Constexpr){
#define X(s, expected)                        \
  {                                           \
    constexpr uint64_t h = rapidhashMicro(s); \
    static_assert(h == expected);             \
  }

    TEST_CASES(X)
#undef X
}

TEST(RapidHashNano, Constexpr) {
#define X(s, expected)                       \
  {                                          \
    constexpr uint64_t h = rapidhashNano(s); \
    static_assert(h == expected);            \
  }

  TEST_CASES(X)
#undef X
}
