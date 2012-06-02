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

#ifndef FOLLY_BENCHMARK_H_
#define FOLLY_BENCHMARK_H_

#include "folly/Preprocessor.h" // for FB_ANONYMOUS_VARIABLE
#include <cassert>
#include <ctime>
#include <boost/function_types/function_arity.hpp>
#include <functional>
#include <glog/logging.h>
#include <gflags/gflags.h>
#include <limits>

DECLARE_bool(benchmark);

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

namespace detail {

/**
 * This is the clock ID used for measuring time. On older kernels, the
 * resolution of this clock will be very coarse, which will cause the
 * benchmarks to fail.
 */
enum Clock { DEFAULT_CLOCK_ID = CLOCK_REALTIME };

/**
 * Adds a benchmark wrapped in a std::function. Only used
 * internally. Pass by value is intentional.
 */
void addBenchmarkImpl(const char* file,
                      const char* name,
                      std::function<uint64_t(unsigned int)>);

/**
 * Takes the difference between two timespec values. end is assumed to
 * occur after start.
 */
inline uint64_t timespecDiff(timespec end, timespec start) {
  if (end.tv_sec == start.tv_sec) {
    assert(end.tv_nsec >= start.tv_nsec);
    return end.tv_nsec - start.tv_nsec;
  }
  assert(end.tv_sec > start.tv_sec &&
         end.tv_sec - start.tv_sec <
         std::numeric_limits<uint64_t>::max() / 1000000000UL);
  return (end.tv_sec - start.tv_sec) * 1000000000UL
    + end.tv_nsec - start.tv_nsec;
}

/**
 * Takes the difference between two sets of timespec values. The first
 * two come from a high-resolution clock whereas the other two come
 * from a low-resolution clock. The crux of the matter is that
 * high-res values may be bogus as documented in
 * http://linux.die.net/man/3/clock_gettime. The trouble is when the
 * running process migrates from one CPU to another, which is more
 * likely for long-running processes. Therefore we watch for high
 * differences between the two timings.
 *
 * This function is subject to further improvements.
 */
inline uint64_t timespecDiff(timespec end, timespec start,
                             timespec endCoarse, timespec startCoarse) {
  auto fine = timespecDiff(end, start);
  auto coarse = timespecDiff(endCoarse, startCoarse);
  if (coarse - fine >= 1000000) {
    // The fine time is in all likelihood bogus
    return coarse;
  }
  return fine;
}

} // namespace detail

/**
 * Supporting type for BENCHMARK_SUSPEND defined below.
 */
struct BenchmarkSuspender {
  BenchmarkSuspender() {
    CHECK_EQ(0, clock_gettime(detail::DEFAULT_CLOCK_ID, &start));
  }

  BenchmarkSuspender(const BenchmarkSuspender &) = delete;
  BenchmarkSuspender(BenchmarkSuspender && rhs) {
    start = rhs.start;
    rhs.start.tv_nsec = rhs.start.tv_sec = 0;
  }

  BenchmarkSuspender& operator=(const BenchmarkSuspender &) = delete;
  BenchmarkSuspender& operator=(BenchmarkSuspender && rhs) {
    if (start.tv_nsec > 0 || start.tv_sec > 0) {
      tally();
    }
    start = rhs.start;
    rhs.start.tv_nsec = rhs.start.tv_sec = 0;
    return *this;
  }

  ~BenchmarkSuspender() {
    if (start.tv_nsec > 0 || start.tv_sec > 0) {
      tally();
    }
  }

  void dismiss() {
    assert(start.tv_nsec > 0 || start.tv_sec > 0);
    tally();
    start.tv_nsec = start.tv_sec = 0;
  }

  void rehire() {
    assert(start.tv_nsec == 0 || start.tv_sec == 0);
    CHECK_EQ(0, clock_gettime(detail::DEFAULT_CLOCK_ID, &start));
  }

  /**
   * This helps the macro definition. To get around the dangers of
   * operator bool, returns a pointer to member (which allows no
   * arithmetic).
   */
  operator int BenchmarkSuspender::*() const {
    return nullptr;
  }

  /**
   * Accumulates nanoseconds spent outside benchmark.
   */
  typedef uint64_t NanosecondsSpent;
  static NanosecondsSpent nsSpent;

private:
  void tally() {
    timespec end;
    CHECK_EQ(0, clock_gettime(detail::DEFAULT_CLOCK_ID, &end));
    nsSpent += detail::timespecDiff(end, start);
    start = end;
  }

  timespec start;
};

/**
 * Adds a benchmark. Usually not called directly but instead through
 * the macro BENCHMARK defined below. The lambda function involved
 * must take exactly one parameter of type unsigned, and the benchmark
 * uses it with counter semantics (iteration occurs inside the
 * function).
 */
template <typename Lambda>
typename std::enable_if<
  boost::function_types::function_arity<decltype(&Lambda::operator())>::value
  == 2
>::type
addBenchmark(const char* file, const char* name, Lambda&& lambda) {
  auto execute = [=](unsigned int times) {
    BenchmarkSuspender::nsSpent = 0;
    timespec start, end;

    // CORE MEASUREMENT STARTS
    CHECK_EQ(0, clock_gettime(detail::DEFAULT_CLOCK_ID, &start));
    lambda(times);
    CHECK_EQ(0, clock_gettime(detail::DEFAULT_CLOCK_ID, &end));
    // CORE MEASUREMENT ENDS

    return detail::timespecDiff(end, start) - BenchmarkSuspender::nsSpent;
  };

  detail::addBenchmarkImpl(file, name,
                           std::function<uint64_t(unsigned int)>(execute));
}

