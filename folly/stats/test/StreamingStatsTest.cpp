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

#include <folly/stats/StreamingStats.h>

#include <vector>

#include <folly/portability/GTest.h>

using folly::StreamingStats;

TEST(StreamingStatsTest, EmptyDataSet) {
  StreamingStats<int, double> stats;
  EXPECT_EQ(stats.count(), 0);
  EXPECT_THROW(stats.minimum(), std::logic_error);
  EXPECT_THROW(stats.maximum(), std::logic_error);
  EXPECT_THROW(stats.mean(), std::logic_error);
  EXPECT_THROW(stats.populationVariance(), std::logic_error);
  EXPECT_THROW(stats.populationStandardDeviation(), std::logic_error);
  EXPECT_THROW(stats.sampleVariance(), std::logic_error);
  EXPECT_THROW(stats.sampleStandardDeviation(), std::logic_error);
}

TEST(StreamingStatsTest, Constructor) {
  std::vector<int> v{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  StreamingStats<int, double> stats(v.begin(), v.end());
  EXPECT_EQ(stats.count(), 10);
  EXPECT_DOUBLE_EQ(stats.mean(), 5.5);
  EXPECT_DOUBLE_EQ(stats.minimum(), 1.0);
  EXPECT_DOUBLE_EQ(stats.maximum(), 10.0);
}

TEST(StreamingStatsTest, ConstructFromValues) {
  StreamingStats<double> stats1;
  for (double i = 0.0; i < 100; i++) {
    stats1.add(i);
  }

  StreamingStats<double> stats2(stats1.state());
  EXPECT_EQ(stats1.count(), stats2.count());
  EXPECT_EQ(stats1.mean(), stats2.mean());
  EXPECT_EQ(stats1.m2(), stats2.m2());
  EXPECT_EQ(stats1.minimum(), stats2.minimum());
  EXPECT_EQ(stats1.maximum(), stats2.maximum());
}

template <typename SampleDataType>
class StreamingStatsTest : public testing::Test {
 public:
  void SetUp() override {
    for (SampleDataType value = 1; value < 11; value += 1) {
      stats.add(value);
    }
  }
  StreamingStats<SampleDataType, double> stats;
};

using InputDataTypes = ::testing::Types<double, float, long, int, short>;
TYPED_TEST_SUITE(StreamingStatsTest, InputDataTypes);

TYPED_TEST(StreamingStatsTest, StatsCalculations) {
  EXPECT_EQ(this->stats.count(), 10);
  EXPECT_DOUBLE_EQ(this->stats.mean(), 5.5);
  EXPECT_DOUBLE_EQ(this->stats.minimum(), 1.0);
  EXPECT_DOUBLE_EQ(this->stats.maximum(), 10.0);

  EXPECT_DOUBLE_EQ(this->stats.populationVariance(), 8.25);
  EXPECT_DOUBLE_EQ(
      this->stats.populationStandardDeviation(), 2.8722813232690143);

  EXPECT_DOUBLE_EQ(this->stats.sampleVariance(), 9.166666666666666);
  EXPECT_DOUBLE_EQ(this->stats.sampleStandardDeviation(), 3.0276503540974917);
}
