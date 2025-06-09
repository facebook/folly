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

using namespace std::literals;

constexpr auto cases = std::array{
    std::pair{""sv, UINT64_C(232177599295442350)},
    std::pair{"0"sv, UINT64_C(14193856657648385672)},
    std::pair{"01"sv, UINT64_C(15549595023848265440)},
    std::pair{"012"sv, UINT64_C(14036073547449753364)},
    std::pair{"0123"sv, UINT64_C(2155398448399527240)},
    std::pair{"01234"sv, UINT64_C(11595122963875691922)},
    std::pair{"012345"sv, UINT64_C(12910097366968805346)},
    std::pair{"0123456"sv, UINT64_C(2988730266698498992)},
    std::pair{"01234567"sv, UINT64_C(7570412248888932898)},
    std::pair{"0123456789ABCDEF"sv, UINT64_C(4286119474277594607)},
    std::pair{"0123456789ABCDEF01234567"sv, UINT64_C(6602676763163752414)},
    std::pair{"0123456789ABCDEF0"sv, UINT64_C(12163985545246830987)},
    std::pair{"0123456789ABCDEF01"sv, UINT64_C(17633497820352341844)},
    std::pair{"0123456789ABCDEF012"sv, UINT64_C(5134914024862322698)},
    std::pair{"0123456789ABCDEF0123"sv, UINT64_C(15456488218748233591)},
    std::pair{"0123456789ABCDEF01234"sv, UINT64_C(8219044676438946980)},
    std::pair{"0123456789ABCDEF012345"sv, UINT64_C(2949818754802360919)},
    std::pair{"0123456789ABCDEF0123456"sv, UINT64_C(10100507821488338105)},
    std::pair{
        "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"sv,
        UINT64_C(5986613784938156867)},
    // Sequences with bytes represented as negative chars.
    std::pair{"\x80"sv, UINT64_C(7470186259668200490)},
    std::pair{"\x80\x81"sv, UINT64_C(11863878210592514807)},
    std::pair{
        "\x80\x81\x82\x83\x84\x85\x86\x87"sv, UINT64_C(4054026010566036770)},
    std::pair{
        "\x61\x80\x81\x82\x83\x84\x85\x86\x87\x62"sv,
        UINT64_C(7452325226268640525)},
};

TEST(RapidHash, Runtime) {
  for (auto [in, out] : cases) {
    EXPECT_EQ(out, folly::hash::rapidhash(in.data(), in.size())) << in;
  }
}

TEST(RapidHashMicro, Runtime) {
  for (auto [in, out] : cases) {
    EXPECT_EQ(out, folly::hash::rapidhashMicro(in.data(), in.size())) << in;
  }
}

TEST(RapidHashNano, Runtime) {
  for (auto [in, out] : cases) {
    EXPECT_EQ(out, folly::hash::rapidhashNano(in.data(), in.size())) << in;
  }
}

TEST(RapidHash, Constexpr) {
#define TEST_CASE(i)                                           \
  {                                                            \
    constexpr auto testCase_##i = cases[i];                    \
    constexpr uint64_t h_##i = folly::hash::rapidhash(         \
        testCase_##i.first.data(), testCase_##i.first.size()); \
    static_assert(h_##i == testCase_##i.second);               \
  }

  TEST_CASE(0);
  TEST_CASE(1);
  TEST_CASE(2);
  TEST_CASE(3);
  TEST_CASE(4);
  TEST_CASE(5);
  TEST_CASE(6);
  TEST_CASE(7);
  TEST_CASE(8);
  TEST_CASE(9);
  TEST_CASE(10);
  TEST_CASE(11);
  TEST_CASE(12);
  TEST_CASE(13);
  TEST_CASE(14);
  TEST_CASE(15);
  TEST_CASE(16);
  TEST_CASE(17);
  TEST_CASE(18);
  TEST_CASE(19);
  TEST_CASE(20);
  TEST_CASE(21);
  TEST_CASE(22);
}

TEST(RapidHashMicro, Constexpr) {
#define TEST_CASE_MICRO(i)                                     \
  {                                                            \
    constexpr auto testCase_##i = cases[i];                    \
    constexpr uint64_t h_##i = folly::hash::rapidhashMicro(    \
        testCase_##i.first.data(), testCase_##i.first.size()); \
    static_assert(h_##i == testCase_##i.second);               \
  }

  TEST_CASE_MICRO(0);
  TEST_CASE_MICRO(1);
  TEST_CASE_MICRO(2);
  TEST_CASE_MICRO(3);
  TEST_CASE_MICRO(4);
  TEST_CASE_MICRO(5);
  TEST_CASE_MICRO(6);
  TEST_CASE_MICRO(7);
  TEST_CASE_MICRO(8);
  TEST_CASE_MICRO(9);
  TEST_CASE_MICRO(10);
  TEST_CASE_MICRO(11);
  TEST_CASE_MICRO(12);
  TEST_CASE_MICRO(13);
  TEST_CASE_MICRO(14);
  TEST_CASE_MICRO(15);
  TEST_CASE_MICRO(16);
  TEST_CASE_MICRO(17);
  TEST_CASE_MICRO(18);
  TEST_CASE_MICRO(19);
  TEST_CASE_MICRO(20);
  TEST_CASE_MICRO(21);
  TEST_CASE_MICRO(22);
}

TEST(RapidHashNano, Constexpr) {
#define TEST_CASE_NANO(i)                                      \
  {                                                            \
    constexpr auto testCase_##i = cases[i];                    \
    constexpr uint64_t h_##i = folly::hash::rapidhashNano(     \
        testCase_##i.first.data(), testCase_##i.first.size()); \
    static_assert(h_##i == testCase_##i.second);               \
  }

  TEST_CASE_NANO(0);
  TEST_CASE_NANO(1);
  TEST_CASE_NANO(2);
  TEST_CASE_NANO(3);
  TEST_CASE_NANO(4);
  TEST_CASE_NANO(5);
  TEST_CASE_NANO(6);
  TEST_CASE_NANO(7);
  TEST_CASE_NANO(8);
  TEST_CASE_NANO(9);
  TEST_CASE_NANO(10);
  TEST_CASE_NANO(11);
  TEST_CASE_NANO(12);
  TEST_CASE_NANO(13);
  TEST_CASE_NANO(14);
  TEST_CASE_NANO(15);
  TEST_CASE_NANO(16);
  TEST_CASE_NANO(17);
  TEST_CASE_NANO(18);
  TEST_CASE_NANO(19);
  TEST_CASE_NANO(20);
  TEST_CASE_NANO(21);
  TEST_CASE_NANO(22);
}
