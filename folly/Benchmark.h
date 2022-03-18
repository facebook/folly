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

#pragma once

#include <folly/BenchmarkUtil.h>
#include <folly/Portability.h>
#include <folly/Preprocessor.h> // for FB_ANONYMOUS_VARIABLE
#include <folly/Range.h>
#include <folly/ScopeGuard.h>
#include <folly/Traits.h>
#include <folly/functional/Invoke.h>
#include <folly/lang/Hint.h>
#include <folly/portability/GFlags.h>

#include <cassert>
#include <chrono>
#include <functional>
#include <limits>
#include <type_traits>
#include <unordered_map>

#include <boost/function_types/function_arity.hpp>
#include <glog/logging.h>

DECLARE_bool(benchmark);
DECLARE_uint32(bm_result_width_chars);

namespace folly {

/**
 * Runs all benchmarks defined. Usually put in main().
 */
void runBenchmarks();

/**
 * Runs all benchmarks defined if and only if the --benchmark flag has
 * been passed to the program. Usually put in main().
 */
inline bool runBenchmarksOnFlag() {
  if (FLAGS_benchmark) {
    runBenchmarks();
  }
  return FLAGS_benchmark;
}

class UserMetric {
 public:
  enum class Type { CUSTOM, TIME, METRIC };

  int64_t value{};
  Type type{Type::CUSTOM};

  UserMetric() = default;
  /* implicit */ UserMetric(int64_t val, Type typ = Type::CUSTOM)
      : value(val), type(typ) {}
};

using UserCounters = std::unordered_map<std::string, UserMetric>;

namespace detail {
struct TimeIterData {
  std::chrono::high_resolution_clock::duration duration;
  unsigned int niter;
  UserCounters userCounters;
};

using BenchmarkFun = std::function<TimeIterData(unsigned int)>;

struct BenchmarkRegistration {
  std::string file;
  std::string name;
  BenchmarkFun func;
  bool useCounter = false;
};

struct BenchmarkResult {
  std::string file;
  std::string name;
  double timeInNs;
  UserCounters counters;
};

/**
 * Adds a benchmark wrapped in a std::function. Only used
 * internally. Pass by value is intentional.
 */
void addBenchmarkImpl(
    const char* file, StringPiece name, BenchmarkFun, bool useCounter);

/**
 * Runs all benchmarks defined in the program, doesn't print by default.
 * Usually used when customized printing of results is desired.
 */
std::vector<BenchmarkResult> runBenchmarksWithResults();

} // namespace detail

/**
 * Supporting type for BENCHMARK_SUSPEND defined below.
 */
struct BenchmarkSuspender {
  using Clock = std::chrono::high_resolution_clock;
  using TimePoint = Clock::time_point;
  using Duration = Clock::duration;

  struct DismissedTag {};
  static inline constexpr DismissedTag Dismissed{};

  BenchmarkSuspender() : start(Clock::now()) {}

  explicit BenchmarkSuspender(DismissedTag) : start(TimePoint{}) {}

  BenchmarkSuspender(const BenchmarkSuspender&) = delete;
  BenchmarkSuspender(BenchmarkSuspender&& rhs) noexcept {
    start = rhs.start;
    rhs.start = {};
  }

  BenchmarkSuspender& operator=(const BenchmarkSuspender&) = delete;
  BenchmarkSuspender& operator=(BenchmarkSuspender&& rhs) noexcept {
    if (start != TimePoint{}) {
      tally();
    }
    start = rhs.start;
    rhs.start = {};
    return *this;
  }

  ~BenchmarkSuspender() {
    if (start != TimePoint{}) {
      tally();
    }
  }

  void dismiss() {
    assert(start != TimePoint{});
    tally();
    start = {};
  }

  void rehire() {
    assert(start == TimePoint{});
    start = Clock::now();
  }

  template <class F>
  auto dismissing(F f) -> invoke_result_t<F> {
    SCOPE_EXIT { rehire(); };
    dismiss();
    return f();
  }

  /**
   * This is for use inside of if-conditions, used in BENCHMARK macros.
   * If-conditions bypass the explicit on operator bool.
   */
  explicit operator bool() const { return false; }

  /**
   * Accumulates time spent outside benchmark.
   */
  static Duration timeSpent;

