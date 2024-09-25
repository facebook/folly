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

#include <folly/algorithm/simd/detail/UnrollUtils.h>

#include <folly/portability/GTest.h>

#include <array>

namespace folly::simd::detail {

TEST(UnrollUtilsTest, ArrayMap) {
  constexpr std::array<int, 3> in = {1, 2, 3};

  struct {
    constexpr double operator()(int x) { return x + 1; }
  } constexpr op;

  constexpr auto actual = UnrollUtils::arrayMap(in, op);
  constexpr std::array<double, 3> expected{2.0, 3.0, 4.0};

  // non constexpr std::array operator== for older standards
  static_assert(expected[0] == actual[0], "");
  static_assert(expected[1] == actual[1], "");
  static_assert(expected[2] == actual[2], "");
}

template <int... values>
constexpr int reduceValues() {
  std::array<int, sizeof...(values)> arr{values...};
  return UnrollUtils::arrayReduce(arr, std::plus<>{});
}

TEST(UnrollUtilsTest, ArrayReduce) {
  static_assert(1 == reduceValues<1>(), "");
  static_assert(3 == reduceValues<1, 2>(), "");
  static_assert(4 == reduceValues<1, 0, 3>(), "");
  static_assert(10 == reduceValues<1, 2, 3, 4>(), "");
}

template <std::size_t stopAt>
struct UnrollUntilTestOp {
  std::size_t* lastStep;

  template <std::size_t i>
  constexpr bool operator()(folly::index_constant<i>) const {
    *lastStep = i;
    return i == stopAt;
  }
};

constexpr std::size_t kNoStop = std::numeric_limits<std::size_t>::max();

template <
    std::size_t N,
    std::size_t stopAt,
    bool expectedRes,
    std::size_t expectedLastStep>
constexpr bool unrollUntilTest() {
  std::size_t lastStep = kNoStop;
  UnrollUntilTestOp<stopAt> op{&lastStep};
  bool res = UnrollUtils::unrollUntil<N>(op);

  return (res == expectedRes) && (lastStep == expectedLastStep);
}

TEST(UnrollUtilsTest, UnrollUntil) {
  static_assert(
      unrollUntilTest<
          /*N*/ 0,
          /*stopAt*/ 0,
          /*expectedRes*/ false,
          /*ExpectedLastStep*/ kNoStop>(),
      "");
  static_assert(
      unrollUntilTest<
          /*N*/ 0,
          /*stopAt*/ 1,
          /*expectedRes*/ false,
          /*ExpectedLastStep*/ kNoStop>(),
      "");
  static_assert(
      unrollUntilTest<
          /*N*/ 3,
          /*stopAt*/ 1,
          /*expectedRes*/ true,
          /*ExpectedLastStep*/ 1>(),
      "");
  static_assert(
      unrollUntilTest<
          /*N*/ 3,
          /*stopAt*/ 4,
          /*expectedRes*/ false,
          /*ExpectedLastStep*/ 2>(),
      "");
  static_assert(
      unrollUntilTest<
          /*N*/ 5,
          /*stopAt*/ 3,
          /*expectedRes*/ true,
          /*ExpectedLastStep*/ 3>(),
      "");
}

} // namespace folly::simd::detail
