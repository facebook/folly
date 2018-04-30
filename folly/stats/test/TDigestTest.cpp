/*
 * Copyright 2013-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <folly/stats/TDigest.h>

#include <chrono>
#include <random>

#include <folly/portability/GTest.h>

using namespace folly;

/*
 * Tests run at a reasonable speed with these settings, but it is good to
 * occasionally test with kNumRandomRuns = 3000.
 */
const int32_t kNumSamples = 50000;
const int32_t kNumRandomRuns = 30;
const int32_t kSeed = 0;

TEST(TDigest, Basic) {
  TDigest digest(100);

  std::vector<double> values;
  for (int i = 1; i <= 100; ++i) {
    values.push_back(i);
  }

  digest = digest.merge(values);

  EXPECT_EQ(100, digest.count());
  EXPECT_EQ(5050, digest.sum());
  EXPECT_EQ(50.5, digest.mean());

  EXPECT_EQ(0.6, digest.estimateQuantile(0.001));
  EXPECT_EQ(2.0 - 0.5, digest.estimateQuantile(0.01));
  EXPECT_EQ(50.375, digest.estimateQuantile(0.5));
  EXPECT_EQ(100.0 - 0.5, digest.estimateQuantile(0.99));
  EXPECT_EQ(100.4, digest.estimateQuantile(0.999));
}

TEST(TDigest, Merge) {
  TDigest digest(100);

  std::vector<double> values;
  for (int i = 1; i <= 100; ++i) {
    values.push_back(i);
  }
  digest = digest.merge(values);

  values.clear();
  for (int i = 101; i <= 200; ++i) {
    values.push_back(i);
  }
  digest = digest.merge(values);

  EXPECT_EQ(200, digest.count());
  EXPECT_EQ(20100, digest.sum());
  EXPECT_EQ(100.5, digest.mean());

  EXPECT_EQ(0.7, digest.estimateQuantile(0.001));
  EXPECT_EQ(4.0 - 1.5, digest.estimateQuantile(0.01));
  EXPECT_EQ(100.25, digest.estimateQuantile(0.5));
  EXPECT_EQ(200.0 - 1.5, digest.estimateQuantile(0.99));
  EXPECT_EQ(200.3, digest.estimateQuantile(0.999));
}

TEST(TDigest, MergeSmall) {
  TDigest digest(100);

  std::vector<double> values;
  values.push_back(1);

  digest = digest.merge(values);

  EXPECT_EQ(1, digest.count());
  EXPECT_EQ(1, digest.sum());
  EXPECT_EQ(1, digest.mean());

  EXPECT_EQ(1.0, digest.estimateQuantile(0.001));
  EXPECT_EQ(1.0, digest.estimateQuantile(0.01));
  EXPECT_EQ(1.0, digest.estimateQuantile(0.5));
  EXPECT_EQ(1.0, digest.estimateQuantile(0.99));
  EXPECT_EQ(1.0, digest.estimateQuantile(0.999));
}

TEST(TDigest, MergeLarge) {
  TDigest digest(100);

  std::vector<double> values;
  for (int i = 1; i <= 1000; ++i) {
    values.push_back(i);
  }
  digest = digest.merge(values);

  EXPECT_EQ(1000, digest.count());
  EXPECT_EQ(500500, digest.sum());
  EXPECT_EQ(500.5, digest.mean());

  EXPECT_EQ(1.5, digest.estimateQuantile(0.001));
  EXPECT_EQ(10.5, digest.estimateQuantile(0.01));
  EXPECT_EQ(500.25, digest.estimateQuantile(0.5));
  EXPECT_EQ(990.25, digest.estimateQuantile(0.99));
  EXPECT_EQ(999.5, digest.estimateQuantile(0.999));
}