 private:
  void tally() {
    auto end = Clock::now();
    timeSpent += end - start;
    start = end;
  }

  TimePoint start;
};

/**
 * Adds a benchmark. Usually not called directly but instead through
 * the macro BENCHMARK defined below. The lambda function involved
 * must take exactly one parameter of type unsigned, and the benchmark
 * uses it with counter semantics (iteration occurs inside the
 * function).
 */
template <typename Lambda>
typename std::enable_if<folly::is_invocable_v<Lambda, unsigned>>::type
addBenchmark(const char* file, StringPiece name, Lambda&& lambda) {
  auto execute = [=](unsigned int times) {
    BenchmarkSuspender::timeSpent = {};
    unsigned int niter;

    // CORE MEASUREMENT STARTS
    auto start = std::chrono::high_resolution_clock::now();
    niter = lambda(times);
    auto end = std::chrono::high_resolution_clock::now();
    // CORE MEASUREMENT ENDS
    return detail::TimeIterData{
        (end - start) - BenchmarkSuspender::timeSpent, niter, UserCounters{}};
  };

  detail::addBenchmarkImpl(file, name, detail::BenchmarkFun(execute), false);
}

/**
 * Adds a benchmark. Usually not called directly but instead through
 * the macro BENCHMARK defined below. The lambda function involved
 * must take zero parameters, and the benchmark calls it repeatedly
 * (iteration occurs outside the function).
 */
template <typename Lambda>
typename std::enable_if<folly::is_invocable_v<Lambda>>::type addBenchmark(
    const char* file, StringPiece name, Lambda&& lambda) {
  addBenchmark(file, name, [=](unsigned int times) {
    unsigned int niter = 0;
    while (times-- > 0) {
      niter += lambda();
    }
    return niter;
  });
}

/**
 * similar as previous two template specialization, but lambda will also take
 * customized counters in the following two cases
 */
template <typename Lambda>
typename std::enable_if<
    folly::is_invocable_v<Lambda, UserCounters&, unsigned>>::type
addBenchmark(const char* file, StringPiece name, Lambda&& lambda) {
  auto execute = [=](unsigned int times) {
    BenchmarkSuspender::timeSpent = {};
    unsigned int niter;

    // CORE MEASUREMENT STARTS
    auto start = std::chrono::high_resolution_clock::now();
    UserCounters counters;
    niter = lambda(counters, times);
    auto end = std::chrono::high_resolution_clock::now();
    // CORE MEASUREMENT ENDS
    return detail::TimeIterData{
        (end - start) - BenchmarkSuspender::timeSpent, niter, counters};
  };

  detail::addBenchmarkImpl(
      file,
      name,
      std::function<detail::TimeIterData(unsigned int)>(execute),
      true);
}

template <typename Lambda>
typename std::enable_if<folly::is_invocable_v<Lambda, UserCounters&>>::type
addBenchmark(const char* file, StringPiece name, Lambda&& lambda) {
  addBenchmark(file, name, [=](UserCounters& counters, unsigned int times) {
    unsigned int niter = 0;
    while (times-- > 0) {
      niter += lambda(counters);
    }
    return niter;
  });
}

struct dynamic;

void benchmarkResultsToDynamic(
    const std::vector<detail::BenchmarkResult>& data, dynamic&);

void benchmarkResultsFromDynamic(
    const dynamic&, std::vector<detail::BenchmarkResult>&);

void printResultComparison(
    const std::vector<detail::BenchmarkResult>& base,
    const std::vector<detail::BenchmarkResult>& test);

} // namespace folly

/**
 * Introduces a benchmark function. Used internally, see BENCHMARK and
 * friends below.
 */

#define BENCHMARK_IMPL(funName, stringName, rv, paramType, paramName)          \
  static void funName(paramType);                                              \
  FOLLY_MAYBE_UNUSED static bool FB_ANONYMOUS_VARIABLE(follyBenchmarkUnused) = \
      (::folly::addBenchmark(                                                  \
           __FILE__,                                                           \
           stringName,                                                         \
           [](paramType paramName) -> unsigned {                               \
             funName(paramName);                                               \
             return rv;                                                        \
           }),                                                                 \
       true);                                                                  \
  static void funName(paramType paramName)

