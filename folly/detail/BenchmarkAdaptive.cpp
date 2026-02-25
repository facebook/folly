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

#include <chrono>
#include <iomanip>
#include <sstream>

#include <folly/String.h>

namespace folly {
namespace detail {

using namespace std::chrono;

// Compute split-half stability: are the first-half and second-half estimates
// within each other's CIs (with epsilon tolerance)?  Needs at least 4 samples.
StabilityStats computeStabilityStats(
    const std::vector<double>& samples, double percentile) {
  size_t n = samples.size();
  CHECK_GE(n, 4) << "computeStabilityStats requires at least 4 samples";

  std::vector<double> firstHalf(samples.begin(), samples.begin() + n / 2);
  std::vector<double> secondHalf(samples.begin() + n / 2, samples.end());

  auto ci1 = SortedSamples(std::move(firstHalf)).percentileCI(percentile);
  auto ci2 = SortedSamples(std::move(secondHalf)).percentileCI(percentile);

  // Check if each estimate is within the other's CI, with epsilon tolerance
  return {
      .firstHalf = ci1,
      .secondHalf = ci2,
      .isStable = (ci1.estimate >= ci2.lo - kStabilityEpsilonNs) &&
          (ci1.estimate <= ci2.hi + kStabilityEpsilonNs) &&
          (ci2.estimate >= ci1.lo - kStabilityEpsilonNs) &&
          (ci2.estimate <= ci1.hi + kStabilityEpsilonNs),
  };
}

size_t recalibrateIterCount(
    size_t iterCount, nanoseconds target, nanoseconds dur) {
  if (dur.count() > 10) { // don't divide by small numbers
    size_t candidate = std::max<size_t>(
        1,
        static_cast<size_t>(std::ceil(
            iterCount * static_cast<double>(target.count()) / dur.count())));
    return std::max(iterCount, candidate);
  }
  return iterCount * 2;
}

namespace {

// IMPORTANT: Many functions here are marked FOLLY_NOINLINE to try to reduce
// interference between the benchmark harness and the code under test. The hope
// is that by keeping logging, statistics, and convergence-checking code out of
// line, we:
//   (1) Keep the hot measurement loop compact and cache-friendly
//   (2) Reduce the chance of harness code polluting icache during measurement
//   (3) Prevent the compiler from intermingling harness with benchmarks
// This is somewhat speculative - we can't prove it helps - but the cost is low
// and the potential benefit is cleaner measurements. The most important uses
// are on SamplingLoop::run() and checkAllDone() which bound the hot path.

// Format sample statistics with CI and split-half stability.
FOLLY_NOINLINE std::string formatSampleStats(
    const char* name, const std::vector<double>& samples, double percentile) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2);
  oss << kANSIBold << name << kANSIReset << ": ";

  if (samples.size() < 2) {
    oss << "samples=" << samples.size();
    return oss.str();
  }

  SortedSamples sorted(samples);
  auto ci = sorted.percentileCI(percentile);
  oss << "p" << percentile << "=" << ci.estimate << "ns"
      << " CI=[" << ci.lo << ", " << ci.hi << "] (" << ci.relWidth() << "%)"
      << " samples=" << samples.size();

  if (samples.size() >= 4) {
    auto ss = computeStabilityStats(samples, percentile);
    oss << "\n    "
        << (ss.isStable
                ? "[stable]"
                : (std::string(kANSIBoldYellow) + "[unstable]" + kANSIReset))
        << " 1st=" << ss.firstHalf.estimate << " [" << ss.firstHalf.lo << ", "
        << ss.firstHalf.hi << "]"
        << " 2nd=" << ss.secondHalf.estimate << " [" << ss.secondHalf.lo << ", "
        << ss.secondHalf.hi << "]";
  }
  return oss.str();
}

// State for tracking samples, used for both benchmarks and baselines.
// For baselines: reg=nullptr, counters will be empty, done unused.
struct BenchState {
  std::string name; // Benchmark or baseline name
  const BenchmarkRegistration* reg; // nullptr for baselines
  const AdaptiveOptions* opts;
  size_t iterCount = 1;
  std::vector<std::pair<double, UserCounters>> samples{};
  nanoseconds elapsed{0};
  bool done = false; // benchmarks only

