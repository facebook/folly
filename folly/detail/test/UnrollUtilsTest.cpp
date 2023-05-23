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

#include <folly/detail/UnrollUtils.h>

#include <folly/portability/GTest.h>

#include <array>

namespace folly::detail {

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

template <int stopAt>
struct UnrollUntilTestOp {
  int* lastStep;

  template <int i>
  constexpr bool operator()(UnrollStep<i>) const {
    *lastStep = i;
    return i == stopAt;
  }
};

template <int N, int stopAt, bool expectedRes, int expectedLastStep>
constexpr bool unrollUntilTest() {
  int lastStep = -1;
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
          /*ExpectedLastStep*/ -1>(),
      "");
  static_assert(
      unrollUntilTest<
          /*N*/ 0,
          /*stopAt*/ 1,
          /*expectedRes*/ false,
          /*ExpectedLastStep*/ -1>(),
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

} // namespace folly::detail
