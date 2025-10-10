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

#include <stdexcept>

#include <vector>

#include <folly/Benchmark.h>
#include <folly/ExceptionWrapper.h>
#include <folly/debugging/exception_tracer/SmartExceptionTracer.h>

#if FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF

#if FOLLY_HAVE_SMART_EXCEPTION_TRACER

using namespace folly::exception_tracer;

namespace {

[[noreturn]] FOLLY_NOINLINE void throwRuntimeError() {
  throw std::runtime_error("benchmark exception");
}

FOLLY_NOINLINE void recurse(int n) {
  if (n == 0) {
    throwRuntimeError();
  }
  recurse(n - 1);
}

folly::exception_tracer::ExceptionInfo makeExceptionWithStackDepthMeasureThrow(
    int n) {
  try {
    recurse(n);
  } catch (...) {
    folly::BenchmarkSuspender s;
    return getTrace(std::current_exception());
  }
  return {};
}

folly::exception_tracer::ExceptionInfo
makeExceptionPtrWithStackDepthMeasureRetrieve(int n) {
  try {
    folly::BenchmarkSuspender s;
    recurse(n);
  } catch (...) {
    return getTrace(std::current_exception());
  }
  return {};
}

folly::exception_tracer::ExceptionInfo
makeExceptionRefWithStackDepthMeasureRetrieve(int n) {
  try {
    folly::BenchmarkSuspender s;
    recurse(n);
  } catch (const std::exception& e) {
    return getTrace(e);
  }
  return {};
}

folly::exception_tracer::ExceptionInfo
makeExceptionWrapperWithStackDepthMeasureRetrieve(int n) {
  try {
    folly::BenchmarkSuspender s;
    recurse(n);
  } catch (...) {
    auto wrapper = folly::exception_wrapper(std::current_exception());
    return getTrace(wrapper);
  }
  return {};
}

} // namespace

void captureException(int iterations, int stackDepth) {
  for (int i = 0; i < iterations; i++) {
    auto info = makeExceptionWithStackDepthMeasureThrow(stackDepth);
    folly::BenchmarkSuspender s;
    folly::assume(info.frames.size() > 0);
  }
}

void retrieveStackTracePtr(int iterations, int stackDepth) {
  for (int i = 0; i < iterations; i++) {
    auto info = makeExceptionPtrWithStackDepthMeasureRetrieve(stackDepth);
    folly::BenchmarkSuspender s;
    folly::assume(info.frames.size() > 0);
  }
}

void retrieveStackTraceRef(int iterations, int stackDepth) {
  for (int i = 0; i < iterations; i++) {
    auto info = makeExceptionRefWithStackDepthMeasureRetrieve(stackDepth);
    folly::BenchmarkSuspender s;
    folly::assume(info.frames.size() > 0);
  }
}

void retrieveStackTraceWrapper(int iterations, int stackDepth) {
  for (int i = 0; i < iterations; i++) {
    auto info = makeExceptionWrapperWithStackDepthMeasureRetrieve(stackDepth);
    folly::BenchmarkSuspender s;
    folly::assume(info.frames.size() > 0);
  }
}

BENCHMARK_PARAM(captureException, 0)
BENCHMARK_PARAM(captureException, 20)
BENCHMARK_PARAM(captureException, 100)
BENCHMARK_PARAM(captureException, 1000)

BENCHMARK_DRAW_LINE();

BENCHMARK_PARAM(retrieveStackTracePtr, 0)
BENCHMARK_PARAM(retrieveStackTracePtr, 20)
BENCHMARK_PARAM(retrieveStackTracePtr, 100)
BENCHMARK_PARAM(retrieveStackTracePtr, 1000)

BENCHMARK_DRAW_LINE();

BENCHMARK_PARAM(retrieveStackTraceRef, 0)
BENCHMARK_PARAM(retrieveStackTraceRef, 20)
BENCHMARK_PARAM(retrieveStackTraceRef, 100)
BENCHMARK_PARAM(retrieveStackTraceRef, 1000)

BENCHMARK_DRAW_LINE();

BENCHMARK_PARAM(retrieveStackTraceWrapper, 0)
BENCHMARK_PARAM(retrieveStackTraceWrapper, 20)
BENCHMARK_PARAM(retrieveStackTraceWrapper, 100)
BENCHMARK_PARAM(retrieveStackTraceWrapper, 1000)

int main(int, char**) {
  folly::runBenchmarks();
}

#endif
#endif
