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

#include <folly/detail/BenchmarkAdaptive.h>

#include <folly/portability/GTest.h>

#include <cmath>

using namespace folly::detail;
using namespace std::chrono_literals;
using folly::UserCounters;
using folly::UserMetric;

// --- SortedSamples tests ---

TEST(SortedSamplesTest, SingleSample) {
  SortedSamples s({42.0});
  EXPECT_EQ(s.size(), 1);
  EXPECT_DOUBLE_EQ(s.percentile(0), 42.0);
  EXPECT_DOUBLE_EQ(s.percentile(50), 42.0);
  EXPECT_DOUBLE_EQ(s.percentile(100), 42.0);
}

TEST(SortedSamplesTest, PercentileInterpolation) {
  // 5 samples: 10, 20, 30, 40, 50
  SortedSamples s({30, 10, 50, 20, 40});

  // pos = (5-1) * pct / 100 = 4 * pct / 100
  EXPECT_DOUBLE_EQ(s.percentile(0), 10.0);
  EXPECT_DOUBLE_EQ(s.percentile(25), 20.0);
  EXPECT_DOUBLE_EQ(s.percentile(50), 30.0);
  EXPECT_DOUBLE_EQ(s.percentile(75), 40.0);
  EXPECT_DOUBLE_EQ(s.percentile(100), 50.0);

  // Interpolation: pct=12.5 -> pos=0.5 -> 10*0.5 + 20*0.5 = 15
  EXPECT_DOUBLE_EQ(s.percentile(12.5), 15.0);
  EXPECT_DOUBLE_EQ(s.percentile(37.5), 25.0);
}

TEST(SortedSamplesTest, EmptySamplesDeath) {
  SortedSamples s({});
  EXPECT_EQ(s.size(), 0);
  EXPECT_DEATH(s.percentile(50), "percentile requires at least 1 sample");
}

TEST(SortedSamplesTest, PercentileCIBasic) {
  // 10 samples: 1, 2, ..., 10
  std::vector<double> data;
  for (int i = 1; i <= 10; ++i) {
    data.push_back(i);
  }
  SortedSamples s(data);

  // Median (p50) should be around 5.5
  auto ci = s.percentileCI(50.0);
  EXPECT_NEAR(ci.estimate, 5.5, 0.01);

  // CI should contain the estimate
  EXPECT_LE(ci.lo, ci.estimate);
  EXPECT_GE(ci.hi, ci.estimate);

  // With 10 uniform samples, CI should be non-trivial
  EXPECT_GT(ci.relWidth(), 0.0);
}

TEST(SortedSamplesTest, PercentileCIExtremesCollapse) {
  // At p=0 and p=100, SE is 0 so CI should collapse to a point
  SortedSamples s({1.0, 2.0, 3.0, 4.0, 5.0});

  auto ci0 = s.percentileCI(0.0);
  EXPECT_DOUBLE_EQ(ci0.lo, 1.0);
  EXPECT_DOUBLE_EQ(ci0.hi, 1.0);
  EXPECT_DOUBLE_EQ(ci0.estimate, 1.0);

  auto ci100 = s.percentileCI(100.0);
  EXPECT_DOUBLE_EQ(ci100.lo, 5.0);
  EXPECT_DOUBLE_EQ(ci100.hi, 5.0);
  EXPECT_DOUBLE_EQ(ci100.estimate, 5.0);
}

TEST(SortedSamplesTest, RelWidthWithZeroEstimate) {
  // relWidth returns 100.0 ("we know nothing") for zero estimate
  SortedSamples s({0.0, 0.0, 0.0});
  auto ci = s.percentileCI(50.0);
  EXPECT_DOUBLE_EQ(ci.estimate, 0.0);
  EXPECT_DOUBLE_EQ(ci.relWidth(), 100.0);
}

TEST(SortedSamplesTest, PercentileCIRelWidthWithTooFewSamples) {
  // percentileCIRelWidth returns 100.0 for < 2 samples
  SortedSamples s({42.0});
  EXPECT_DOUBLE_EQ(s.percentileCIRelWidth(50.0), 100.0);
}