#define BENCHMARK_IMPL_COUNTERS(                                               \
    funName, stringName, counters, rv, paramType, paramName)                   \
  static void funName(                                                         \
      ::folly::UserCounters& FOLLY_PP_DETAIL_APPEND_VA_ARG(paramType));        \
  FOLLY_MAYBE_UNUSED static bool FB_ANONYMOUS_VARIABLE(follyBenchmarkUnused) = \
      (::folly::addBenchmark(                                                  \
           __FILE__,                                                           \
           stringName,                                                         \
           [](::folly::UserCounters& counters FOLLY_PP_DETAIL_APPEND_VA_ARG(   \
               paramType paramName)) -> unsigned {                             \
             funName(counters FOLLY_PP_DETAIL_APPEND_VA_ARG(paramName));       \
             return rv;                                                        \
           }),                                                                 \
       true);                                                                  \
  static void funName(::folly::UserCounters& counters                          \
                          FOLLY_PP_DETAIL_APPEND_VA_ARG(paramType paramName))

/**
 * Introduces a benchmark function with support for returning the actual
 * number of iterations. Used internally, see BENCHMARK_MULTI and friends
 * below.
 */
#define BENCHMARK_MULTI_IMPL(funName, stringName, paramType, paramName)        \
  static unsigned funName(paramType);                                          \
  FOLLY_MAYBE_UNUSED static bool FB_ANONYMOUS_VARIABLE(follyBenchmarkUnused) = \
      (::folly::addBenchmark(                                                  \
           __FILE__,                                                           \
           stringName,                                                         \
           [](paramType paramName) { return funName(paramName); }),            \
       true);                                                                  \
  static unsigned funName(paramType paramName)

/**
 * Introduces a benchmark function. Use with either one or two arguments.
 * The first is the name of the benchmark. Use something descriptive, such
 * as insertVectorBegin. The second argument may be missing, or could be a
 * symbolic counter. The counter dictates how many internal iteration the
 * benchmark does. Example:
 *
 * BENCHMARK(vectorPushBack) {
 *   vector<int> v;
 *   v.push_back(42);
 * }
 *
 * BENCHMARK(insertVectorBegin, iters) {
 *   vector<int> v;
 *   FOR_EACH_RANGE (i, 0, iters) {
 *     v.insert(v.begin(), 42);
 *   }
 * }
 */