TEST(TDigest, MergeLargeAsDigests) {
  std::vector<TDigest> digests;
  TDigest digest(100);

  std::vector<double> values;
  for (int i = 1; i <= 1000; ++i) {
    values.push_back(i);
  }
  // Ensure that the values do not monotonically increase across digests.
  std::random_shuffle(values.begin(), values.end());
  for (int i = 0; i < 10; ++i) {
    std::vector<double> sorted(
        values.begin() + (i * 100), values.begin() + (i + 1) * 100);
    std::sort(sorted.begin(), sorted.end());
    digests.push_back(digest.merge(sorted));
  }

  digest = TDigest::merge(digests);

  EXPECT_EQ(1000, digest.count());
  EXPECT_EQ(500500, digest.sum());
  EXPECT_EQ(500.5, digest.mean());

  EXPECT_EQ(1.5, digest.estimateQuantile(0.001));
  EXPECT_EQ(10.5, digest.estimateQuantile(0.01));
  EXPECT_EQ(990.25, digest.estimateQuantile(0.99));
  EXPECT_EQ(999.5, digest.estimateQuantile(0.999));
}

class DistributionTest
    : public ::testing::TestWithParam<
          std::tuple<std::pair<bool, size_t>, double, bool>> {};

TEST_P(DistributionTest, ReasonableError) {
  std::pair<bool, size_t> underlyingDistribution;
  bool logarithmic;
  size_t modes;
  double quantile;
  double reasonableError = 0;
  bool digestMerge;

  std::tie(underlyingDistribution, quantile, digestMerge) = GetParam();

  std::tie(logarithmic, modes) = underlyingDistribution;
  if (quantile == 0.001 || quantile == 0.999) {
    reasonableError = 0.0005;
  } else if (quantile == 0.01 || quantile == 0.99) {
    reasonableError = 0.005;
  } else if (quantile == 0.25 || quantile == 0.5 || quantile == 0.75) {
    reasonableError = 0.02;
  }

  std::vector<double> errors;

  std::default_random_engine generator;
  generator.seed(kSeed);
  for (size_t iter = 0; iter < kNumRandomRuns; ++iter) {
    TDigest digest(100);

    std::vector<double> values;

    if (logarithmic) {
      std::lognormal_distribution<double> distribution(0.0, 1.0);

      for (size_t i = 0; i < kNumSamples; ++i) {
        auto mode = (int)distribution(generator) % modes;
        values.push_back(distribution(generator) + 100.0 * mode);
      }
    } else {
      std::uniform_int_distribution<int> distributionPicker(0, modes - 1);

      std::vector<std::normal_distribution<double>> distributions;
      for (size_t i = 0; i < modes; ++i) {
        distributions.emplace_back(100.0 * (i + 1), 25);
      }

      for (size_t i = 0; i < kNumSamples; ++i) {
        auto distributionIdx = distributionPicker(generator);
        values.push_back(distributions[distributionIdx](generator));
      }
    }

    std::vector<TDigest> digests;
    for (size_t i = 0; i < kNumSamples / 1000; ++i) {
      auto it_l = values.begin() + (i * 1000);
      auto it_r = it_l + 1000;
      std::sort(it_l, it_r);
      folly::Range<const double*> r(values, i * 1000, 1000);
      if (digestMerge) {
        digests.push_back(digest.merge(r));
      } else {
        digest = digest.merge(r);
      }
    }

    std::sort(values.begin(), values.end());

    if (digestMerge) {
      digest = TDigest::merge(digests);
    }

    double est = digest.estimateQuantile(quantile);
    auto it = std::lower_bound(values.begin(), values.end(), est);
    int32_t actualRank = std::distance(values.begin(), it);
    double actualQuantile = ((double)actualRank) / kNumSamples;
    errors.push_back(actualQuantile - quantile);
  }

  double sum = 0.0;

  for (auto error : errors) {
    sum += error;
  }

  double mean = sum / kNumRandomRuns;

  double numerator = 0.0;
  for (auto error : errors) {
    numerator += pow(error - mean, 2);
  }

  double stddev = std::sqrt(numerator / (kNumRandomRuns - 1));

  EXPECT_GE(reasonableError, stddev);
}

INSTANTIATE_TEST_CASE_P(
    ReasonableErrors,
    DistributionTest,
    ::testing::Combine(
        ::testing::Values(
            std::make_pair(true, 1),
            std::make_pair(true, 3),
            std::make_pair(false, 1),
            std::make_pair(false, 10)),
        ::testing::Values(0.001, 0.01, 0.25, 0.50, 0.75, 0.99, 0.999),
        ::testing::Bool()));
