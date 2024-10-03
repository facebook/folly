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

#include <folly/detail/SplitStringSimd.h>
#include <folly/detail/SplitStringSimdImpl.h>

#include <folly/FBString.h>
#include <folly/FBVector.h>
#include <folly/Range.h>
#include <folly/portability/GTest.h>
#include <folly/small_vector.h>

#include <random>

namespace folly {
namespace detail {

// making sure that basic scalar works
TEST(SplitStringSimdTest, ByCharScalarKeepEmpty) {
  using pieces = std::vector<folly::StringPiece>;

  auto run = [](folly::StringPiece s) {
    pieces res;
    splitByCharScalar<false>(',', s, res);
    return res;
  };

  ASSERT_EQ(run(""), (pieces{""}));
  ASSERT_EQ(run("a"), (pieces{"a"}));
  ASSERT_EQ(run(",a"), (pieces{"", "a"}));
  ASSERT_EQ(run("a,aa"), (pieces{"a", "aa"}));
  ASSERT_EQ(run("a,,aa"), (pieces{"a", "", "aa"}));
  ASSERT_EQ(run("aaaa,aaa,aa,a"), (pieces{"aaaa", "aaa", "aa", "a"}));
  ASSERT_EQ(run("aaaa,aaa,aa,a,"), (pieces{"aaaa", "aaa", "aa", "a", ""}));
}

TEST(SplitStringSimdTest, ByCharScalarIgnoreEmpty) {
  using pieces = std::vector<folly::StringPiece>;

  auto run = [](folly::StringPiece s) {
    pieces res;
    splitByCharScalar<true>(',', s, res);
    return res;
  };

  ASSERT_EQ(run(""), (pieces{}));
  ASSERT_EQ(run("a"), (pieces{"a"}));
  ASSERT_EQ(run(",a"), (pieces{"a"}));
  ASSERT_EQ(run("a,aa"), (pieces{"a", "aa"}));
  ASSERT_EQ(run("a,,aa"), (pieces{"a", "aa"}));
  ASSERT_EQ(run("aaaa,aaa,aa,a,"), (pieces{"aaaa", "aaa", "aa", "a"}));
  ASSERT_EQ(run(",,,"), (pieces{}));
}

template <bool ignoreEmpty, typename Container>
void testContainerSV(
    folly::StringPiece s, const std::vector<folly::StringPiece>& expected) {
  Container actual;
  simdSplitByChar(',', s, actual, ignoreEmpty);

  ASSERT_EQ(expected.size(), actual.size());

  for (std::size_t i = 0; i != expected.size(); ++i) {
    ASSERT_EQ(expected[i].data(), actual[i].data()) << s << " : " << i;
    ASSERT_EQ(expected[i].size(), actual[i].size()) << s << " : " << i;
  }
}

// shorter name to get 1 line auto format
template <bool ie>
void testAllContainersOfSVs(
    folly::StringPiece s, const std::vector<folly::StringPiece>& expected) {
  testContainerSV<ie, folly::fbvector<folly::StringPiece>>(s, expected);
  testContainerSV<ie, folly::fbvector<std::string_view>>(s, expected);

  testContainerSV<ie, folly::small_vector<folly::StringPiece, 1>>(s, expected);
  testContainerSV<ie, folly::small_vector<folly::StringPiece, 2>>(s, expected);
  testContainerSV<ie, folly::small_vector<folly::StringPiece, 3>>(s, expected);
  testContainerSV<ie, folly::small_vector<folly::StringPiece, 4>>(s, expected);
  testContainerSV<ie, folly::small_vector<folly::StringPiece, 6>>(s, expected);
  testContainerSV<ie, folly::small_vector<folly::StringPiece, 7>>(s, expected);
  testContainerSV<ie, folly::small_vector<folly::StringPiece, 8>>(s, expected);
  static_assert(
      !SimdSplitByCharIsDefinedFor<
          folly::small_vector<folly::StringPiece, 9>>::value,
      "");

  testContainerSV<ie, folly::small_vector<std::string_view, 1>>(s, expected);
  testContainerSV<ie, folly::small_vector<std::string_view, 2>>(s, expected);
  testContainerSV<ie, folly::small_vector<std::string_view, 3>>(s, expected);
  testContainerSV<ie, folly::small_vector<std::string_view, 4>>(s, expected);
  testContainerSV<ie, folly::small_vector<std::string_view, 6>>(s, expected);
  testContainerSV<ie, folly::small_vector<std::string_view, 7>>(s, expected);
  testContainerSV<ie, folly::small_vector<std::string_view, 8>>(s, expected);
  static_assert(
      !SimdSplitByCharIsDefinedFor<
          folly::small_vector<std::string_view, 9>>::value,
      "");
}

template <bool ignoreEmpty, typename Container>
void testContainersOfStrings(
    folly::StringPiece s, const std::vector<folly::StringPiece>& expected) {
  Container actual;
  simdSplitByChar(',', s, actual, ignoreEmpty);

  ASSERT_EQ(expected.size(), actual.size()) << s;

  for (std::size_t i = 0; i != expected.size(); ++i) {
    ASSERT_EQ(expected[i], actual[i]) << s << " : " << i;
  }
}

template <bool ie>
void testAllContainersOfStrings(
    folly::StringPiece s, const std::vector<folly::StringPiece>& expected) {
  testContainersOfStrings<ie, std::vector<std::string>>(s, expected);
  testContainersOfStrings<ie, folly::fbvector<std::string>>(s, expected);

  testContainersOfStrings<ie, std::vector<folly::fbstring>>(s, expected);
  testContainersOfStrings<ie, folly::fbvector<folly::fbstring>>(s, expected);
}

template <bool ignoreEmpty>
void runTestStringSplitOneType(folly::StringPiece s) {
  std::vector<folly::StringPiece> expected;
  splitByCharScalar<ignoreEmpty>(',', s, expected);

  std::vector<std::vector<folly::StringPiece>> actuals;

  actuals.emplace_back();
  simdSplitByChar(',', s, actuals.back(), ignoreEmpty);

#if FOLLY_SSE_PREREQ(4, 2)
  actuals.emplace_back();
  PlatformSimdSplitByChar<
      simd::detail::SimdSse42Platform<std::uint8_t>,
      ignoreEmpty>{}(',', s, actuals.back());
#if defined(__AVX2__)
  actuals.emplace_back();
  PlatformSimdSplitByChar<
      simd::detail::SimdAvx2Platform<std::uint8_t>,
      ignoreEmpty>{}(',', s, actuals.back());
#endif
#endif

#if FOLLY_AARCH64
  actuals.emplace_back();
  PlatformSimdSplitByChar<
      simd::detail::SimdAarch64Platform<std::uint8_t>,
      ignoreEmpty>{}(',', s, actuals.back());
#endif

  for (const auto& actual : actuals) {
    ASSERT_EQ(expected.size(), actual.size()) << s;

    for (std::size_t i = 0; i != expected.size(); ++i) {
      ASSERT_EQ(expected[i].data(), actual[i].data()) << s << " : " << i;
      ASSERT_EQ(expected[i].size(), actual[i].size()) << s << " : " << i;
    }
  }

  testAllContainersOfSVs<ignoreEmpty>(s, expected);
  testAllContainersOfStrings<ignoreEmpty>(s, expected);
}

void runTestStringSplit(folly::StringPiece s) {
  runTestStringSplitOneType<false>(s);
  runTestStringSplitOneType<true>(s);
}

std::string repeat(std::string_view substr, std::size_t n) {
  std::string res;
  while (n--) {
    res.append(substr);
  }
  return res;
}

// We also do fuzzing for covering more cases.
TEST(SplitStringSimd, ByChar) {
  runTestStringSplit("");
  runTestStringSplit(",");
  runTestStringSplit(",,");
  runTestStringSplit(",,,");
  runTestStringSplit(",a,,aa");
  runTestStringSplit("aa,aaa,aaaa");
  runTestStringSplit("aa,aaa,,,aaa,,a");

  runTestStringSplit(repeat("a,", 100));
  runTestStringSplit(repeat("aa,", 100));
  // every char (the bigger cases come from older version)
  runTestStringSplit(repeat(",", 255));
  runTestStringSplit(repeat(",", 512));
  runTestStringSplit(repeat(",", 512 + 16));
  runTestStringSplit(repeat(",", 512 + 32));
  runTestStringSplit(repeat(",", 512 + 64));
  runTestStringSplit(
      repeat(",", std::numeric_limits<std::uint16_t>::max() + 512 + 16));

  // special case: triggered shift right by 32 on uint32
  {
    constexpr std::string_view kTestData = "ong_history_by_pagetype_convr:0,";
    static_assert(kTestData.size() == 32);

    alignas(32) std::array<char, 32> buf;
    std::copy(kTestData.begin(), kTestData.end(), buf.begin());
    runTestStringSplit({buf.data(), buf.size()});
  }
}

TEST(SplitStringSimd, ByCharTestDifferentOffsets) {
  alignas(32) std::array<char, 100> buf;

  std::mt19937 gen;
  std::uniform_int_distribution<> dis(0, 1);
  for (auto& c : buf) {
    c = dis(gen) ? ',' : 'a';
  }

  for (auto f = buf.begin(); f != buf.end(); ++f) {
    for (auto l = f; l != buf.end(); ++l) {
      runTestStringSplit({f, l});
    }
  }
}

} // namespace detail
} // namespace folly