  // Extract just the timing values for statistical analysis.
  std::vector<double> timings() const {
    std::vector<double> result;
    result.reserve(samples.size());
    for (const auto& s : samples) {
      result.push_back(s.first);
    }
    return result;
  }

  void doRecalibrate(nanoseconds dur) {
    auto target = duration_cast<nanoseconds>(microseconds(opts->sliceUsec));
    size_t prev = iterCount;
    iterCount = folly::detail::recalibrateIterCount(iterCount, target, dur);
    if (dur > 2 * target && prev > 1) {
      LOG(WARNING) << kANSIBoldYellow << "Unstable calibration: " << kANSIReset
                   << name << " took " << dur.count() << "ns (target "
                   << target.count() << "ns)";
    }
  }

  // For baselines: run function and store raw sample (no correction).
  double runAndAddSampleRaw(
      const BenchmarkFun& fun, nanoseconds& totalElapsed) {
    TimeIterData res = fun(static_cast<unsigned>(iterCount));
    auto dur = duration_cast<nanoseconds>(res.duration);
    double nsPerIter = static_cast<double>(dur.count()) / res.niter;
    samples.emplace_back(nsPerIter, UserCounters{});
    elapsed += dur;
    totalElapsed += dur;
    doRecalibrate(dur);
    return nsPerIter;
  }

  // For benchmarks: run and store baseline-adjusted sample.
  void runAndAddSample(
      double baselineNsPerIter,
      double suspenderOverheadNsPerSuspension,
      nanoseconds& totalElapsed) {
    TimeIterData res = reg->func(static_cast<unsigned>(iterCount));
    auto dur = duration_cast<nanoseconds>(res.duration);
    double adjusted =
        static_cast<double>(dur.count()) / res.niter - baselineNsPerIter;
    if (res.suspensionCount > 0) {
      adjusted -= static_cast<double>(res.suspensionCount) / res.niter *
          suspenderOverheadNsPerSuspension;
    }
    adjusted = std::max(0.0, adjusted);
    samples.emplace_back(adjusted, std::move(res.userCounters));
    elapsed += dur;
    totalElapsed += dur;
    doRecalibrate(dur);
  }

  bool isStable() const {
    return samples.size() >= 4 &&
        computeStabilityStats(timings(), opts->targetPercentile).isStable;
  }

  bool checkDone() {
    if (elapsed >= seconds(opts->maxSecs)) {
      done = true;
      return true;
    }
    if (samples.size() < opts->minSamples ||
        elapsed < duration<double>(opts->minSecs)) {
      return false;
    }
    if (!isStable()) {
      return false;
    }
    done =
        SortedSamples(timings()).percentileCIRelWidth(opts->targetPercentile) <=
        opts->targetPrecisionPct;
    return done;
  }

  UserCounters countersForEstimate(double estimate) const {
    if (samples.empty()) {
      return {};
    }
    auto best = std::min_element(
        samples.begin(),
        samples.end(),
        [estimate](const auto& a, const auto& b) {
          return std::abs(a.first - estimate) < std::abs(b.first - estimate);
        });
    return best->second;
  }

