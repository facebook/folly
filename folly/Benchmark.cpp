/*
 * Copyright 2016 Facebook, Inc.
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

// @author Andrei Alexandrescu (andrei.alexandrescu@fb.com)

#include <folly/Benchmark.h>
#include <folly/Foreach.h>
#include <folly/json.h>
#include <folly/String.h>

#include <algorithm>
#include <boost/regex.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>
#include <cstring>

using namespace std;

DEFINE_bool(benchmark, false, "Run benchmarks.");
DEFINE_bool(json, false, "Output in JSON format.");

DEFINE_string(
    bm_regex,
    "",
    "Only benchmarks whose names match this regex will be run.");

DEFINE_int64(
    bm_min_usec,
    100,
    "Minimum # of microseconds we'll accept for each benchmark.");

DEFINE_int64(
    bm_min_iters,
    1,
    "Minimum # of iterations we'll try for each benchmark.");

DEFINE_int64(
    bm_max_iters,
    1L << 30L,
    "Maximum # of iterations we'll try for each benchmark.");

DEFINE_int32(
    bm_max_secs,
    1,
    "Maximum # of seconds we'll spend on each benchmark.");

namespace folly {

BenchmarkSuspender::NanosecondsSpent BenchmarkSuspender::nsSpent;

typedef function<detail::TimeIterPair(unsigned int)> BenchmarkFun;


vector<tuple<string, string, BenchmarkFun>>& benchmarks() {
  static vector<tuple<string, string, BenchmarkFun>> _benchmarks;
  return _benchmarks;
}

#define FB_FOLLY_GLOBAL_BENCHMARK_BASELINE fbFollyGlobalBenchmarkBaseline
#define FB_STRINGIZE_X2(x) FB_STRINGIZE(x)

// Add the global baseline
BENCHMARK(FB_FOLLY_GLOBAL_BENCHMARK_BASELINE) {
#ifdef _MSC_VER
  _ReadWriteBarrier();
#else
  asm volatile("");
#endif
}

int getGlobalBenchmarkBaselineIndex() {
  const char *global = FB_STRINGIZE_X2(FB_FOLLY_GLOBAL_BENCHMARK_BASELINE);
  auto it = std::find_if(
    benchmarks().begin(),
    benchmarks().end(),
    [global](const tuple<string, string, BenchmarkFun> &v) {
      return get<1>(v) == global;
    }
  );
  CHECK(it != benchmarks().end());
  return it - benchmarks().begin();
}

#undef FB_STRINGIZE_X2
#undef FB_FOLLY_GLOBAL_BENCHMARK_BASELINE

void detail::addBenchmarkImpl(const char* file, const char* name,
                              BenchmarkFun fun) {
  benchmarks().emplace_back(file, name, std::move(fun));
}

/**
 * Given a point, gives density at that point as a number 0.0 < x <=
 * 1.0. The result is 1.0 if all samples are equal to where, and
 * decreases near 0 if all points are far away from it. The density is
 * computed with the help of a radial basis function.
 */
static double density(const double * begin, const double *const end,
                      const double where, const double bandwidth) {
  assert(begin < end);
  assert(bandwidth > 0.0);
  double sum = 0.0;
  FOR_EACH_RANGE (i, begin, end) {
    auto d = (*i - where) / bandwidth;
    sum += exp(- d * d);
  }
  return sum / (end - begin);
}

/**
 * Computes mean and variance for a bunch of data points. Note that
 * mean is currently not being used.
 */
static pair<double, double>
meanVariance(const double * begin, const double *const end) {
  assert(begin < end);
  double sum = 0.0, sum2 = 0.0;
  FOR_EACH_RANGE (i, begin, end) {
    sum += *i;
    sum2 += *i * *i;
  }
  auto const n = end - begin;
  return make_pair(sum / n, sqrt((sum2 - sum * sum / n) / n));
}

/**
 * Computes the mode of a sample set through brute force. Assumes
 * input is sorted.
 */
static double mode(const double * begin, const double *const end) {
  assert(begin < end);
  // Lower bound and upper bound for result and their respective
  // densities.
  auto
    result = 0.0,
    bestDensity = 0.0;

  // Get the variance so we pass it down to density()
  auto const sigma = meanVariance(begin, end).second;
  if (!sigma) {
    // No variance means constant signal
    return *begin;
  }

  FOR_EACH_RANGE (i, begin, end) {
    assert(i == begin || *i >= i[-1]);
    auto candidate = density(begin, end, *i, sigma * sqrt(2.0));
    if (candidate > bestDensity) {
      // Found a new best
      bestDensity = candidate;
      result = *i;
    } else {
      // Density is decreasing... we could break here if we definitely
      // knew this is unimodal.
    }
  }

  return result;
}

/**
 * Given a bunch of benchmark samples, estimate the actual run time.
 */
