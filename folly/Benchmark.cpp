/*
 * Copyright 2012 Facebook, Inc.
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

#include "Benchmark.h"
#include "Foreach.h"
#include "String.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

using namespace std;

DEFINE_bool(benchmark, false, "Run benchmarks.");

namespace folly {

BenchmarkSuspender::NanosecondsSpent BenchmarkSuspender::nsSpent;

typedef function<uint64_t(unsigned int)> BenchmarkFun;
static vector<tuple<const char*, const char*, BenchmarkFun>> benchmarks;

// Add the global baseline
BENCHMARK(globalBenchmarkBaseline) {
  asm volatile("");
}

void detail::addBenchmarkImpl(const char* file, const char* name,
                              BenchmarkFun fun) {
  benchmarks.emplace_back(file, name, std::move(fun));
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
  static uint64_t resolutionInNs = 0, coarseResolutionInNs = 0;
  if (!resolutionInNs) {
    timespec ts;
    CHECK_EQ(0, clock_getres(detail::DEFAULT_CLOCK_ID, &ts));
    CHECK_EQ(0, ts.tv_sec) << "Clock sucks.";
    CHECK_LT(0, ts.tv_nsec) << "Clock too fast for its own good.";
    CHECK_EQ(1, ts.tv_nsec) << "Clock too coarse, upgrade your kernel.";
    resolutionInNs = ts.tv_nsec;
  }
  // Whe choose a minimum minimum (sic) of 10,000 nanoseconds, but if
  // the clock resolution is worse than that, it will be larger. In
  // essence we're aiming at making the quantization noise 0.01%.
  static const auto minNanoseconds = min(resolutionInNs * 100000, 1000000000UL);

  // We do measurements in several epochs and take the minimum, to
  // account for jitter.
  static const unsigned int epochs = 1000;
  // We establish a total time budget as we don't want a measurement
  // to take too long. This will curtail the number of actual epochs.
  static const uint64_t timeBudgetInNs = 1000000000;
  timespec global;
  CHECK_EQ(0, clock_gettime(CLOCK_REALTIME, &global));

  double epochResults[epochs] = { 0 };
  size_t actualEpochs = 0;

  for (; actualEpochs < epochs; ++actualEpochs) {
    for (unsigned int n = 1; n < (1U << 30); n *= 2) {
      auto const nsecs = fun(n);
      if (nsecs < minNanoseconds) {
        continue;
      }
      // We got an accurate enough timing, done. But only save if
      // smaller than the current result.
      epochResults[actualEpochs] = max(0.0, double(nsecs) / n - globalBaseline);
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

static string humanReadable(double n, unsigned int decimals) {
  auto a = fabs(n);
  char suffix = ' ';

  if (a >= 1E21) {
    // Too big to be comprehended by the puny human brain
    suffix = '!';
    n /= 1E21;
  } else if (a >= 1E18) {
    // "EXA" written with suffix 'X' so as to not create confusion
    // with scientific notation.
    suffix = 'X';
    n /= 1E18;
  } else if (a >= 1E15) {
    // "PETA"
    suffix = 'P';
    n /= 1E15;
  } else if (a >= 1E12) {
    // "TERA"
    suffix = 'T';
    n /= 1E12;
  } else if (a >= 1E9) {
    // "GIGA"
    suffix = 'G';
    n /= 1E9;
  } else if (a >= 1E6) {
    // "MEGA"
    suffix = 'M';
    n /= 1E6;
  } else if (a >= 1E3) {
    // "KILO"
    suffix = 'K';
    n /= 1E3;
  } else if (a == 0.0) {
    suffix = ' ';
  } else if (a < 1E-15) {
    // too small
    suffix = '?';
    n *= 1E18;
  } else if (a < 1E-12) {
    // "femto"
    suffix = 'f';
    n *= 1E15;
  } else if (a < 1E-9) {
    // "pico"
    suffix = 'p';
    n *= 1E12;
  } else if (a < 1E-6) {
    // "nano"
    suffix = 'n';
    n *= 1E9;
  } else if (a < 1E-3) {
    // "micro"
    suffix = 'u';
    n *= 1E6;
  } else if (a < 1) {
    // "mili"
    suffix = 'm';
    n *= 1E3;
  }

  return stringPrintf("%*.*f%c", decimals + 3 + 1, decimals, n, suffix);
}

static void printBenchmarkResults(
  const vector<tuple<const char*, const char*, double> >& data) {
  // Width available
  static const uint columns = 76;

  // Compute the longest benchmark name
  size_t longestName = 0;
  FOR_EACH_RANGE (i, 1, benchmarks.size()) {
    longestName = max(longestName, strlen(get<1>(benchmarks[i])));
  }

  // Print a horizontal rule
  auto separator = [&](char pad) {
    puts(string(columns, pad).c_str());
  };

  // Print header for a file
  auto header = [&](const char* file) {
    separator('=');
    printf("%-*srelative  ns/iter  iters/s\n",
           columns - 26, file);
    separator('=');
  };

  double baselineNsPerIter = numeric_limits<double>::max();
  const char* lastFile = "";

  for (auto& datum : data) {
    auto file = get<0>(datum);
    if (strcmp(file, lastFile)) {
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
    s.resize(columns - 27, ' ');
    auto nsPerIter = get<2>(datum);
    auto itersPerSec = 1E9 / nsPerIter;
    if (!useBaseline) {
      // Print without baseline
      printf("%*s           %s  %s\n",
             static_cast<int>(s.size()), s.c_str(),
             humanReadable(nsPerIter, 2).c_str(),
             humanReadable(itersPerSec, 2).c_str());
    } else {
      // Print with baseline
      auto rel = baselineNsPerIter / nsPerIter * 100.0;
      printf("%*s %7.2f%%  %s  %s\n",
             static_cast<int>(s.size()), s.c_str(),
             rel,
             humanReadable(nsPerIter, 2).c_str(),
             humanReadable(itersPerSec, 2).c_str());
    }
  }
  separator('=');
}

void runBenchmarks() {
  CHECK(!benchmarks.empty());

  vector<tuple<const char*, const char*, double>> results;
  results.reserve(benchmarks.size() - 1);

  // PLEASE KEEP QUIET. MEASUREMENTS IN PROGRESS.

  auto const globalBaseline = runBenchmarkGetNSPerIteration(
    get<2>(benchmarks.front()), 0);
  FOR_EACH_RANGE (i, 1, benchmarks.size()) {
    auto elapsed = strcmp(get<1>(benchmarks[i]), "-") == 0
      ? 0.0 // skip the separators
      : runBenchmarkGetNSPerIteration(get<2>(benchmarks[i]),
                                      globalBaseline);
    results.emplace_back(get<0>(benchmarks[i]),
                         get<1>(benchmarks[i]), elapsed);
  }

  // PLEASE MAKE NOISE. MEASUREMENTS DONE.

  printBenchmarkResults(results);
}

} // namespace folly