TEST(SortedSamplesTest, PercentileCIRequiresTwoSamples) {
  SortedSamples s({42.0});
  EXPECT_DEATH(
      s.percentileCI(50.0), "percentileCI requires at least 2 samples");
}

// --- computeStabilityStats tests ---

TEST(StabilityStatsTest, StableWithIdenticalHalves) {
  // Same values in both halves -> stable
  std::vector<double> samples = {10, 10, 10, 10};
  auto stats = computeStabilityStats(samples, 50.0, 0.01);
  EXPECT_TRUE(stats.isStable);
  EXPECT_DOUBLE_EQ(stats.firstHalf.estimate, stats.secondHalf.estimate);
}

TEST(StabilityStatsTest, UnstableWithDisjointHalves) {
  // Completely disjoint halves -> unstable
  std::vector<double> samples = {1, 2, 100, 101};
  auto stats = computeStabilityStats(samples, 50.0, 0.01);
  EXPECT_FALSE(stats.isStable);
}

TEST(StabilityStatsTest, RequiresFourSamples) {
  std::vector<double> samples = {1, 2, 3};
  EXPECT_DEATH(
      computeStabilityStats(samples, 50.0, 0.01),
      "computeStabilityStats requires at least 4 samples");
}

TEST(StabilityStatsTest, EpsilonToleranceForSubNsValues) {
  // Sub-picosecond differences should be considered stable due to epsilon
  std::vector<double> samples = {0.0001, 0.0001, 0.0002, 0.0002};
  auto stats = computeStabilityStats(samples, 50.0, 0.01);
  EXPECT_TRUE(stats.isStable);
}

TEST(StabilityStatsTest, StableWhenDriftBelowTargetPrecision) {
  // Many samples with tiny systematic drift (~0.05% of estimate).
  // With a tight absolute epsilon this would be "unstable", but the relative
  // epsilon tied to `targetPrecisionPct` makes it stable.
  std::vector<double> samples;
  for (int i = 0; i < 500; ++i) {
    samples.push_back(2780.0); // first half: ~2780
  }
  for (int i = 0; i < 500; ++i) {
    samples.push_back(2778.5); // second half: ~2778.5, drift ~0.05%
  }
  // With 0.4% target precision, drift of 0.05% is well within budget.
  EXPECT_TRUE(computeStabilityStats(samples, 50.0, 0.4).isStable);
  // With 0.01% target precision, the same drift exceeds the budget.
  EXPECT_FALSE(computeStabilityStats(samples, 50.0, 0.01).isStable);
}

// --- runBenchmarksAdaptive integration tests ---

namespace {
AdaptiveOptions defaultTestOptions() {
  return AdaptiveOptions{
      .sliceUsec = 100,
      .targetPercentile = 50.0,
      .targetPrecisionPct = 50.0, // High tolerance for fast convergence
      .minSamples = 5,
      .minSecs = 0,
      .maxSecs = 1,
      .verbose = true, // make logs more useful
  };
}

// Standard baseline: 10ns/iter
BenchmarkFun defaultBaseline() {
  return [](unsigned n) { return TimeIterData{10ns * n, n, {}, 0}; };
}

// No-op suspender baseline for tests (no BENCHMARK_SUSPEND used in test funs)
BenchmarkFun noOpSuspenderBaseline() {
  return [](unsigned n) { return TimeIterData{10ns * n, n, {}, 0}; };
}

// Create a simple benchmark function with fixed ns/iter
BenchmarkFun simpleBench(std::chrono::nanoseconds nsPerIter) {
  return [nsPerIter](unsigned n) {
    return TimeIterData{nsPerIter * n, n, {}, 0};
  };
}

// Create benchmark registration with simple timing
BenchmarkRegistration makeReg(
    const char* name, std::chrono::nanoseconds nsPerIter) {
  return {"file.cpp", name, simpleBench(nsPerIter)};
}

// Helper to convert vector of values to vector of pointers
std::vector<const BenchmarkRegistration*> toPtrs(
    const std::vector<BenchmarkRegistration>& benchmarks) {
  std::vector<const BenchmarkRegistration*> ptrs;
  ptrs.reserve(benchmarks.size());
  for (const auto& b : benchmarks) {
    ptrs.push_back(&b);
  }
  return ptrs;
}
} // namespace

