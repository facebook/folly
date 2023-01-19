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

#include <float.h>
#include <math.h>
#include <glog/logging.h>

#include <folly/ConstexprMath.h>
#include <folly/experimental/Coinflip.h>
#include <folly/portability/GTest.h>

// Source:
// https://stackoverflow.com/questions/2328258/cumulative-normal-distribution-function-in-c-c/18786808#18786808
double normalCDF(double value) {
  return 0.5 * erfc(-value * M_SQRT1_2);
}

bool is_biased(
    int64_t num_successes,
    int64_t num_trials,
    double p,
    double threshold = 1e-6) {
  if (p == 0 || p == 1) {
    // This test won't work on the boundaries, so give a failure result to
    // prompt debugging.
    return true;
  }
  const double x = static_cast<double>(num_trials) / num_successes;
  // mu is the mean of an exponential distribution with parameter p.
  const double mu = 1 / p;
  // sigma is the standard deviation of an exponential distribution with
  // parameter p.
  const double sigma = std::sqrt(1 - p) / p;
  // The calculation for z comes from the central limit theorem. Specifically,
  // when the underlying distribution has mean mu and standard deviation sigma,
  // then sqrt(num_successes) * (x - mu) ~ Normal(0, sigma^2). The z-score is
  // normalized to a Normal(0, 1) distribution.
  const double z = std::sqrt(num_successes) * (x - mu) / sigma;
  const double cdf = normalCDF(z);

  if (cdf < threshold / 2 || cdf > 1 - threshold / 2) {
    return true;
  }

  return false;
}

TEST(coinflip_test, ctor) {
  folly::Coinflip coin0;
}

bool experiment(uint32_t wait, uint32_t num_successes = 1000000) {
  const double p = 1.0 / wait;
  int64_t num_trials = 0;
  uint64_t counter = 0;

  // We're going to cheat and look into the implementation a little so that we
  // can gather enough values to perform a powerful statistical analysis. The
  // idea is that if we know the step size, then we can calculate how many
  // times we would need to call coinflip to possibly get the next true return.
  // We will then jump ahead that many iterations.
  uint64_t step = 0;
  while (step == 0) {
    uint64_t last_counter = counter;
    if (!folly::Coinflip::coinflip(
            /*counter=*/&counter,
            /*wait=*/wait,
            /*rng=*/folly::ThreadLocalPRNG())) {
      if (last_counter != 0) {
        step = last_counter - counter;
      }
    } else if (counter == 0) {
      // The implementation doesn't appear to be doing anything with the
      // counter.
      return false;
    }
  }

  for (uint32_t i = 0; i < num_successes; /* i updated in loop */) {
    if (counter > step) {
      // Avoid the leaving the counter at 0 since the implementation might
      // or might not treat that case specially.
      int64_t num_failures = counter / step - 1;
      counter -= step * num_failures;
      num_trials += num_failures;
    }

    for (uint32_t x = 0; x < 2; x++) {
      // Do this twice because the first iteration might leave counter = 0,
      // which might be handled differently in various implementations.
      if (folly::Coinflip::coinflip(
              /*counter=*/&counter,
              /*wait=*/wait,
              /*rng=*/folly::ThreadLocalPRNG())) {
        i++;
      }
      num_trials++;
    }
  }

  return is_biased(num_successes, num_trials, p);
}

#define TEST_BIAS(WAIT)                                                \
  TEST(coinflip_test, bias_##WAIT) {                                   \
    EXPECT_FALSE(experiment(/*wait=*/WAIT, /*num_successes=*/100000)); \
  }

TEST_BIAS(8)
TEST_BIAS(9)
TEST_BIAS(17)
TEST_BIAS(31)
TEST_BIAS(32)
TEST_BIAS(33)
TEST_BIAS(63)
TEST_BIAS(64)
TEST_BIAS(65)
TEST_BIAS(127)
TEST_BIAS(128)
TEST_BIAS(129)
TEST_BIAS(255)
TEST_BIAS(256)
TEST_BIAS(257)
TEST_BIAS(511)
TEST_BIAS(512)
TEST_BIAS(513)
TEST_BIAS(1023)
TEST_BIAS(1024)
TEST_BIAS(1025)
TEST_BIAS(2047)
TEST_BIAS(2048)
TEST_BIAS(2049)

TEST_BIAS(100)
TEST_BIAS(1000)
TEST_BIAS(10000)
TEST_BIAS(100000)
TEST_BIAS(1000000)
TEST_BIAS(10000000)

TEST_BIAS(UINT32_MAX)

// Bias would creep in if the approximate step is smaller than the true step,
// since that would lead to false negatives. We need to make sure that
// the algorithm used to compute the approximate step is always greater than
// or equal to the precise step.
static constexpr int kScaleBits = 54;
static constexpr uint64_t kScale = 1ull << kScaleBits;
static constexpr uint64_t kStepApproxBase = 1ull << (kScaleBits - 31);

uint64_t step_approx(uint32_t wait) {
  return kStepApproxBase << __builtin_clz(wait - 1);
}

uint64_t step_exact(uint32_t wait) {
  return folly::to_integral(-log(1 - 1.0 / wait) * kScale);
}

TEST(coinflip_test, approx_ge_exact) {
  for (uint32_t power = 2; power < 32; power++) {
    uint64_t wait = 1ull << power;
    ASSERT_GE(step_approx(wait - 1), step_exact(wait - 1));
    ASSERT_GE(step_approx(wait), step_exact(wait));
    ASSERT_GE(step_approx(wait + 1), step_exact(wait + 1));
  }

  ASSERT_GE(step_approx(UINT32_MAX - 1), step_exact(UINT32_MAX - 1));
  ASSERT_GE(step_approx(UINT32_MAX), step_exact(UINT32_MAX));
}

int main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  return RUN_ALL_TESTS();
}