  std::string formatStats() const {
    return formatSampleStats(name.c_str(), timings(), opts->targetPercentile);
  }
};

FOLLY_NOINLINE void verboseLogInitial(
    const AdaptiveOptions& opts, const std::vector<BenchState>& states) {
  std::ostringstream oss;
  oss << "\n  " << opts.toString();
  for (const auto& s : states) {
    oss << "\n    " << s.name;
  }
  LOG(INFO) << oss.str();
}

FOLLY_NOINLINE void verboseLogFinal(
    size_t round,
    const BenchState& baselineState,
    const BenchState& suspenderBaselineState,
    nanoseconds wallClock,
    const std::vector<BenchState>& states) {
  // Compute total benchmark time
  nanoseconds totalBenchTime{0};
  for (const auto& s : states) {
    totalBenchTime += s.elapsed;
  }
  auto accountedTime =
      baselineState.elapsed + suspenderBaselineState.elapsed + totalBenchTime;
  auto overhead = wallClock - accountedTime;

  std::ostringstream oss;
  oss << "\nFinal results (" << round << " rounds)";

  // Time breakdown using prettyPrint for concise output
  auto fmtTime = [](nanoseconds ns) {
    return trimWhitespace(
               prettyPrint(
                   duration_cast<duration<double>>(ns).count(),
                   PRETTY_TIME_HMS,
                   false))
        .str();
  };
  oss << "\n  Time: " << fmtTime(wallClock) << " – baseline "
      << fmtTime(baselineState.elapsed) << ", suspender "
      << fmtTime(suspenderBaselineState.elapsed) << ", benchmarks "
      << fmtTime(totalBenchTime) << ", overhead " << fmtTime(overhead);

  // Baseline stats
  oss << "\n  " << baselineState.formatStats();
  oss << "\n  " << suspenderBaselineState.formatStats();

  // Benchmark stats
  for (const auto& s : states) {
    oss << "\n  " << s.formatStats();
  }
  LOG(INFO) << oss.str();
}

// Encapsulates the sampling loop state and logic.
// Methods are noinline to keep the hot loop compact and isolated.
struct SamplingLoop {
  const BenchmarkFun& baselineFun;
  const BenchmarkFun& suspenderBaselineFun;
  BenchState& baselineState;
  BenchState& suspenderBaselineState;
  std::vector<BenchState>& states;
  const AdaptiveOptions& opts;
  nanoseconds totalElapsed{0};
  size_t round = 0;

  // Baseline sampling intervals to reduce overhead.
  // Baseline is very cheap (~0.3ns), suspender is more expensive (~35ns).
  static constexpr size_t kBaselineSampleInterval = 8;
  static constexpr size_t kSuspenderSampleInterval = 2;

  // Cached baseline values (reused between samples)
  double cachedBaselineNsPerIter = 0;
  double cachedSuspenderOverheadNsPerSuspension = 0;

  // Convergence check state
  nanoseconds lastCheck{0};
  nanoseconds lastOscillationWarning{0};

  static constexpr auto kCheckInterval = milliseconds(150);
  static constexpr auto kOscillationWarningInterval = seconds(10);

  // Run sampling loop until all benchmarks are done.
  FOLLY_NOINLINE void run() {
    while (true) {
      // Sample baseline periodically (cheap, every 8 rounds)
      if (round % kBaselineSampleInterval == 0) {
        cachedBaselineNsPerIter =
            baselineState.runAndAddSampleRaw(baselineFun, totalElapsed);
      }

      // Sample suspender baseline more frequently (expensive, every 2 rounds)
      if (round % kSuspenderSampleInterval == 0) {
        cachedSuspenderOverheadNsPerSuspension =
            suspenderBaselineState.runAndAddSampleRaw(
                suspenderBaselineFun, totalElapsed);
      }

      // Run benchmarks (skip done ones), using cached baseline values
      for (auto& s : states) {
        if (s.done) {
          continue;
        }
        s.runAndAddSample(
            cachedBaselineNsPerIter,
            cachedSuspenderOverheadNsPerSuspension,
            totalElapsed);
      }
      ++round;
      if (checkAllDone()) {
        break;
      }
    }
  }

