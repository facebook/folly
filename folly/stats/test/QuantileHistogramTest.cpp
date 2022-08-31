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

#include <folly/stats/QuantileHistogram.h>

#include <random>
#include <vector>

#include <folly/Range.h>
#include <folly/portability/GTest.h>

using namespace folly;

template <typename T>
class QuantileHistogramTypedTest : public ::testing::Test {};

using ValueTypes = ::testing::Types<
    QuantileHistogram<>,
    QuantileHistogram<PredefinedQuantiles::MinAndMax>,
    QuantileHistogram<PredefinedQuantiles::Median>,
    QuantileHistogram<PredefinedQuantiles::P01>,
    QuantileHistogram<PredefinedQuantiles::P99>,
    QuantileHistogram<PredefinedQuantiles::MedianAndHigh>,
    QuantileHistogram<PredefinedQuantiles::Quartiles>,
    QuantileHistogram<PredefinedQuantiles::Deciles>,
    QuantileHistogram<PredefinedQuantiles::Ventiles>>;

TYPED_TEST_SUITE(QuantileHistogramTypedTest, ValueTypes);

TYPED_TEST(QuantileHistogramTypedTest, ctor) {
  TypeParam qhist;
  EXPECT_EQ(qhist.min(), 0);
  EXPECT_EQ(qhist.max(), 0);
  EXPECT_EQ(qhist.count(), 0);
  EXPECT_EQ(qhist.estimateQuantile(0), 0);
  EXPECT_EQ(qhist.estimateQuantile(0.5), 0);
  EXPECT_EQ(qhist.estimateQuantile(1.0), 0);
}

TYPED_TEST(QuantileHistogramTypedTest, addValue) {
  TypeParam qhist;

  qhist.addValue(3);
  qhist.addValue(3.7);
  qhist.addValue(7);
  qhist.addValue(3.7);

  EXPECT_EQ(qhist.min(), 3);
  EXPECT_EQ(qhist.max(), 7);
}

TYPED_TEST(QuantileHistogramTypedTest, quantilesAreCloseUniform) {
  std::default_random_engine rng;
  std::uniform_real_distribution<double> dist(0.0, 1.0);

  TypeParam qhist;

  for (size_t i = 0; i < 10000; i++) {
    qhist.addValue(dist(rng));
  }

  for (size_t i = 0; i <= 100; i++) {
    double quantile = static_cast<double>(i) / 100.0;
    double location = qhist.estimateQuantile(quantile);
    ASSERT_NEAR(quantile, location, 0.01);
  }
}

TYPED_TEST(QuantileHistogramTypedTest, quantilesAreCloseNormal) {
  std::default_random_engine rng;
  std::normal_distribution<double> dist(0.0, 1.0);

  TypeParam qhist;

  for (size_t i = 0; i < 100000; i++) {
    qhist.addValue(dist(rng));
  }

  // This works in this simplified form because mean == 0 and stddev == 1.
  auto cdf = [](double value) { return 0.5 * erfc(-value * M_SQRT1_2); };

  for (size_t i = 0; i < qhist.quantiles().size(); i++) {
    double quantile = qhist.quantiles()[i];
    double location = qhist.estimateQuantile(quantile);
    EXPECT_NEAR(cdf(location), quantile, 0.01);
  }
}

TYPED_TEST(QuantileHistogramTypedTest, mergeUnsortedValues) {
  std::default_random_engine rng;
  std::normal_distribution<double> dist(0.0, 1.0);

  TypeParam qhist;

  for (size_t i = 0; i < 1000; i++) {
    qhist.addValue(dist(rng));
  }

  TypeParam toMerge{qhist};

  std::vector<double> vals;
  vals.reserve(1000);
  for (size_t i = 0; i < 1000; i++) {
    double val = dist(rng);
    qhist.addValue(val);
    vals.push_back(val);
  }

  Range<const double*> r{vals.data(), vals.size()};
  TypeParam merged = toMerge.merge(r);

  EXPECT_EQ(qhist.count(), merged.count());
}

