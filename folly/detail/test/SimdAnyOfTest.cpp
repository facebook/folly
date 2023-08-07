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

#include <folly/detail/SimdAnyOf.h>

#include <folly/Range.h>
#include <folly/detail/SimdCharPlatform.h>
#include <folly/portability/GTest.h>

#include <array>

#if FOLLY_DETAIL_HAS_SIMD_CHAR_PLATFORM

namespace folly {
namespace simd_detail {

template <typename Platform, int unrolling>
void anySpacesTestForPlatformUnrolling(folly::StringPiece s, bool expected) {
  bool actual = folly::simd_detail::simdAnyOf<Platform, unrolling>(
      s.data(), s.data() + s.size(), [](typename Platform::reg_t x) {
        return Platform::equal(x, ' ');
      });
  ASSERT_EQ(expected, actual) << s;
}

template <typename Platform>
void anySpacesTestForPlatform(folly::StringPiece s, bool expected) {
  ASSERT_NO_FATAL_FAILURE(
      (anySpacesTestForPlatformUnrolling<Platform, 1>(s, expected)));
  ASSERT_NO_FATAL_FAILURE(
      (anySpacesTestForPlatformUnrolling<Platform, 2>(s, expected)));
  ASSERT_NO_FATAL_FAILURE(
      (anySpacesTestForPlatformUnrolling<Platform, 3>(s, expected)));
  ASSERT_NO_FATAL_FAILURE(
      (anySpacesTestForPlatformUnrolling<Platform, 4>(s, expected)));
}

void anySpacesTest(folly::StringPiece s, bool expected) {
  ASSERT_NO_FATAL_FAILURE(
      anySpacesTestForPlatform<SimdCharPlatform>(s, expected));
#if FOLLY_X64
  ASSERT_NO_FATAL_FAILURE(
      anySpacesTestForPlatform<SimdCharSse2Platform>(s, expected));
#if defined(__AVX2__)
  ASSERT_NO_FATAL_FAILURE(
      anySpacesTestForPlatform<SimdCharAvx2Platform>(s, expected));
#endif
#endif
#if FOLLY_AARCH64
  ASSERT_NO_FATAL_FAILURE(
      anySpacesTestForPlatform<SimdCharAarch64Platform>(s, expected));
#endif
}

// Main tests for this are comming from fuzzing users

TEST(SimdAnyOfSimple, Basic) {
  anySpacesTest("", false);
  anySpacesTest(" ", true);
  anySpacesTest("a", false);

  anySpacesTest("aaaa aaaa", true);
  anySpacesTest("aaaaaaaaaaaa", false);

  anySpacesTest(std::string(15u, 'a'), false);
}

TEST(SimdAnyOfSimple, Ignore) {
  alignas(64) std::array<char, 64> buffer;
  buffer.fill(' ');
  for (auto& c : buffer) {
    c = 'a';
    anySpacesTest({&c, 1}, false);
    c = ' ';
  }
}

TEST(SimdAnyOfSimple, BigChunk) {
  std::string buffer(300, 'a');

  for (std::size_t i = 0; i != 32; ++i) {
    for (std::size_t j = 0; j != 32; ++j) {
      char* f = buffer.data() + i;
      char* l = buffer.data() + buffer.size() - j;

      folly::StringPiece toTest =
          folly::StringPiece{f, static_cast<std::size_t>(l - f)};

      anySpacesTest(toTest, false);

      while (f != l) {
        *f = ' ';
        anySpacesTest(toTest, true);
        *f = 'a';
        ++f;
      }
    }
  }
}

} // namespace simd_detail
} // namespace folly

#endif // FOLLY_DETAIL_HAS_SIMD_CHAR_PLATFORM