static double estimateTime(double * begin, double * end) {
  assert(begin < end);

  // Current state of the art: get the minimum. After some
  // experimentation, it seems taking the minimum is the best.

  return *min_element(begin, end);

  // What follows after estimates the time as the mode of the
  // distribution.

  // Select the awesomest (i.e. most frequent) result. We do this by
  // sorting and then computing the longest run length.
  sort(begin, end);

  // Eliminate outliers. A time much larger than the minimum time is
  // considered an outlier.
  while (end[-1] > 2.0 * *begin) {
    --end;
    if (begin == end) {
      LOG(INFO) << *begin;
    }
    assert(begin < end);
  }

  double result = 0;

  /* Code used just for comparison purposes */ {
    unsigned bestFrequency = 0;
    unsigned candidateFrequency = 1;
    double candidateValue = *begin;
    for (auto current = begin + 1; ; ++current) {
      if (current == end || *current != candidateValue) {
        // Done with the current run, see if it was best
        if (candidateFrequency > bestFrequency) {
          bestFrequency = candidateFrequency;
          result = candidateValue;
        }
        if (current == end) {
          break;
        }
        // Start a new run
        candidateValue = *current;
        candidateFrequency = 1;
      } else {
        // Cool, inside a run, increase the frequency
        ++candidateFrequency;
      }
    }
  }

  result = mode(begin, end);

  return result;
}

static double runBenchmarkGetNSPerIteration(const BenchmarkFun& fun,
                                            const double globalBaseline) {
  // They key here is accuracy; too low numbers means the accuracy was
  // coarse. We up the ante until we get to at least minNanoseconds
  // timings.
  static uint64_t resolutionInNs = 0;
  if (!resolutionInNs) {
    timespec ts;
    CHECK_EQ(0, clock_getres(CLOCK_REALTIME, &ts));
    CHECK_EQ(0, ts.tv_sec) << "Clock sucks.";
    CHECK_LT(0, ts.tv_nsec) << "Clock too fast for its own good.";
    CHECK_EQ(1, ts.tv_nsec) << "Clock too coarse, upgrade your kernel.";
    resolutionInNs = ts.tv_nsec;
  }
  // We choose a minimum minimum (sic) of 100,000 nanoseconds, but if
  // the clock resolution is worse than that, it will be larger. In
  // essence we're aiming at making the quantization noise 0.01%.
  static const auto minNanoseconds =
    max<uint64_t>(FLAGS_bm_min_usec * 1000UL,
        min<uint64_t>(resolutionInNs * 100000, 1000000000ULL));

  // We do measurements in several epochs and take the minimum, to
  // account for jitter.
  static const unsigned int epochs = 1000;
  // We establish a total time budget as we don't want a measurement
  // to take too long. This will curtail the number of actual epochs.
  const uint64_t timeBudgetInNs = FLAGS_bm_max_secs * 1000000000ULL;
  timespec global;
  CHECK_EQ(0, clock_gettime(CLOCK_REALTIME, &global));

  double epochResults[epochs] = { 0 };
  size_t actualEpochs = 0;

  for (; actualEpochs < epochs; ++actualEpochs) {
    const auto maxIters = FLAGS_bm_max_iters;
    for (unsigned int n = FLAGS_bm_min_iters; n < maxIters; n *= 2) {
      auto const nsecsAndIter = fun(n);
      if (nsecsAndIter.first < minNanoseconds) {
        continue;
      }
      // We got an accurate enough timing, done. But only save if
      // smaller than the current result.
      epochResults[actualEpochs] = max(0.0, double(nsecsAndIter.first) /
                                       nsecsAndIter.second - globalBaseline);
      // Done with the current epoch, we got a meaningful timing.
      break;
    }
    timespec now;
    CHECK_EQ(0, clock_gettime(CLOCK_REALTIME, &now));
    if (detail::timespecDiff(now, global) >= timeBudgetInNs) {
      // No more time budget available.
      ++actualEpochs;
      break;
    }
  }

  // If the benchmark was basically drowned in baseline noise, it's
  // possible it became negative.
  return max(0.0, estimateTime(epochResults, epochResults + actualEpochs));
}

struct ScaleInfo {
  double boundary;
  const char* suffix;
};

static const ScaleInfo kTimeSuffixes[] {
  { 365.25 * 24 * 3600, "years" },
  { 24 * 3600, "days" },
  { 3600, "hr" },
  { 60, "min" },
  { 1, "s" },
  { 1E-3, "ms" },
  { 1E-6, "us" },
  { 1E-9, "ns" },
  { 1E-12, "ps" },
  { 1E-15, "fs" },
  { 0, nullptr },
};

static const ScaleInfo kMetricSuffixes[] {
  { 1E24, "Y" },  // yotta
  { 1E21, "Z" },  // zetta
  { 1E18, "X" },  // "exa" written with suffix 'X' so as to not create
                  //   confusion with scientific notation
  { 1E15, "P" },  // peta
  { 1E12, "T" },  // terra
  { 1E9, "G" },   // giga
  { 1E6, "M" },   // mega
  { 1E3, "K" },   // kilo
  { 1, "" },
  { 1E-3, "m" },  // milli
  { 1E-6, "u" },  // micro
  { 1E-9, "n" },  // nano
  { 1E-12, "p" }, // pico
  { 1E-15, "f" }, // femto
  { 1E-18, "a" }, // atto
  { 1E-21, "z" }, // zepto
  { 1E-24, "y" }, // yocto
  { 0, nullptr },
};

