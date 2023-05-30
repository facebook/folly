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

#include <folly/detail/SimpleSimdStringUtils.h>

#include <folly/detail/SimdCharPlatform.h>
#include <folly/detail/SimpleSimdStringUtilsImpl.h>
#include <folly/portability/GTest.h>

namespace folly {
namespace detail {
namespace {

template <typename Platform>
bool hasSpaceOrCntrlSymbolsForPlatform(folly::StringPiece s) {
  return SimpleSimdStringUtilsImpl<Platform>::hasSpaceOrCntrlSymbols(s);
}

void testHasSpaceOrCntrlSymbols(folly::StringPiece s, bool r) {
  ASSERT_EQ(r, simdHasSpaceOrCntrlSymbols(s)) << s;

  using namespace simd_detail;
  ASSERT_EQ(r, hasSpaceOrCntrlSymbolsForPlatform<void>(s)) << s;

#if FOLLY_X64
  ASSERT_EQ(r, hasSpaceOrCntrlSymbolsForPlatform<SimdCharSse2Platform>(s)) << s;
#if defined(__AVX2__)
  ASSERT_EQ(r, hasSpaceOrCntrlSymbolsForPlatform<SimdCharAvx2Platform>(s)) << s;
#endif
#endif

#if FOLLY_AARCH64
  ASSERT_EQ(r, hasSpaceOrCntrlSymbolsForPlatform<SimdCharAarch64Platform>(s))
      << s;
#endif
}

// We also substantially rely on fuzzers for our testing

TEST(SpaceOrCntrl, One) {
  testHasSpaceOrCntrlSymbols("!", false);
}

TEST(SpaceOrCntrl, EachSymbol) {
  for (std::uint16_t uChar = 0;
       uChar <= std::numeric_limits<std::uint8_t>::max();
       ++uChar) {
    bool expected = std::isspace(uChar) || std::iscntrl(uChar);

    char c = static_cast<char>(uChar);
    ASSERT_NO_FATAL_FAILURE(testHasSpaceOrCntrlSymbols({&c, 1u}, expected));
  }
}

TEST(SpaceOrCntrl, ExplicitExamples) {
  using TestCase = std::pair<std::string, bool>;

  std::pair<std::string, bool> testCases[] = {
      TestCase{" ", true},
      TestCase{"123\t", true},
      TestCase{"123", false},
      TestCase{"abA__11", false},
      TestCase{std::string("123") + char(1), true},
  };
  for (const auto& testCase : testCases) {
    ASSERT_NO_FATAL_FAILURE(
        testHasSpaceOrCntrlSymbols(testCase.first, testCase.second));
  }
}

} // namespace
} // namespace detail
} // namespace folly
