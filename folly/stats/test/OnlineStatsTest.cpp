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
#include <folly/stats/OnlineStats.h>
#include <vector>

using folly::OnlineStats;
using std::vector;

TEST(OnlineStatsTest, EmptyDataSet) {
  OnlineStats<double> stats;
  EXPECT_EQ(stats.count(), 0);
  EXPECT_TRUE(std::isnan(stats.mean()));
  EXPECT_TRUE(std::isnan(stats.unbiasedVariance()));
  EXPECT_TRUE(std::isnan(stats.unbiasedStandardDeviation()));
  EXPECT_TRUE(std::isnan(stats.biasedVariance()));
  EXPECT_TRUE(std::isnan(stats.biasedStandardDeviation()));
}

TEST(OnlineStatsTest, Constructor) {
  std::vector<int> v{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  OnlineStats<double> stats(v.begin(), v.end());
  EXPECT_EQ(stats.count(), 10);
  EXPECT_DOUBLE_EQ(stats.mean(), 5.5);
  EXPECT_DOUBLE_EQ(stats.minimum(), 1.0);
  EXPECT_DOUBLE_EQ(stats.maximum(), 10.0);
}

template <typename T>
class OnlineStatsTest : public testing::Test {
 public:
  void SetUp() override {
    for (T value = 1.0; value < 11.0; value += 1.0) {
      stats.add(value);
    }
  }
  OnlineStats<T> stats;
};

using InputDataTypes = ::testing::Types<double, float, long, int, short>;
TYPED_TEST_CASE(OnlineStatsTest, InputDataTypes);

TYPED_TEST(OnlineStatsTest, StatsCalculations) {
  double var, std;

  EXPECT_EQ(this->stats.count(), 10);
  EXPECT_DOUBLE_EQ(this->stats.mean(), 5.5);
  EXPECT_DOUBLE_EQ(this->stats.minimum(), 1.0);
  EXPECT_DOUBLE_EQ(this->stats.maximum(), 10.0);

  // unbiased stats

  EXPECT_DOUBLE_EQ(this->stats.unbiasedVariance(), 8.25);
  EXPECT_DOUBLE_EQ(this->stats.unbiasedStandardDeviation(), 2.8722813232690143);

  std::tie(var, std) = this->stats.unbiasedVarianceStandardDeviation();
  EXPECT_DOUBLE_EQ(var, 8.25);
  EXPECT_DOUBLE_EQ(std, 2.8722813232690143);

  // biased stats

  EXPECT_DOUBLE_EQ(this->stats.biasedVariance(), 9.166666666666666);
  EXPECT_DOUBLE_EQ(this->stats.biasedStandardDeviation(), 3.0276503540974917);

  std::tie(var, std) = this->stats.biasedVarianceStandardDeviation();
  EXPECT_DOUBLE_EQ(var, 9.166666666666666);
  EXPECT_DOUBLE_EQ(std, 3.0276503540974917);
}