static string humanReadable(double n, unsigned int decimals,
                            const ScaleInfo* scales) {
  if (std::isinf(n) || std::isnan(n)) {
    return folly::to<string>(n);
  }

  const double absValue = fabs(n);
  const ScaleInfo* scale = scales;
  while (absValue < scale[0].boundary && scale[1].suffix != nullptr) {
    ++scale;
  }

  const double scaledValue = n / scale->boundary;
  return stringPrintf("%.*f%s", decimals, scaledValue, scale->suffix);
}

static string readableTime(double n, unsigned int decimals) {
  return humanReadable(n, decimals, kTimeSuffixes);
}

static string metricReadable(double n, unsigned int decimals) {
  return humanReadable(n, decimals, kMetricSuffixes);
}

static void printBenchmarkResultsAsTable(
  const vector<tuple<string, string, double> >& data) {
  // Width available
  static const unsigned int columns = 76;

  // Compute the longest benchmark name
  size_t longestName = 0;
  FOR_EACH_RANGE (i, 1, benchmarks().size()) {
    longestName = max(longestName, get<1>(benchmarks()[i]).size());
  }

  // Print a horizontal rule
  auto separator = [&](char pad) {
    puts(string(columns, pad).c_str());
  };

  // Print header for a file
  auto header = [&](const string& file) {
    separator('=');
    printf("%-*srelative  time/iter  iters/s\n",
           columns - 28, file.c_str());
    separator('=');
  };

  double baselineNsPerIter = numeric_limits<double>::max();
  string lastFile;

  for (auto& datum : data) {
    auto file = get<0>(datum);
    if (file != lastFile) {
      // New file starting
      header(file);
      lastFile = file;
    }

    string s = get<1>(datum);
    if (s == "-") {
      separator('-');
      continue;
    }
    bool useBaseline /* = void */;
    if (s[0] == '%') {
      s.erase(0, 1);
      useBaseline = true;
    } else {
      baselineNsPerIter = get<2>(datum);
      useBaseline = false;
    }
    s.resize(columns - 29, ' ');
    auto nsPerIter = get<2>(datum);
    auto secPerIter = nsPerIter / 1E9;
    auto itersPerSec = (secPerIter == 0)
                           ? std::numeric_limits<double>::infinity()
                           : (1 / secPerIter);
    if (!useBaseline) {
      // Print without baseline
      printf("%*s           %9s  %7s\n",
             static_cast<int>(s.size()), s.c_str(),
             readableTime(secPerIter, 2).c_str(),
             metricReadable(itersPerSec, 2).c_str());
    } else {
      // Print with baseline
      auto rel = baselineNsPerIter / nsPerIter * 100.0;
      printf("%*s %7.2f%%  %9s  %7s\n",
             static_cast<int>(s.size()), s.c_str(),
             rel,
             readableTime(secPerIter, 2).c_str(),
             metricReadable(itersPerSec, 2).c_str());
    }
  }
  separator('=');
}

static void printBenchmarkResultsAsJson(
  const vector<tuple<string, string, double> >& data) {
  dynamic d = dynamic::object;
  for (auto& datum: data) {
    d[std::get<1>(datum)] = std::get<2>(datum) * 1000.;
  }

  printf("%s\n", toPrettyJson(d).c_str());
}

static void printBenchmarkResults(
  const vector<tuple<string, string, double> >& data) {

  if (FLAGS_json) {
    printBenchmarkResultsAsJson(data);
  } else {
    printBenchmarkResultsAsTable(data);
  }
}

void runBenchmarks() {
  CHECK(!benchmarks().empty());

  vector<tuple<string, string, double>> results;
  results.reserve(benchmarks().size() - 1);

  std::unique_ptr<boost::regex> bmRegex;
  if (!FLAGS_bm_regex.empty()) {
    bmRegex.reset(new boost::regex(FLAGS_bm_regex));
  }

  // PLEASE KEEP QUIET. MEASUREMENTS IN PROGRESS.

  unsigned int baselineIndex = getGlobalBenchmarkBaselineIndex();

  auto const globalBaseline =
      runBenchmarkGetNSPerIteration(get<2>(benchmarks()[baselineIndex]), 0);
  FOR_EACH_RANGE (i, 0, benchmarks().size()) {
    if (i == baselineIndex) {
      continue;
    }
    double elapsed = 0.0;
    if (get<1>(benchmarks()[i]) != "-") { // skip separators
      if (bmRegex && !boost::regex_search(get<1>(benchmarks()[i]), *bmRegex)) {
        continue;
      }
      elapsed = runBenchmarkGetNSPerIteration(get<2>(benchmarks()[i]),
                                              globalBaseline);
    }
    results.emplace_back(get<0>(benchmarks()[i]),
                         get<1>(benchmarks()[i]), elapsed);
  }

  // PLEASE MAKE NOISE. MEASUREMENTS DONE.

  printBenchmarkResults(results);
}

} // namespace folly