TYPED_TEST(QuantileHistogramTypedTest, mergeHistograms) {
  std::default_random_engine rng;
  std::normal_distribution<double> dist(0.0, 1.0);

  std::array<TypeParam, 3> qhists{};

  for (size_t i = 0; i < 10000; i++) {
    qhists[0].addValue(dist(rng));

    qhists[1].addValue(dist(rng));
    qhists[1].addValue(dist(rng));

    qhists[2].addValue(dist(rng));
    qhists[2].addValue(dist(rng));
    qhists[2].addValue(dist(rng));
  }

  TypeParam merged = TypeParam::merge(Range(qhists.data(), qhists.size()));

  auto cdf = [](double value) { return 0.5 * erfc(-value * M_SQRT1_2); };

  for (size_t i = 0; i < merged.quantiles().size(); i++) {
    double quantile = merged.quantiles()[i];
    double location = merged.estimateQuantile(quantile);
    EXPECT_NEAR(cdf(location), quantile, 0.01);
  }
}

TYPED_TEST(QuantileHistogramTypedTest, debugString) {
  std::default_random_engine rng;
  std::normal_distribution<double> dist(0.0, 1.0);
  TypeParam qhist;

  for (size_t i = 0; i < 10000; i++) {
    qhist.addValue(dist(rng));
  }

  auto debugString = qhist.debugString();

  EXPECT_NE(
      debugString.find(folly::to<std::string>(qhist.count())),
      std::string::npos);
  for (auto q : qhist.quantiles()) {
    EXPECT_NE(debugString.find(folly::to<std::string>(q)), std::string::npos);
    EXPECT_NE(
        debugString.find(folly::to<std::string>(qhist.estimateQuantile(q))),
        std::string::npos);
  }
}

TYPED_TEST(QuantileHistogramTypedTest, bimodalStress) {
  std::default_random_engine rng;
  std::uniform_real_distribution<double> uniform(0.0, 1.0);
  std::normal_distribution<double> dist(0.0, 1.0);

  static constexpr size_t kNumTrials = 25;
  static constexpr size_t kNumAdds = 2500;
  static constexpr double kSpread = 10000.0;

  for (size_t trial = 0; trial < kNumTrials; trial++) {
    TypeParam qhist;

    double weightA = uniform(rng);

    double meanA = kSpread * uniform(rng);
    double stddevA = kSpread * uniform(rng);
    std::normal_distribution<double> distA(meanA, stddevA);

    double meanB = kSpread * uniform(rng);
    double stddevB = kSpread * uniform(rng);
    std::normal_distribution<double> distB(meanB, stddevB);

    for (size_t i = 0; i < kNumAdds; i++) {
      if (uniform(rng) < weightA) {
        qhist.addValue(distA(rng));
      } else {
        qhist.addValue(distB(rng));
      }
    }

    EXPECT_EQ(qhist.count(), kNumAdds);
  }
}

TEST(CPUShardedQuantileHistogramTest, stress) {
  static constexpr size_t kNumThreads = 20;
  static constexpr size_t kNumAdds = 2500;
  std::vector<std::thread> threads(kNumThreads);
  CPUShardedQuantileHistogram qhist;

  for (auto& th : threads) {
    th = std::thread([&] {
      std::default_random_engine rng;
      std::uniform_real_distribution<double> uniform(0.0, 1.0);
      for (size_t i = 0; i < kNumAdds; i++) {
        qhist.addValue(uniform(rng));

        // Random flush operation.
        if (uniform(rng) < 0.01) {
          EXPECT_LE(qhist.max(), 1.0);
        }
      }
    });
  }

  for (auto& th : threads) {
    th.join();
  }

  EXPECT_EQ(qhist.count(), kNumThreads * kNumAdds);
}
