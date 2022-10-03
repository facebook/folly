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

// @author Andrei Alexandrescu (andrei.alexandrescu@fb.com)

#include <folly/Benchmark.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <utility>
#include <vector>

#include <boost/regex.hpp>

#include <folly/FileUtil.h>
#include <folly/MapUtil.h>
#include <folly/String.h>
#include <folly/container/Foreach.h>
#include <folly/json.h>

using namespace std;

FOLLY_GFLAGS_DEFINE_bool(benchmark, false, "Run benchmarks.");

FOLLY_GFLAGS_DEFINE_bool(json, false, "Output in JSON format.");

FOLLY_GFLAGS_DEFINE_bool(bm_estimate_time, false, "Estimate running time");

FOLLY_GFLAGS_DEFINE_bool(
    bm_profile, false, "Run benchmarks with constant number of iterations");

FOLLY_GFLAGS_DEFINE_int64(
    bm_profile_iters, 1000, "Number of iterations for profiling");

FOLLY_GFLAGS_DEFINE_string(
    bm_relative_to,
    "",
    "Print benchmark results relative to an earlier dump (via --bm_json_verbose)");

FOLLY_GFLAGS_DEFINE_string(
    bm_json_verbose,
    "",
    "File to write verbose JSON format (for BenchmarkCompare / --bm_relative_to). "
    "NOTE: this is written independent of the above --json / --bm_relative_to.");

FOLLY_GFLAGS_DEFINE_string(
    bm_regex, "", "Only benchmarks whose names match this regex will be run.");

FOLLY_GFLAGS_DEFINE_int64(
    bm_min_usec,
    100,
    "Minimum # of microseconds we'll accept for each benchmark.");

FOLLY_GFLAGS_DEFINE_int32(
    bm_min_iters, 1, "Minimum # of iterations we'll try for each benchmark.");

FOLLY_GFLAGS_DEFINE_int64(
    bm_max_iters,
    1 << 30,
    "Maximum # of iterations we'll try for each benchmark.");

FOLLY_GFLAGS_DEFINE_int32(
    bm_max_secs, 1, "Maximum # of seconds we'll spend on each benchmark.");

FOLLY_GFLAGS_DEFINE_uint32(
    bm_result_width_chars, 76, "Width of results table in characters");

FOLLY_GFLAGS_DEFINE_uint32(
    bm_max_trials,
    1000,
    "Maximum number of trials (iterations) executed for each benchmark.");