TEST(AdaptiveTest, EmptyBenchmarks) {
  auto result = runBenchmarksAdaptive(
      {}, defaultBaseline(), noOpSuspenderBaseline(), defaultTestOptions());
  EXPECT_TRUE(result.results.empty());
  EXPECT_EQ(result.totalRounds, 0);
}

TEST(AdaptiveTest, SingleBenchmark) {
  std::vector<BenchmarkRegistration> benchmarks{makeReg("bench1", 15ns)};
  auto result = runBenchmarksAdaptive(
      toPtrs(benchmarks),
      defaultBaseline(),
      noOpSuspenderBaseline(),
      defaultTestOptions());

  ASSERT_EQ(result.results.size(), 1);
  EXPECT_EQ(result.results[0].name, "bench1");
  // Baseline is 10ns, benchmark is 15ns, so adjusted should be ~5ns
  EXPECT_NEAR(result.results[0].timeInNs, 5.0, 0.5);
  EXPECT_GE(result.totalRounds, 5); // At least minSamples
}

TEST(AdaptiveTest, MultipleBenchmarks) {
  std::vector<BenchmarkRegistration> benchmarks{
      makeReg("fast", 12ns), makeReg("slow", 20ns)};
  auto result = runBenchmarksAdaptive(
      toPtrs(benchmarks),
      defaultBaseline(),
      noOpSuspenderBaseline(),
      defaultTestOptions());

  ASSERT_EQ(result.results.size(), 2);
  EXPECT_NEAR(result.results[0].timeInNs, 2.0, 0.5); // 12 - 10
  EXPECT_NEAR(result.results[1].timeInNs, 10.0, 0.5); // 20 - 10
}

TEST(AdaptiveTest, BaselineSubtractionClampsToZero) {
  // Benchmark faster than baseline (edge case)
  std::vector<BenchmarkRegistration> benchmarks{
      makeReg("faster_than_baseline", 15ns)};
  auto result = runBenchmarksAdaptive(
      toPtrs(benchmarks),
      simpleBench(20ns), // baseline > benchmark
      noOpSuspenderBaseline(),
      defaultTestOptions());

  ASSERT_EQ(result.results.size(), 1);
  // Should clamp to 0, not go negative
  EXPECT_EQ(result.results[0].timeInNs, 0.0);
}

TEST(AdaptiveTest, EqualSamplingAcrossBenchmarks) {
  // Verify that all benchmarks are sampled the same number of times per round
  auto opts = defaultTestOptions();
  opts.minSamples = 10;

  size_t baselineCalls = 0;
  auto baselineFun = [&](unsigned n) {
    ++baselineCalls;
    return TimeIterData{10ns * n, n, {}, 0};
  };

  size_t bench1Calls = 0;
  size_t bench2Calls = 0;

  std::vector<BenchmarkRegistration> benchmarks{
      {"file.cpp",
       "bench1",
       [&](unsigned n) {
         ++bench1Calls;
         return TimeIterData{15ns * n, n, {}, 0};
       }},
      {"file.cpp",
       "bench2",
       [&](unsigned n) {
         ++bench2Calls;
         return TimeIterData{20ns * n, n, {}, 0};
       }},
  };

  auto result = runBenchmarksAdaptive(
      toPtrs(benchmarks), baselineFun, noOpSuspenderBaseline(), opts);

  // With constant values, SE=0, so converges at first check (round 10)
  EXPECT_GE(result.totalRounds, 10u); // At least minSamples
  // Both benchmarks should have equal sampling calls
  EXPECT_EQ(bench1Calls, bench2Calls);
}