/**
 * Adds a benchmark. Usually not called directly but instead through
 * the macro BENCHMARK defined below. The lambda function involved
 * must take zero parameters, and the benchmark calls it repeatedly
 * (iteration occurs outside the function).
 */
template <typename Lambda>
typename std::enable_if<
  boost::function_types::function_arity<decltype(&Lambda::operator())>::value
  == 1
>::type
addBenchmark(const char* file, const char* name, Lambda&& lambda) {
  addBenchmark(file, name, [=](unsigned int times) {
      while (times-- > 0) {
        lambda();
      }
    });
}

/**
 * Call doNotOptimizeAway(var) against variables that you use for
 * benchmarking but otherwise are useless. The compiler tends to do a
 * good job at eliminating unused variables, and this function fools
 * it into thinking var is in fact needed.
 */
template <class T>
void doNotOptimizeAway(T&& datum) {
  asm volatile("" : "+r" (datum));
}

} // namespace folly

/**
 * Introduces a benchmark function. Used internally, see BENCHMARK and
 * friends below.
 */
#define BENCHMARK_IMPL(funName, stringName, paramType, paramName)       \
  static void funName(paramType);                                       \
  static bool FB_ANONYMOUS_VARIABLE(follyBenchmarkUnused) = (           \
    ::folly::addBenchmark(__FILE__, stringName,                         \
      [](paramType paramName) { funName(paramName); }),                 \
    true);                                                              \
  static void funName(paramType paramName)

/**
 * Introduces a benchmark function. Use with either one one or two
 * arguments. The first is the name of the benchmark. Use something
 * descriptive, such as insertVectorBegin. The second argument may be
 * missing, or could be a symbolic counter. The counter dictates how
 * many internal iteration the benchmark does. Example:
 *
 * BENCHMARK(vectorPushBack) {
 *   vector<int> v;
 *   v.push_back(42);
 * }
 *
 * BENCHMARK(insertVectorBegin, n) {
 *   vector<int> v;
 *   FOR_EACH_RANGE (i, 0, n) {
 *     v.insert(v.begin(), 42);
 *   }
 * }
 */
#define BENCHMARK(name, ...)                                    \
  BENCHMARK_IMPL(                                               \
    name,                                                       \
    FB_STRINGIZE(name),                                         \
    FB_ONE_OR_NONE(unsigned, ## __VA_ARGS__),                   \
    __VA_ARGS__)

/**
 * Defines a benchmark that passes a parameter to another one. This is
 * common for benchmarks that need a "problem size" in addition to
 * "number of iterations". Consider:
 *
 * void pushBack(uint n, size_t initialSize) {
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
#define BENCHMARK_PARAM(name, param)                                    \
  BENCHMARK_IMPL(                                                       \
      FB_CONCATENATE(name, FB_CONCATENATE(_, param)),                   \
      FB_STRINGIZE(name) "(" FB_STRINGIZE(param) ")",                   \
      unsigned,                                                         \
      iters) {                                                          \
    name(iters, param);                                                 \
  }

/**
 * Just like BENCHMARK, but prints the time relative to a
 * baseline. The baseline is the most recent BENCHMARK() seen in
 * lexical order. Example:
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
#define BENCHMARK_RELATIVE(name, ...)                           \
  BENCHMARK_IMPL(                                               \
    name,                                                       \
    "%" FB_STRINGIZE(name),                                     \
    FB_ONE_OR_NONE(unsigned, ## __VA_ARGS__),                   \
    __VA_ARGS__)

/**
 * A combination of BENCHMARK_RELATIVE and BENCHMARK_PARAM.
 */
#define BENCHMARK_RELATIVE_PARAM(name, param)                           \
  BENCHMARK_IMPL(                                                       \
      FB_CONCATENATE(name, FB_CONCATENATE(_, param)),                   \
      "%" FB_STRINGIZE(name) "(" FB_STRINGIZE(param) ")",               \
      unsigned,                                                         \
      iters) {                                                          \
    name(iters, param);                                                 \
  }

/**
 * Draws a line of dashes.
 */
#define BENCHMARK_DRAW_LINE()                                   \
  static bool FB_ANONYMOUS_VARIABLE(follyBenchmarkUnused) = (   \
    ::folly::addBenchmark(__FILE__, "-", []() { }),             \
    true);

/**
 * Allows execution of code that doesn't count torward the benchmark's
 * time budget. Example:
 *
 * BENCHMARK_START_GROUP(insertVectorBegin, n) {
 *   vector<int> v;
 *   SUSPEND_BENCHMARK {
 *     v.reserve(n);
 *   }
 *   FOR_EACH_RANGE (i, 0, n) {
 *     v.insert(v.begin(), 42);
 *   }
 * }
 */
#define BENCHMARK_SUSPEND                               \
  if (auto FB_ANONYMOUS_VARIABLE(BENCHMARK_SUSPEND) =   \
      ::folly::BenchmarkSuspender()) {}                 \
  else

#endif // FOLLY_BENCHMARK_H_