#define BENCHMARK(name, ...)                   \
  BENCHMARK_IMPL(                              \
      name,                                    \
      FOLLY_PP_STRINGIZE(name),                \
      FB_ARG_2_OR_1(1, ##__VA_ARGS__),         \
      FB_ONE_OR_NONE(unsigned, ##__VA_ARGS__), \
      __VA_ARGS__)

/**
 * Allow users to record customized counter during benchmarking,
 * there will be one extra column showing in the output result for each counter
 *
 * BENCHMARK_COUNTERS(insertVectorBegin, couters, iters) {
 *   vector<int> v;
 *   FOR_EACH_RANGE (i, 0, iters) {
 *     v.insert(v.begin(), 42);
 *   }
 *   BENCHMARK_SUSPEND {
 *      counters["foo"] = 10;
 *   }
 * }
 */
#define BENCHMARK_COUNTERS(name, counters, ...) \
  BENCHMARK_IMPL_COUNTERS(                      \
      name,                                     \
      FOLLY_PP_STRINGIZE(name),                 \
      counters,                                 \
      FB_ARG_2_OR_1(1, ##__VA_ARGS__),          \
      FB_ONE_OR_NONE(unsigned, ##__VA_ARGS__),  \
      __VA_ARGS__)
/**
 * Like BENCHMARK above, but allows the user to return the actual
 * number of iterations executed in the function body. This can be
 * useful if the benchmark function doesn't know upfront how many
 * iterations it's going to run or if it runs through a certain
 * number of test cases, e.g.:
 *
 * BENCHMARK_MULTI(benchmarkSomething) {
 *   std::vector<int> testCases { 0, 1, 1, 2, 3, 5 };
 *   for (int c : testCases) {
 *     doSomething(c);
 *   }
 *   return testCases.size();
 * }
 */
#define BENCHMARK_MULTI(name, ...)             \
  BENCHMARK_MULTI_IMPL(                        \
      name,                                    \
      FOLLY_PP_STRINGIZE(name),                \
      FB_ONE_OR_NONE(unsigned, ##__VA_ARGS__), \
      __VA_ARGS__)

/**
 * Defines a benchmark that passes a parameter to another one. This is
 * common for benchmarks that need a "problem size" in addition to
 * "number of iterations". Consider:
 *
 * void pushBack(uint32_t n, size_t initialSize) {
 *   vector<int> v;
 *   BENCHMARK_SUSPEND {
 *     v.resize(initialSize);
 *   }
 *   FOR_EACH_RANGE (i, 0, n) {
 *    v.push_back(i);
 *   }
 * }
 * BENCHMARK_PARAM(pushBack, 0)
 * BENCHMARK_PARAM(pushBack, 1000)
 * BENCHMARK_PARAM(pushBack, 1000000)
 *
 * The benchmark above estimates the speed of push_back at different
 * initial sizes of the vector. The framework will pass 0, 1000, and
 * 1000000 for initialSize, and the iteration count for n.
 */
#define BENCHMARK_PARAM(name, param) BENCHMARK_NAMED_PARAM(name, param, param)

/**
 * Same as BENCHMARK_PARAM, but allows one to return the actual number of
 * iterations that have been run.
 */
#define BENCHMARK_PARAM_MULTI(name, param) \
  BENCHMARK_NAMED_PARAM_MULTI(name, param, param)

/*
 * Like BENCHMARK_PARAM(), but allows a custom name to be specified for each
 * parameter, rather than using the parameter value.
 *
 * Useful when the parameter value is not a valid token for string pasting,
 * of when you want to specify multiple parameter arguments.
 *
 * For example:
 *
 * void addValue(uint32_t n, int64_t bucketSize, int64_t min, int64_t max) {
 *   Histogram<int64_t> hist(bucketSize, min, max);
 *   int64_t num = min;
 *   FOR_EACH_RANGE (i, 0, n) {
 *     hist.addValue(num);
 *     ++num;
 *     if (num > max) { num = min; }
 *   }
 * }
 *
 * BENCHMARK_NAMED_PARAM(addValue, 0_to_100, 1, 0, 100)
 * BENCHMARK_NAMED_PARAM(addValue, 0_to_1000, 10, 0, 1000)
 * BENCHMARK_NAMED_PARAM(addValue, 5k_to_20k, 250, 5000, 20000)
 */
#define BENCHMARK_NAMED_PARAM(name, param_name, ...)                   \
  BENCHMARK_IMPL(                                                      \
      FB_CONCATENATE(name, FB_CONCATENATE(_, param_name)),             \
      FOLLY_PP_STRINGIZE(name) "(" FOLLY_PP_STRINGIZE(param_name) ")", \
      iters,                                                           \
      unsigned,                                                        \
      iters) {                                                         \
    name(iters, ##__VA_ARGS__);                                        \
  }

/**
 * Same as BENCHMARK_NAMED_PARAM, but allows one to return the actual number
 * of iterations that have been run.
 */
#define BENCHMARK_NAMED_PARAM_MULTI(name, param_name, ...)             \
  BENCHMARK_MULTI_IMPL(                                                \
      FB_CONCATENATE(name, FB_CONCATENATE(_, param_name)),             \
      FOLLY_PP_STRINGIZE(name) "(" FOLLY_PP_STRINGIZE(param_name) ")", \
      unsigned,                                                        \
      iters) {                                                         \
    return name(iters, ##__VA_ARGS__);                                 \
  }

/**
 * Just like BENCHMARK, but prints the time relative to a
 * baseline. The baseline is the most recent BENCHMARK() seen in
 * the current scope. Example:
 *
 * // This is the baseline
 * BENCHMARK(insertVectorBegin, n) {
 *   vector<int> v;
 *   FOR_EACH_RANGE (i, 0, n) {
 *     v.insert(v.begin(), 42);
 *   }
 * }
 *
 * BENCHMARK_RELATIVE(insertListBegin, n) {
 *   list<int> s;
 *   FOR_EACH_RANGE (i, 0, n) {
 *     s.insert(s.begin(), 42);
 *   }
 * }
 *
 * Any number of relative benchmark can be associated with a
 * baseline. Another BENCHMARK() occurrence effectively establishes a
 * new baseline.
 */
#define BENCHMARK_RELATIVE(name, ...)          \
  BENCHMARK_IMPL(                              \
      name,                                    \
      "%" FOLLY_PP_STRINGIZE(name),            \
      FB_ARG_2_OR_1(1, ##__VA_ARGS__),         \
      FB_ONE_OR_NONE(unsigned, ##__VA_ARGS__), \
      __VA_ARGS__)

#define BENCHMARK_COUNTERS_RELATIVE(name, counters, ...) \
  BENCHMARK_IMPL_COUNTERS(                               \
      name,                                              \
      "%" FOLLY_PP_STRINGIZE(name),                      \
      counters,                                          \
      FB_ARG_2_OR_1(1, ##__VA_ARGS__),                   \
      FB_ONE_OR_NONE(unsigned, ##__VA_ARGS__),           \
      __VA_ARGS__)
/**
 * Same as BENCHMARK_RELATIVE, but allows one to return the actual number
 * of iterations that have been run.
 */
#define BENCHMARK_RELATIVE_MULTI(name, ...)    \
  BENCHMARK_MULTI_IMPL(                        \
      name,                                    \
      "%" FOLLY_PP_STRINGIZE(name),            \
      FB_ONE_OR_NONE(unsigned, ##__VA_ARGS__), \
      __VA_ARGS__)

/**
 * A combination of BENCHMARK_RELATIVE and BENCHMARK_PARAM.
 */
#define BENCHMARK_RELATIVE_PARAM(name, param) \
  BENCHMARK_RELATIVE_NAMED_PARAM(name, param, param)

/**
 * Same as BENCHMARK_RELATIVE_PARAM, but allows one to return the actual
 * number of iterations that have been run.
 */
#define BENCHMARK_RELATIVE_PARAM_MULTI(name, param) \
  BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(name, param, param)

/**
 * A combination of BENCHMARK_RELATIVE and BENCHMARK_NAMED_PARAM.
 */
#define BENCHMARK_RELATIVE_NAMED_PARAM(name, param_name, ...)              \
  BENCHMARK_IMPL(                                                          \
      FB_CONCATENATE(name, FB_CONCATENATE(_, param_name)),                 \
      "%" FOLLY_PP_STRINGIZE(name) "(" FOLLY_PP_STRINGIZE(param_name) ")", \
      iters,                                                               \
      unsigned,                                                            \
      iters) {                                                             \
    name(iters, ##__VA_ARGS__);                                            \
  }

/**
 * Same as BENCHMARK_RELATIVE_NAMED_PARAM, but allows one to return the
 * actual number of iterations that have been run.
 */
#define BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(name, param_name, ...)        \
  BENCHMARK_MULTI_IMPL(                                                    \
      FB_CONCATENATE(name, FB_CONCATENATE(_, param_name)),                 \
      "%" FOLLY_PP_STRINGIZE(name) "(" FOLLY_PP_STRINGIZE(param_name) ")", \
      unsigned,                                                            \
      iters) {                                                             \
    return name(iters, ##__VA_ARGS__);                                     \
  }

/**
 * Draws a line of dashes.
 */
#define BENCHMARK_DRAW_LINE()                                                  \
  FOLLY_MAYBE_UNUSED static bool FB_ANONYMOUS_VARIABLE(follyBenchmarkUnused) = \
      (::folly::addBenchmark(__FILE__, "-", []() -> unsigned { return 0; }),   \
       true)

/**
 * Allows execution of code that doesn't count torward the benchmark's
 * time budget. Example:
 *
 * BENCHMARK_START_GROUP(insertVectorBegin, n) {
 *   vector<int> v;
 *   BENCHMARK_SUSPEND {
 *     v.reserve(n);
 *   }
 *   FOR_EACH_RANGE (i, 0, n) {
 *     v.insert(v.begin(), 42);
 *   }
 * }
 */
#define BENCHMARK_SUSPEND                             \
  if (auto FB_ANONYMOUS_VARIABLE(BENCHMARK_SUSPEND) = \
          ::folly::BenchmarkSuspender()) {            \
  } else
