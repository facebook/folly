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

#include <folly/algorithm/simd/detail/SimdForEach.h>

#include <folly/portability/GTest.h>

#include <array>

namespace folly {
namespace simd::detail {

constexpr int kCardinal = 4;

template <bool kSameUnrollValue>
struct TestDelegate {
  char* stopAt = nullptr;

  template <typename N>
  bool step(char* s, ignore_extrema ignore, N unroll_i) const {
    int middle = kCardinal - ignore.first - ignore.last;
    while (ignore.first--) {
      EXPECT_EQ(*s, 0);
      *s++ = 'i';
    }
    while (middle--) {
      EXPECT_EQ(*s, 0);
      if (kSameUnrollValue) {
        *s++ = 'a';
      } else {
        *s++ = 'a' + unroll_i();
      }
    }
    while (ignore.last--) {
      *s++ = 'i';
    }

    return stopAt != nullptr && s > stopAt;
  }

  template <typename N>
  bool step(char* s, ignore_none, N unroll_i) const {
    for (int i = 0; i != kCardinal; ++i) {
      EXPECT_EQ(*s, 0);
      if (kSameUnrollValue) {
        *s++ = 'a';
      } else {
        *s++ = 'a' + unroll_i();
      }
    }
    return stopAt != nullptr && s > stopAt;
  }