namespace folly {

std::chrono::high_resolution_clock::duration BenchmarkSuspender::timeSpent;

typedef function<detail::TimeIterData(unsigned int)> BenchmarkFun;

vector<detail::BenchmarkRegistration>& benchmarks() {
  static vector<detail::BenchmarkRegistration> _benchmarks;
  return _benchmarks;
}

#define FB_FOLLY_GLOBAL_BENCHMARK_BASELINE fbFollyGlobalBenchmarkBaseline
#define FB_STRINGIZE_X2(x) FOLLY_PP_STRINGIZE(x)

// Add the global baseline
BENCHMARK(FB_FOLLY_GLOBAL_BENCHMARK_BASELINE) {
#ifdef _MSC_VER
  _ReadWriteBarrier();
#else
  asm volatile("");
#endif
}

size_t getGlobalBenchmarkBaselineIndex() {
  const char* global = FB_STRINGIZE_X2(FB_FOLLY_GLOBAL_BENCHMARK_BASELINE);
  auto it = std::find_if(
      benchmarks().begin(),
      benchmarks().end(),
      [global](const detail::BenchmarkRegistration& v) {
        return v.name == global;
      });
  CHECK(it != benchmarks().end());
  return size_t(std::distance(benchmarks().begin(), it));
}

#undef FB_STRINGIZE_X2
#undef FB_FOLLY_GLOBAL_BENCHMARK_BASELINE

void detail::addBenchmarkImpl(
    const char* file, StringPiece name, BenchmarkFun fun, bool useCounter) {
  benchmarks().push_back({file, name.str(), std::move(fun), useCounter});
}

static std::pair<double, UserCounters> runBenchmarkGetNSPerIteration(
    const BenchmarkFun& fun, const double globalBaseline) {
  using std::chrono::duration_cast;
  using std::chrono::high_resolution_clock;
  using std::chrono::microseconds;
  using std::chrono::nanoseconds;
  using std::chrono::seconds;

  // They key here is accuracy; too low numbers means the accuracy was
  // coarse. We up the ante until we get to at least minNanoseconds
  // timings.
  static_assert(
      std::is_same<high_resolution_clock::duration, nanoseconds>::value,
      "High resolution clock must be nanosecond resolution.");
  // We choose a minimum minimum (sic) of 100,000 nanoseconds, but if
  // the clock resolution is worse than that, it will be larger. In
  // essence we're aiming at making the quantization noise 0.01%.
  static const auto minNanoseconds = std::max<nanoseconds>(
      nanoseconds(100000), microseconds(FLAGS_bm_min_usec));

  // We establish a total time budget as we don't want a measurement
  // to take too long. This will curtail the number of actual trials.
  const auto timeBudget = seconds(FLAGS_bm_max_secs);
  auto global = high_resolution_clock::now();

  std::vector<std::pair<double, UserCounters>> trialResults(
      FLAGS_bm_max_trials);
  size_t actualTrials = 0;

  // We do measurements in several trials (epochs) and take the minimum, to
  // account for jitter.
  for (; actualTrials < FLAGS_bm_max_trials; ++actualTrials) {
    const auto maxIters = uint32_t(FLAGS_bm_max_iters);
    for (auto n = uint32_t(FLAGS_bm_min_iters); n < maxIters; n *= 2) {
      detail::TimeIterData timeIterData = fun(static_cast<unsigned int>(n));
      if (timeIterData.duration < minNanoseconds) {
        continue;
      }
      // We got an accurate enough timing, done. But only save if
      // smaller than the current result.
      auto nsecs = duration_cast<nanoseconds>(timeIterData.duration);
      trialResults[actualTrials] = std::make_pair(
          max(0.0, double(nsecs.count()) / timeIterData.niter - globalBaseline),
          std::move(timeIterData.userCounters));
      // Done with the current trial, we got a meaningful timing.
      break;
    }
    auto now = high_resolution_clock::now();
    if (now - global >= timeBudget) {
      // No more time budget available.
      ++actualTrials;
      break;
    }
  }

  // Current state of the art: get the minimum. After some
  // experimentation, it seems taking the minimum is the best.
  auto iter = min_element(
      trialResults.begin(),
      trialResults.begin() + actualTrials,
      [](const auto& a, const auto& b) { return a.first < b.first; });

  // If the benchmark was basically drowned in baseline noise, it's
  // possible it became negative.
  return std::make_pair(max(0.0, iter->first), iter->second);
}

static std::pair<double, UserCounters> runBenchmarkGetNSPerIterationEstimate(
    const BenchmarkFun& fun, const double globalBaseline) {
  using std::chrono::duration_cast;
  using std::chrono::high_resolution_clock;
  using std::chrono::microseconds;
  using std::chrono::nanoseconds;
  using std::chrono::seconds;
  using TrialResultType = std::pair<double, UserCounters>;

  // They key here is accuracy; too low numbers means the accuracy was
  // coarse. We up the ante until we get to at least minNanoseconds
  // timings.
  static_assert(
      std::is_same<high_resolution_clock::duration, nanoseconds>::value,
      "High resolution clock must be nanosecond resolution.");

  // Estimate single iteration running time for 1 sec
  double estPerIter = 0.0; // Estimated nanosec per iteration
  auto estStart = high_resolution_clock::now();
  const auto estBudget = seconds(1);
  for (auto n = 1; n < 1000; n *= 2) {
    detail::TimeIterData timeIterData = fun(static_cast<unsigned int>(n));
    auto now = high_resolution_clock::now();
    auto nsecs = duration_cast<nanoseconds>(timeIterData.duration);
    estPerIter = double(nsecs.count() - globalBaseline) / n;
    if (now - estStart > estBudget) {
      break;
    }
  }
  // Can't estimate running time, so make it a baseline
  if (estPerIter <= 0.0) {
    estPerIter = globalBaseline;
  }

  // We do measurements in several trials (epochs) to account for jitter.
  size_t actualTrials = 0;
  const unsigned int estimateCount = to_integral(max(1.0, 5e+7 / estPerIter));
  std::vector<TrialResultType> trialResults(FLAGS_bm_max_trials);
  const auto maxRunTime = seconds(5);
  auto globalStart = high_resolution_clock::now();

  // Run benchmark up to trial times with at least 0.5 sec each
  // Or until we run out of alowed time (5sec)
  for (size_t tryId = 0; tryId < FLAGS_bm_max_trials; tryId++) {
    detail::TimeIterData timeIterData = fun(estimateCount);
    auto nsecs = duration_cast<nanoseconds>(timeIterData.duration);

    if (nsecs.count() > globalBaseline) {
      auto nsecIter =
          double(nsecs.count() - globalBaseline) / timeIterData.niter;
      trialResults[actualTrials++] =
          std::make_pair(nsecIter, std::move(timeIterData.userCounters));
    }
    // Check if we are out of time quota
    auto now = high_resolution_clock::now();
    if (now - globalStart > maxRunTime) {
      break;
    }
  }

  // Sort results by running time
  std::sort(
      trialResults.begin(),
      trialResults.begin() + actualTrials,
      [](const TrialResultType& a, const TrialResultType& b) {
        return a.first < b.first;
      });

  const auto getPercentile = [](size_t count, double p) -> size_t {
    return static_cast<size_t>(count * p);
  };

  const size_t trialP25 = getPercentile(actualTrials, 0.25);
  const size_t trialP75 = getPercentile(actualTrials, 0.75);
  if (trialP75 - trialP25 == 0) {
    // Use first trial results if p75 == p25.
    return std::make_pair(trialResults[0].first, trialResults[0].second);
  }

  double geomeanNsec = 0.0;
  for (size_t tryId = trialP25; tryId < trialP75; tryId++) {
    geomeanNsec += std::log(trialResults[tryId].first);
  }
  geomeanNsec = std::exp(geomeanNsec / (1.0 * (trialP75 - trialP25)));

  return std::make_pair(
      geomeanNsec, trialResults[trialP25 + (trialP75 - trialP25) / 2].second);
}

static std::pair<double, UserCounters> runProfilingGetNSPerIteration(
    const BenchmarkFun& fun, const double globalBaseline) {
  using std::chrono::duration_cast;
  using std::chrono::high_resolution_clock;
  using std::chrono::nanoseconds;

  // They key here is accuracy; too low numbers means the accuracy was
  // coarse. We up the ante until we get to at least minNanoseconds
  // timings.
  static_assert(
      std::is_same<high_resolution_clock::duration, nanoseconds>::value,
      "High resolution clock must be nanosecond resolution.");

  // This is a very simple measurement with a single epoch
  // and should be used only for profiling purposes
  detail::TimeIterData timeIterData = fun(FLAGS_bm_profile_iters);

  auto nsecs = duration_cast<nanoseconds>(timeIterData.duration);
  auto nsecIter = double(nsecs.count()) / timeIterData.niter - globalBaseline;
  return std::make_pair(nsecIter, std::move(timeIterData.userCounters));
}

struct ScaleInfo {
  double boundary;
  const char* suffix;
};

static const ScaleInfo kTimeSuffixes[]{
    {365.25 * 24 * 3600, "years"},
    {24 * 3600, "days"},
    {3600, "hr"},
    {60, "min"},
    {1, "s"},
    {1E-3, "ms"},
    {1E-6, "us"},
    {1E-9, "ns"},
    {1E-12, "ps"},
    {1E-15, "fs"},
    {0, nullptr},
};

static const ScaleInfo kMetricSuffixes[]{
    {1E24, "Y"}, // yotta
    {1E21, "Z"}, // zetta
    {1E18, "X"}, // "exa" written with suffix 'X' so as to not create
                 //   confusion with scientific notation
    {1E15, "P"}, // peta
    {1E12, "T"}, // terra
    {1E9, "G"}, // giga
    {1E6, "M"}, // mega
    {1E3, "K"}, // kilo
    {1, ""},
    {1E-3, "m"}, // milli
    {1E-6, "u"}, // micro
    {1E-9, "n"}, // nano
    {1E-12, "p"}, // pico
    {1E-15, "f"}, // femto
    {1E-18, "a"}, // atto
    {1E-21, "z"}, // zepto
    {1E-24, "y"}, // yocto
    {0, nullptr},
};

static string humanReadable(
    double n, unsigned int decimals, const ScaleInfo* scales) {
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

namespace {

constexpr std::string_view kUnitHeaders = "relative  time/iter   iters/s";
constexpr std::string_view kUnitHeadersPadding = "     ";
void printHeaderContents(std::string_view file) {
  printf(
      "%-.*s%*s%*s",
      static_cast<int>(file.size()),
      file.data(),
      static_cast<int>(kUnitHeadersPadding.size()),
      kUnitHeadersPadding.data(),
      static_cast<int>(kUnitHeaders.size()),
      kUnitHeaders.data());
}

void printDefaultHeaderContents(std::string_view file, size_t columns) {
  const size_t maxFileNameChars =
      columns - kUnitHeaders.size() - kUnitHeadersPadding.size();

  if (file.size() <= maxFileNameChars) {
    printHeaderContents(file);
  } else {
    std::string truncatedFile = std::string(file.begin(), file.end());
    constexpr std::string_view overflowFilePrefix = "[...]";
    const int overflow = truncatedFile.size() - maxFileNameChars;
    truncatedFile.erase(0, overflow);
    truncatedFile.replace(0, overflowFilePrefix.size(), overflowFilePrefix);
    printHeaderContents(truncatedFile);
  }
}

void printSeparator(char pad, unsigned int columns) {
  puts(string(columns, pad).c_str());
}

class BenchmarkResultsPrinter {
 public:
  BenchmarkResultsPrinter() : columns_(FLAGS_bm_result_width_chars) {}
  explicit BenchmarkResultsPrinter(std::set<std::string> counterNames)
      : counterNames_(std::move(counterNames)),
        namesLength_{std::accumulate(
            counterNames_.begin(),
            counterNames_.end(),
            size_t{0},
            [](size_t acc, auto&& name) { return acc + 2 + name.length(); })},
        columns_(FLAGS_bm_result_width_chars + namesLength_) {}

  void separator(char pad) { printSeparator(pad, columns_); }

  void header(std::string_view file) {
    separator('=');
    printDefaultHeaderContents(file, columns_);
    for (auto const& name : counterNames_) {
      printf("  %s", name.c_str());
    }
    printf("\n");
    separator('=');
  }

  void print(const vector<detail::BenchmarkResult>& data) {
    for (auto& datum : data) {
      auto file = datum.file;
      if (file != lastFile_) {
        // New file starting
        header(file);
        lastFile_ = file;
      }

      string s = datum.name;
      if (s == "-") {
        separator('-');
        continue;
      }
      bool useBaseline = false;
      // '%' indicates a relative benchmark.
      if (s[0] == '%') {
        s.erase(0, 1);
        useBaseline = isBaselineSet();
      } else {
        baselineNsPerIter_ = datum.timeInNs;
        useBaseline = false;
      }
      s.resize(columns_ - kUnitHeaders.size(), ' ');
      const auto nsPerIter = datum.timeInNs;
      const auto secPerIter = nsPerIter / 1E9;
      const auto itersPerSec = (secPerIter == 0)
          ? std::numeric_limits<double>::infinity()
          : (1 / secPerIter);
      if (!useBaseline) {
        // Print without baseline
        printf(
            "%*s%8.8s  %9.9s  %8.8s",
            static_cast<int>(s.size()),
            s.c_str(),
            "", // Padding for "relative" header.
            readableTime(secPerIter, 2).c_str(),
            metricReadable(itersPerSec, 2).c_str());
      } else {
        // Print with baseline
        const auto rel = baselineNsPerIter_ / nsPerIter * 100.0;
        printf(
            "%*s%7.5g%%  %9.9s  %8.8s",
            static_cast<int>(s.size()),
            s.c_str(),
            rel,
            readableTime(secPerIter, 2).c_str(),
            metricReadable(itersPerSec, 2).c_str());
      }
      for (auto const& name : counterNames_) {
        if (auto ptr = folly::get_ptr(datum.counters, name)) {
          switch (ptr->type) {
            case UserMetric::Type::TIME:
              printf(
                  "  %*s",
                  int(name.length()),
                  readableTime(ptr->value, 2).c_str());
              break;
            case UserMetric::Type::METRIC:
              printf(
                  "  %*s",
                  int(name.length()),
                  metricReadable(ptr->value, 2).c_str());
              break;
            case UserMetric::Type::CUSTOM:
            default:
              printf("  %*" PRId64, int(name.length()), ptr->value);
          }
        } else {
          printf("  %*s", int(name.length()), "NaN");
        }
      }
      printf("\n");
    }
  }

 private:
  bool isBaselineSet() {
    return baselineNsPerIter_ != numeric_limits<double>::max();
  }

  std::set<std::string> counterNames_;
  size_t namesLength_{0};
  size_t columns_{0};
  double baselineNsPerIter_{numeric_limits<double>::max()};
  string lastFile_;
};
} // namespace

static void printBenchmarkResultsAsJson(
    const vector<detail::BenchmarkResult>& data) {
  dynamic d = dynamic::object;
  for (auto& datum : data) {
    d[datum.name] = datum.timeInNs * 1000.;
  }

  printf("%s\n", toPrettyJson(d).c_str());
}

void benchmarkResultsToDynamic(
    const vector<detail::BenchmarkResult>& data, dynamic& out) {
  out = dynamic::array;
  for (auto& datum : data) {
    if (!datum.counters.empty()) {
      dynamic obj = dynamic::object;
      for (auto& counter : datum.counters) {
        dynamic counterInfo = dynamic::object;
        counterInfo["value"] = counter.second.value;
        counterInfo["type"] = static_cast<int>(counter.second.type);
        obj[counter.first] = counterInfo;
      }
      out.push_back(
          dynamic::array(datum.file, datum.name, datum.timeInNs, obj));
    } else {
      out.push_back(dynamic::array(datum.file, datum.name, datum.timeInNs));
    }
  }
}

void benchmarkResultsFromDynamic(
    const dynamic& d, vector<detail::BenchmarkResult>& results) {
  for (auto& datum : d) {
    results.push_back(
        {datum[0].asString(),
         datum[1].asString(),
         datum[2].asDouble(),
         UserCounters{}});
  }
}

static pair<StringPiece, StringPiece> resultKey(
    const detail::BenchmarkResult& result) {
  return pair<StringPiece, StringPiece>(result.file, result.name);
}

void printResultComparison(
    const vector<detail::BenchmarkResult>& base,
    const vector<detail::BenchmarkResult>& test) {
  map<pair<StringPiece, StringPiece>, double> baselines;

  for (auto& baseResult : base) {
    baselines[resultKey(baseResult)] = baseResult.timeInNs;
  }

  // Width available
  const size_t columns = FLAGS_bm_result_width_chars;

  auto header = [&](const string_view& file) {
    printSeparator('=', columns);
    printDefaultHeaderContents(file, columns);
    printf("\n");
    printSeparator('=', columns);
  };

  string lastFile;

  for (auto& datum : test) {
    folly::Optional<double> baseline =
        folly::get_optional(baselines, resultKey(datum));
    auto file = datum.file;
    if (file != lastFile) {
      // New file starting
      header(file);
      lastFile = file;
    }

    string s = datum.name;
    if (s == "-") {
      printSeparator('-', columns);
      continue;
    }
    if (s[0] == '%') {
      s.erase(0, 1);
    }
    s.resize(columns - 29, ' ');
    auto nsPerIter = datum.timeInNs;
    auto secPerIter = nsPerIter / 1E9;
    auto itersPerSec = (secPerIter == 0)
        ? std::numeric_limits<double>::infinity()
        : (1 / secPerIter);
    if (!baseline) {
      // Print without baseline
      printf(
          "%*s           %9s  %7s\n",
          static_cast<int>(s.size()),
          s.c_str(),
          readableTime(secPerIter, 2).c_str(),
          metricReadable(itersPerSec, 2).c_str());
    } else {
      // Print with baseline
      auto rel = *baseline / nsPerIter * 100.0;
      printf(
          "%*s %7.2f%%  %9s  %7s\n",
          static_cast<int>(s.size()),
          s.c_str(),
          rel,
          readableTime(secPerIter, 2).c_str(),
          metricReadable(itersPerSec, 2).c_str());
    }
  }
  printSeparator('=', columns);
}

void checkRunMode() {
  if (folly::kIsDebug || folly::kIsSanitize) {
    std::cerr << "WARNING: Benchmark running "
              << (folly::kIsDebug ? "in DEBUG mode" : "with SANITIZERS")
              << std::endl;
  }
}

namespace {

std::pair<std::set<std::string>, std::vector<detail::BenchmarkResult>>
runBenchmarksWithPrinter(BenchmarkResultsPrinter* FOLLY_NULLABLE printer) {
  vector<detail::BenchmarkResult> results;
  results.reserve(benchmarks().size() - 1);

  std::unique_ptr<boost::regex> bmRegex;
  if (!FLAGS_bm_regex.empty()) {
    bmRegex = std::make_unique<boost::regex>(FLAGS_bm_regex);
  }

  // PLEASE KEEP QUIET. MEASUREMENTS IN PROGRESS.

  size_t baselineIndex = getGlobalBenchmarkBaselineIndex();

  auto const globalBaseline =
      runBenchmarkGetNSPerIteration(benchmarks()[baselineIndex].func, 0);

  std::set<std::string> counterNames;
  FOR_EACH_RANGE (i, 0, benchmarks().size()) {
    if (i == baselineIndex) {
      continue;
    }
    std::pair<double, UserCounters> elapsed;
    auto& bm = benchmarks()[i];
    if (bm.name != "-") { // skip separators
      if (bmRegex && !boost::regex_search(bm.name, *bmRegex)) {
        continue;
      }
      if (FLAGS_bm_profile) {
        elapsed = runProfilingGetNSPerIteration(bm.func, globalBaseline.first);
      } else {
        elapsed = FLAGS_bm_estimate_time
            ? runBenchmarkGetNSPerIterationEstimate(
                  bm.func, globalBaseline.first)
            : runBenchmarkGetNSPerIteration(bm.func, globalBaseline.first);
      }
    }

    // if customized user counters is used, it cannot print the result in real
    // time as it needs to run all cases first to know the complete set of
    // counters have been used, then the header can be printed out properly
    if (printer != nullptr) {
      printer->print({{bm.file, bm.name, elapsed.first, elapsed.second}});
    }
    results.push_back({bm.file, bm.name, elapsed.first, elapsed.second});

    // get all counter names
    for (auto const& kv : elapsed.second) {
      counterNames.insert(kv.first);
    }
  }

  // MEASUREMENTS DONE.

  return std::make_pair(std::move(counterNames), std::move(results));
}

std::vector<detail::BenchmarkResult> resultsFromFile(
    const std::string& filename) {
  std::string content;
  readFile(filename.c_str(), content);
  std::vector<detail::BenchmarkResult> ret;
  if (!content.empty()) {
    benchmarkResultsFromDynamic(parseJson(content), ret);
  }
  return ret;
}

bool writeResultsToFile(
    const std::vector<detail::BenchmarkResult>& results,
    const std::string& filename) {
  dynamic d;
  benchmarkResultsToDynamic(results, d);
  return writeFile(toPrettyJson(d), filename.c_str());
}

} // namespace

namespace detail {

std::vector<BenchmarkResult> runBenchmarksWithResults() {
  return runBenchmarksWithPrinter(nullptr).second;
}

} // namespace detail

void runBenchmarks() {
  CHECK(!benchmarks().empty());

  if (FLAGS_bm_profile) {
    printf(
        "WARNING: Running with constant number of iterations. Results might be jittery.\n");
  }

  checkRunMode();

  BenchmarkResultsPrinter printer;
  bool useCounter =
      std::any_of(benchmarks().begin(), benchmarks().end(), [](const auto& bm) {
        return bm.useCounter;
      });
  // PLEASE KEEP QUIET. MEASUREMENTS IN PROGRESS.

  const bool shouldPrintInline =
      FLAGS_bm_relative_to.empty() && !FLAGS_json && !useCounter;
  auto benchmarkResults =
      runBenchmarksWithPrinter(shouldPrintInline ? &printer : nullptr);

  // PLEASE MAKE NOISE. MEASUREMENTS DONE.

  if (FLAGS_json) {
    printBenchmarkResultsAsJson(benchmarkResults.second);
  } else if (!FLAGS_bm_relative_to.empty()) {
    printResultComparison(
        resultsFromFile(FLAGS_bm_relative_to), benchmarkResults.second);
  } else if (!shouldPrintInline) {
    printer = BenchmarkResultsPrinter{std::move(benchmarkResults.first)};
    printer.print(benchmarkResults.second);
    printer.separator('=');
  }

  if (!FLAGS_bm_json_verbose.empty()) {
    writeResultsToFile(benchmarkResults.second, FLAGS_bm_json_verbose);
  }

  checkRunMode();
}

} // namespace folly