TEST(AdaptiveTest, ConvergenceWithVariance) {
  // Test convergence with quasi-continuous stationary data.
  // Cycles through 21 values (20-40) to simulate continuous stationary data.
  auto opts = defaultTestOptions();
  opts.minSamples = 5;
  opts.targetPrecisionPct = 20.0; // Should converge once CI < 20%

  size_t benchIdx = 0;
  std::vector<BenchmarkRegistration> benchmarks{
      {"file.cpp",
       "cycling",
       [&](unsigned n) {
         auto t = std::chrono::nanoseconds(20 + (benchIdx++ % 21));
         return TimeIterData{t * n, n, {}, 0};
       }},
  };

  auto result = runBenchmarksAdaptive(
      toPtrs(benchmarks), defaultBaseline(), noOpSuspenderBaseline(), opts);

  EXPECT_GE(result.totalRounds, opts.minSamples);
  EXPECT_LE(result.totalRounds, 1000u);
  // Median of [20..40] is 30, baseline is 10, so result ≈ 20
  EXPECT_NEAR(result.results[0].timeInNs, 20.0, 10.0);
}

TEST(AdaptiveTest, MinSamplesGuaranteed) {
  // With constant values, SE=0, so would converge immediately. But we should
  // still collect minSamples samples.
  auto opts = defaultTestOptions();
  opts.minSamples = 20;
  opts.targetPrecisionPct =
      100.0; // Very loose, would converge immediately if allowed

  size_t benchCalls = 0;
  std::vector<BenchmarkRegistration> benchmarks{
      {"file.cpp",
       "constant",
       [&](unsigned n) {
         ++benchCalls;
         return TimeIterData{15ns * n, n, {}, 0};
       }},
  };

  auto result = runBenchmarksAdaptive(
      toPtrs(benchmarks), defaultBaseline(), noOpSuspenderBaseline(), opts);

  EXPECT_GE(benchCalls, 20u);
  EXPECT_GE(result.totalRounds, 20u);
}

TEST(AdaptiveTest, HighVarianceHitsTimeout) {
  // Test that high-variance data eventually hits maxSecs timeout.
  // With alternating 20ns/40ns values, the exact percentile CI stays wide.
  auto opts = defaultTestOptions();
  opts.sliceUsec = 50000; // 50ms per sample
  opts.minSamples = 5;
  opts.targetPrecisionPct = 10.0; // Tight threshold that won't converge
  opts.minSecs = 0;
  opts.maxSecs = 1; // Short timeout

  // Baseline: 50ms duration per call, returning 10ns/iter value
  auto baselineFun = [](unsigned n) {
    return TimeIterData{50ms * n, 5'000'000u * n, {}, 0};
  };

  // Benchmark: alternating 20ns/40ns value, 50ms elapsed per call
  size_t benchIdx = 0;
  std::vector<BenchmarkRegistration> benchmarks{
      {"file.cpp",
       "alternating",
       [&](unsigned n) {
         unsigned niter = (benchIdx % 2 == 0)
             ? 2'500'000u * n // 50ms / 2.5M = 20ns
             : 1'250'000u * n; // 50ms / 1.25M = 40ns
         ++benchIdx;
         return TimeIterData{50ms * n, niter, {}, 0};
       }},
  };

  auto result = runBenchmarksAdaptive(
      toPtrs(benchmarks), baselineFun, noOpSuspenderBaseline(), opts);

  // Should hit timeout since CI won't converge to 10%
  EXPECT_GT(result.totalRounds, opts.minSamples);
}

