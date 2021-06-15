/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <folly/portability/GTest.h>

#include <folly/experimental/coro/GtestHelpers.h>

using namespace ::testing;

namespace {
folly::coro::Task<int> co_getInt(int x) {
  co_return x;
}

struct GtestHelpersMultiplicationTestParam {
  int x;
  int y;
  int expectedProduct;
};
} // namespace

class GtestHelpersMultiplicationTest
    : public TestWithParam<GtestHelpersMultiplicationTestParam> {};

CO_TEST_P(GtestHelpersMultiplicationTest, BasicTest) {
  const auto& param = GetParam();
  int product = (co_await co_getInt(param.x)) * (co_await co_getInt(param.y));

  EXPECT_EQ(product, param.expectedProduct);
}

INSTANTIATE_TEST_CASE_P(
    GtestHelpersMultiplicationTest,
    GtestHelpersMultiplicationTest,
    ValuesIn(std::vector<GtestHelpersMultiplicationTestParam>{
        {1, 1, 1},
        {1, 2, 2},
        {2, 2, 4},
        {-1, -6, 6},
    }));