  // Returns true if all benchmarks are done and we should exit the loop.
  FOLLY_NOINLINE bool checkAllDone() {
    if (totalElapsed - lastCheck < kCheckInterval) {
      return false;
    }
    lastCheck = totalElapsed;

    // Warn periodically about benchmarks/baselines that are oscillating
    if (totalElapsed - lastOscillationWarning >= kOscillationWarningInterval) {
      std::string msg;
      // Check baselines first
      for (const auto* bs : {&baselineState, &suspenderBaselineState}) {
        if (!bs->isStable() && bs->samples.size() >= opts.minSamples) {
          msg += "\n  " + bs->formatStats();
        }
      }
      // Then benchmarks
      for (const auto& s : states) {
        if (s.done) {
          continue;
        }
        if (!s.isStable() && s.samples.size() >= opts.minSamples) {
          auto benchSecs = duration_cast<seconds>(s.elapsed).count();
          msg += fmt::format("\n  {} ({}s own)", s.formatStats(), benchSecs);
        }
      }
      if (!msg.empty()) {
        LOG(WARNING) << "Still oscillating after "
                     << duration_cast<seconds>(totalElapsed).count()
                     << "s total:" << msg << "\n\n";
        lastOscillationWarning = totalElapsed;
      }
    }

    // Check done and collect newly-done benchmarks by category
    std::string converged, timedOutOscillating, timedOutWideCI;
    bool allDone = true;
    for (auto& s : states) {
      bool wasDone = s.done;
      if (!s.checkDone()) {
        allDone = false;
      } else if (!wasDone && opts.verbose) {
        bool exceeded = s.elapsed >= seconds(opts.maxSecs);
        if (!exceeded) {
          converged += "\n  " + s.formatStats();
        } else if (!s.isStable()) {
          timedOutOscillating += "\n  " + s.formatStats();
        } else {
          timedOutWideCI += "\n  " + s.formatStats();
        }
      }
    }
    std::string msg;
    if (!converged.empty()) {
      msg += fmt::format(
          "\n{}→ Converged:{}{}", kANSIBoldGreen, kANSIReset, converged);
    }
    if (!timedOutOscillating.empty()) {
      msg += fmt::format(
          "\n{}→ Exceeded max_secs (still oscillating):{}{}",
          kANSIBoldYellow,
          kANSIReset,
          timedOutOscillating);
    }
    if (!timedOutWideCI.empty()) {
      msg += fmt::format(
          "\n{}→ Exceeded max_secs (CI too wide):{}{}",
          kANSIBoldYellow,
          kANSIReset,
          timedOutWideCI);
    }
    if (!msg.empty()) {
      LOG(INFO) << msg;
    }
    return allDone;
  }
};

} // namespace

AdaptiveResult runBenchmarksAdaptive(
    const std::vector<const BenchmarkRegistration*>& benchmarks,
    const BenchmarkFun& baselineFun,
    const BenchmarkFun& suspenderBaselineFun,
    const AdaptiveOptions& opts) {
  AdaptiveResult result;
  if (benchmarks.empty()) {
    return result;
  }

  auto wallClockStart = steady_clock::now();

  // No upfront calibration — each BenchState starts at `iterCount=1` and
  // self-adjusts via `recalibrateIterCount()` after every sample.  This avoids
  // the cold-start problem where the first call is slow (page faults, TLB
  // misses, lazy linking) causing the doubling search to short-circuit and
  // return iterCount=1 for all subsequent rounds.

  std::vector<BenchState> states;
  states.reserve(benchmarks.size());
  for (const auto* b : benchmarks) {
    states.emplace_back(
        BenchState{
            .name = b->name,
            .reg = b,
            .opts = &opts,
        });
  }

  if (opts.verbose) {
    verboseLogInitial(opts, states);
  }

  BenchState baselineState{
      .name = "baseline",
      .reg = nullptr,
      .opts = &opts,
  };
  BenchState suspenderBaselineState{
      .name = "suspenderBaseline",
      .reg = nullptr,
      .opts = &opts,
  };
  SamplingLoop loop{
      .baselineFun = baselineFun,
      .suspenderBaselineFun = suspenderBaselineFun,
      .baselineState = baselineState,
      .suspenderBaselineState = suspenderBaselineState,
      .states = states,
      .opts = opts};

  // Interleaved Sampling with time-based convergence checks
  loop.run();

  // Final results
  if (opts.verbose) {
    verboseLogFinal(
        loop.round,
        baselineState,
        suspenderBaselineState,
        duration_cast<nanoseconds>(steady_clock::now() - wallClockStart),
        states);
  }

  // Log errors for benchmarks that didn't converge (stability or CI failures)
  {
    std::string msg;
    for (const auto& s : states) {
      if (!s.isStable() ||
          SortedSamples(s.timings())
                  .percentileCIRelWidth(opts.targetPercentile) >
              opts.targetPrecisionPct) {
        msg += "\n  " + s.formatStats();
      }
    }
    if (!msg.empty()) {
      LOG(ERROR) << kANSIBoldRed
                 << "Did not converge or become stable:" << kANSIReset << msg;
    }
  }

  result.results.reserve(states.size());
  for (const auto& s : states) {
    SortedSamples sorted(s.timings());
    double pctile = sorted.percentile(opts.targetPercentile);
    result.results.emplace_back(
        BenchmarkResult{
            s.reg->file,
            s.reg->name,
            pctile, // Already baseline-adjusted
            s.countersForEstimate(pctile)});
  }

  result.totalRounds = loop.round;
  return result;
}

} // namespace detail
} // namespace folly