TEST(AdaptiveTest, UserCountersFromPercentileMatchedSample) {
  // Verify that user counters come from the sample closest to the percentile.
  // With samples 20/25/30/35/40ns, p50=30ns has counter idx=300.
  auto opts = defaultTestOptions();
  opts.minSamples = 5;
  opts.targetPrecisionPct = 100.0;

  size_t benchIdx = 0;
  std::vector<std::chrono::nanoseconds> times = {20ns, 25ns, 30ns, 35ns, 40ns};
  std::vector<int64_t> counterVals = {100, 200, 300, 400, 500};
  std::vector<BenchmarkRegistration> benchmarks{
      {"file.cpp",
       "with_counters",
       [&](unsigned n) {
         size_t idx = benchIdx++ % 5;
         UserCounters counters{{"idx", UserMetric(counterVals[idx])}};
         return TimeIterData{times[idx] * n, n, counters, 0};
       }},
  };

  auto result = runBenchmarksAdaptive(
      toPtrs(benchmarks), defaultBaseline(), noOpSuspenderBaseline(), opts);

  ASSERT_EQ(result.results.size(), 1);
  ASSERT_EQ(result.results[0].counters.count("idx"), 1);
  // p50 of {20,25,30,35,40} adjusted by baseline 10 = {10,15,20,25,30} ->
  // p50=20 Closest sample to 20ns adjusted has counter 300
  EXPECT_EQ(result.results[0].counters.at("idx"), UserMetric(300));
}

TEST(AdaptiveTest, MinSecsEnforcesMinimumDuration) {
  // minSecs forces continued sampling even when CI converges immediately.
  auto opts = defaultTestOptions();
  opts.sliceUsec = 50000; // 50ms per sample
  opts.minSamples = 5;
  opts.targetPrecisionPct = 100.0;
  opts.minSecs = 1;
  opts.maxSecs = 2;

  std::vector<BenchmarkRegistration> benchmarks{makeReg("constant", 60ms)};
  auto result = runBenchmarksAdaptive(
      toPtrs(benchmarks), simpleBench(50ms), noOpSuspenderBaseline(), opts);

  // 50ms baseline + 50ms benchmark = 100ms/round, need 10 rounds for 1s
  EXPECT_GE(result.totalRounds, 10u);
}

TEST(AdaptiveTest, SuspenderOverheadCorrection) {
  // 50ns raw - 10ns baseline - 35ns suspender = 5ns adjusted
  auto opts = defaultTestOptions();
  opts.minSamples = 5;
  opts.targetPrecisionPct = 100.0;

  std::vector<BenchmarkRegistration> benchmarks{
      {"file.cpp",
       "with_suspend",
       [](unsigned n) {
         return TimeIterData{50ns * n, n, {}, n};
       }}, // 1 suspend/iter
  };

  auto result = runBenchmarksAdaptive(
      toPtrs(benchmarks), defaultBaseline(), simpleBench(35ns), opts);

  ASSERT_EQ(result.results.size(), 1);
  EXPECT_NEAR(result.results[0].timeInNs, 5.0, 0.5);
}

TEST(AdaptiveTest, SuspenderOverheadClampsToZero) {
  // 30ns raw - 10ns baseline - 100ns suspender = -80ns, clamped to 0
  auto opts = defaultTestOptions();
  opts.minSamples = 5;
  opts.targetPrecisionPct = 100.0;

  std::vector<BenchmarkRegistration> benchmarks{
      {"file.cpp",
       "clamped",
       [](unsigned n) { return TimeIterData{30ns * n, n, {}, n}; }},
  };

  auto result = runBenchmarksAdaptive(
      toPtrs(benchmarks), defaultBaseline(), simpleBench(100ns), opts);

  ASSERT_EQ(result.results.size(), 1);
  EXPECT_EQ(result.results[0].timeInNs, 0.0);
}

// --- recalibrateIterCount tests ---

TEST(RecalibrateIterCountTest, Basic) {
  // Converges from 1: ceil(1 * 1'000'000 / 500) = 2000
  EXPECT_EQ(recalibrateIterCount(1, 1ms, 500ns), 2000);
  // Monotonically increasing: slow sample can't decrease iterCount
  EXPECT_EQ(recalibrateIterCount(1000, 1ms, 2ms), 1000);
  // Tiny duration (<= 10ns) doubles instead of dividing
  EXPECT_EQ(recalibrateIterCount(4, 1ms, 5ns), 8);
  EXPECT_EQ(recalibrateIterCount(4, 1ms, 0ns), 8);
  // Never returns zero
  EXPECT_GE(recalibrateIterCount(1, 1ms, 999'999'999ns), 1);
}