  template <std::size_t unroll>
  bool unrolledStep(std::array<char*, unroll> unrolled) {
    return detail::UnrollUtils::unrollUntil<static_cast<int>(unroll)>(
        [&](auto unrollI) {
          return step(
              unrolled[unrollI()],
              ignore_none{},
              folly::index_constant<decltype(unrollI)::value + ('A' - 'a')>{});
        });
  }
};

template <int unroll, bool kSameUnrollValue = false>
std::string run(int offset, int len, int stopAt) {
  alignas(64) std::array<char, 100u> buf;
  buf.fill(0);

  TestDelegate<kSameUnrollValue> delegate{
      stopAt == -1 ? nullptr : buf.data() + stopAt};
  simdForEachAligning<unroll>(
      kCardinal, buf.data() + offset, buf.data() + offset + len, delegate);
  return std::string(buf.data());
}

std::string runAllUnrolls(int offset, int len, int stopAt) {
  std::string res = run<1, /*kSameUnrollValue*/ true>(offset, len, stopAt);
  EXPECT_EQ(res, (run<2, /*kSameUnrollValue*/ true>(offset, len, stopAt)));
  EXPECT_EQ(res, (run<3, /*kSameUnrollValue*/ true>(offset, len, stopAt)));
  EXPECT_EQ(res, (run<4, /*kSameUnrollValue*/ true>(offset, len, stopAt)));
  return res;
}

TEST(SimdForEachAligningTest, Tails) {
  ASSERT_EQ("", runAllUnrolls(0, 0, -1));
  ASSERT_EQ("", runAllUnrolls(1, 0, -1));
  ASSERT_EQ("", runAllUnrolls(2, 0, -1));
  ASSERT_EQ("", runAllUnrolls(3, 0, -1));

  ASSERT_EQ("aiii", runAllUnrolls(0, 1, -1));
  ASSERT_EQ("iaii", runAllUnrolls(1, 1, -1));
  ASSERT_EQ("iiai", runAllUnrolls(2, 1, -1));
  ASSERT_EQ("iiia", runAllUnrolls(3, 1, -1));

  ASSERT_EQ("aaii", runAllUnrolls(0, 2, -1));
  ASSERT_EQ("iaai", runAllUnrolls(1, 2, -1));
  ASSERT_EQ("iiaa", runAllUnrolls(2, 2, -1));
  ASSERT_EQ("iiiaaiii", runAllUnrolls(3, 2, -1));

  ASSERT_EQ("aaai", runAllUnrolls(0, 3, -1));
  ASSERT_EQ("iaaa", runAllUnrolls(1, 3, -1));
  ASSERT_EQ("iiaaaiii", runAllUnrolls(2, 3, -1));
  ASSERT_EQ("iiiaaaii", runAllUnrolls(3, 3, -1));

  ASSERT_EQ("aaaa", runAllUnrolls(0, 4, -1));
  ASSERT_EQ("iaaaaiii", runAllUnrolls(1, 4, -1));
  ASSERT_EQ("iiaaaaii", runAllUnrolls(2, 4, -1));
  ASSERT_EQ("iiiaaaai", runAllUnrolls(3, 4, -1));

  ASSERT_EQ("aaaaaiii", runAllUnrolls(0, 5, -1));
  ASSERT_EQ("iaaaaaii", runAllUnrolls(1, 5, -1));
  ASSERT_EQ("iiaaaaai", runAllUnrolls(2, 5, -1));
  ASSERT_EQ("iiiaaaaa", runAllUnrolls(3, 5, -1));
}

TEST(SimdForEachAligningTest, Large) {
  ASSERT_EQ(
      "aaaa"
      "aaaa"
      "aaaa"
      "aaaa"
      "aaii",
      runAllUnrolls(0, 18, -1));
  ASSERT_EQ(
      "iaaa"
      "aaaa"
      "aaaa"
      "aaaa"
      "aaai",
      runAllUnrolls(1, 18, -1));
  ASSERT_EQ(
      "iiaa"
      "aaaa"
      "aaaa"
      "aaaa"
      "aaaa",
      runAllUnrolls(2, 18, -1));
  ASSERT_EQ(
      "iiia"
      "aaaa"
      "aaaa"
      "aaaa"
      "aaaa"
      "aiii",
      runAllUnrolls(3, 18, -1));
}

TEST(SimdForEachAligningTest, Stops) {
  for (int i = 0; i != 4; ++i) {
    ASSERT_EQ("aaaa", runAllUnrolls(0, 18, i));
  }
  for (int i = 0; i != 4; ++i) {
    ASSERT_EQ(
        "aaaa"
        "aaaa",
        runAllUnrolls(0, 18, 4 + i));
  }
  for (int i = 0; i != 4; ++i) {
    ASSERT_EQ(
        "aaaa"
        "aaaa"
        "aaaa"
        "aaaa"
        "aaii",
        runAllUnrolls(0, 18, 16 + i));
  }
}

TEST(SimdForEachAligningTest, UnrollIndexes) {
  ASSERT_EQ(
      "iaaa"
      "aaaa"
      "aaaa",
      run<1>(1, 11, -1));

  ASSERT_EQ(
      "iaaa"
      "aaaa"
      "bbbb",
      run<2>(1, 11, -1));
  ASSERT_EQ(
      "iaaa"
      "aaaa"
      "bbbb"
      "aaii",
      run<2>(1, 13, -1));
  ASSERT_EQ(
      "iaaa"
      "aaaa"
      "bbbb"
      "AAAA"
      "BBBB",
      run<2>(1, 19, -1));
  ASSERT_EQ(
      "iaaa"
      "aaaa"
      "bbbb"
      "AAAA"
      "BBBB"
      "aaaa"
      "aiii",
      run<2>(1, 24, -1));

  ASSERT_EQ(
      "iaaa"
      "aaaa"
      "bbbb",
      run<3>(1, 11, -1));
  ASSERT_EQ(
      "iaaa"
      "aaaa"
      "bbbb"
      "cccc"
      "aaaa",
      run<3>(1, 19, -1));
  ASSERT_EQ(
      "aaaa"
      "aaaa"
      "bbbb"
      "cccc"
      "AAAA"
      "BBBB"
      "CCCC",
      run<3>(0, 28, -1));
  ASSERT_EQ(
      "iaaa"
      "aaaa"
      "bbbb"
      "cccc"
      "AAAA"
      "BBBB"
      "CCCC"
      "aaii",
      run<3>(1, 29, -1));

  ASSERT_EQ(
      "aaaa"
      "aaaa"
      "bbbb"
      "cccc"
      "dddd"
      "aiii",
      run<4>(0, 21, -1));
  ASSERT_EQ(
      "aaaa"
      "aaaa"
      "bbbb"
      "cccc"
      "dddd"
      "AAAA"
      "BBBB"
      "CCCC"
      "DDDD"
      "aiii",
      run<4>(0, 37, -1));
}

} // namespace simd::detail
} // namespace folly
