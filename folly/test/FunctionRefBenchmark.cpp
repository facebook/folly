/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <folly/Benchmark.h>
#include <folly/Function.h>
#include <folly/Random.h>
#include <folly/synchronization/detail/InlineFunctionRef.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>

/**
 * ============================================================================
 * folly/test/FunctionRefBenchmark.cpp             relative  time/iter  iters/s
 * ============================================================================
 * SmallFunctionFunctionPointerInvoke                           3.02ns  331.34M
 * SmallFunctionStdFunctionInvoke                               3.02ns  331.34M
 * SmallFunctionStdFunctionWithReferenceWrapperInv              3.02ns  331.33M
 * SmallFunctionFollyFunctionInvoke                             3.02ns  331.30M
 * SmallFunctionFollyFunctionRefInvoke                          3.02ns  331.34M
 * ----------------------------------------------------------------------------
 * SmallFunctionFunctionPointerCreateInvoke                     3.02ns  331.34M
 * SmallFunctionStdFunctionCreateInvoke                         3.76ns  266.24M
 * SmallFunctionStdFunctionReferenceWrapperCreateI              4.77ns  209.52M
 * SmallFunctionFollyFunctionCreateInvoke                       3.24ns  308.40M
 * SmallFunctionFollyFunctionRefCreateInvoke                    3.02ns  331.34M
 * ----------------------------------------------------------------------------
 * BigFunctionStdFunctionInvoke                                 3.02ns  330.74M
 * BigFunctionStdFunctionReferenceWrapperInvoke                 3.02ns  330.74M
 * BigFunctionFollyFunctionInvoke                               3.02ns  331.31M
 * BigFunctionFollyFunctionRefInvoke                            3.02ns  331.31M
 * ----------------------------------------------------------------------------
 * BigFunctionStdFunctionCreateInvoke                          43.87ns   22.79M
 * BigFunctionStdFunctionReferenceWrapperCreateInv              4.20ns  238.22M
 * BigFunctionFollyFunctionCreateInvoke                        43.01ns   23.25M
 * BigFunctionFollyFunctionRefCreateInvoke                      3.02ns  331.31M
 * ============================================================================
 */

namespace folly {
namespace {
template <typename MakeFunction>
void runSmallInvokeBenchmark(std::size_t iters, MakeFunction make) {
  auto lambda = [](auto& i) {
    folly::makeUnpredictable(i);
    return i;
  };
  folly::makeUnpredictable(lambda);
  auto func = make(lambda);
  folly::makeUnpredictable(func);

  for (auto i = iters; --i;) {
    folly::doNotOptimizeAway(func(i));
  }
}

template <typename MakeFunction>
void runSmallCreateAndInvokeBenchmark(std::size_t iters, MakeFunction make) {
  auto lambda = [](auto& i) {
    folly::makeUnpredictable(i);
    return i;
  };
  folly::makeUnpredictable(lambda);

  for (auto i = iters; --i;) {
    auto func = make(lambda);
    folly::makeUnpredictable(func);
    folly::doNotOptimizeAway(func(i));
  }
}

template <typename MakeFunction>
void runBigAndInvokeBenchmark(std::size_t iters, MakeFunction make) {
  auto suspender = BenchmarkSuspender{};
  auto array = std::array<std::uint8_t, 4096>{};

  auto lambda = [=](auto& i) {
    // we use std::ignore to ensure ODR-usage of array
    std::ignore = array;
    folly::makeUnpredictable(i);
    return i;
  };
  folly::makeUnpredictable(lambda);
  auto func = make(lambda);
  folly::makeUnpredictable(func);

  suspender.dismissing([&] {
    for (auto i = iters; --i;) {
      folly::doNotOptimizeAway(func(i));
    }
  });
}

template <typename MakeFunction>
void runBigCreateAndInvokeBenchmark(std::size_t iters, MakeFunction make) {
  auto suspender = BenchmarkSuspender{};
  auto array = std::array<std::uint8_t, 1024>{};
  folly::makeUnpredictable(array);

  auto lambda = [=](auto& i) {
    folly::doNotOptimizeAway(array);
    folly::makeUnpredictable(i);
    return i;
  };
  folly::makeUnpredictable(lambda);

  suspender.dismissing([&] {
    for (auto i = iters; --i;) {
      auto func = make(lambda);
      folly::makeUnpredictable(func);
      folly::doNotOptimizeAway(func(i));
    }
  });
}
} // namespace

BENCHMARK(SmallFunctionFunctionPointerInvoke, iters) {
  using FPtr = size_t (*)(size_t&);
  runSmallInvokeBenchmark(iters, [](auto& f) { return FPtr{f}; });
}
BENCHMARK(SmallFunctionStdFunctionInvoke, iters) {
  runSmallInvokeBenchmark(
      iters, [](auto& f) { return std::function<size_t(size_t&)>{f}; });
}
BENCHMARK(SmallFunctionStdFunctionWithReferenceWrapperInvoke, iters) {
  runSmallInvokeBenchmark(iters, [](auto& f) {
    return std::function<size_t(size_t&)>{std::ref(f)};
  });
}
BENCHMARK(SmallFunctionFollyFunctionInvoke, iters) {
  runSmallInvokeBenchmark(
      iters, [](auto& f) { return folly::Function<size_t(size_t&)>{f}; });
}
BENCHMARK(SmallFunctionFollyFunctionRefInvoke, iters) {
  runSmallInvokeBenchmark(
      iters, [](auto& f) { return folly::FunctionRef<size_t(size_t&)>{f}; });
}
BENCHMARK(SmallFunctionFollyInlineFunctionRefInvoke, iters) {
  runSmallInvokeBenchmark(iters, [](auto f) {
    return detail::InlineFunctionRef<size_t(size_t&), 24>{std::move(f)};
  });
}

BENCHMARK_DRAW_LINE();
BENCHMARK(SmallFunctionFunctionPointerCreateInvoke, iters) {
  using FPtr = size_t (*)(size_t&);
  runSmallCreateAndInvokeBenchmark(iters, [](auto& f) { return FPtr{f}; });
}
BENCHMARK(SmallFunctionStdFunctionCreateInvoke, iters) {
  runSmallCreateAndInvokeBenchmark(
      iters, [](auto& f) { return std::function<size_t(size_t&)>{f}; });
}
BENCHMARK(SmallFunctionStdFunctionReferenceWrapperCreateInvoke, iters) {
  runSmallCreateAndInvokeBenchmark(iters, [](auto& f) {
    return std::function<size_t(size_t&)>{std::ref(f)};
  });
}
BENCHMARK(SmallFunctionFollyFunctionCreateInvoke, iters) {
  runSmallCreateAndInvokeBenchmark(
      iters, [](auto& f) { return folly::Function<size_t(size_t&)>{f}; });
}
BENCHMARK(SmallFunctionFollyFunctionRefCreateInvoke, iters) {
  runSmallCreateAndInvokeBenchmark(
      iters, [](auto& f) { return folly::FunctionRef<size_t(size_t&)>{f}; });
}
BENCHMARK(SmallFunctionFollyInlineFunctionRefCreateInvoke, iters) {
  runSmallInvokeBenchmark(iters, [](auto f) {
    return detail::InlineFunctionRef<size_t(size_t&), 24>{std::move(f)};
  });
}

BENCHMARK_DRAW_LINE();
BENCHMARK(BigFunctionStdFunctionInvoke, iters) {
  runBigAndInvokeBenchmark(
      iters, [](auto& f) { return std::function<size_t(size_t&)>{f}; });
}
BENCHMARK(BigFunctionStdFunctionReferenceWrapperInvoke, iters) {
  runBigAndInvokeBenchmark(iters, [](auto& f) {
    return std::function<size_t(size_t&)>{std::ref(f)};
  });
}
BENCHMARK(BigFunctionFollyFunctionInvoke, iters) {
  runBigAndInvokeBenchmark(
      iters, [](auto& f) { return folly::Function<size_t(size_t&)>{f}; });
}
BENCHMARK(BigFunctionFollyFunctionRefInvoke, iters) {
  runBigAndInvokeBenchmark(
      iters, [](auto& f) { return folly::FunctionRef<size_t(size_t&)>{f}; });
}
BENCHMARK(BigFunctionFollyInlineFunctionRefInvoke, iters) {
  runSmallInvokeBenchmark(iters, [](auto f) {
    return detail::InlineFunctionRef<size_t(size_t&), 24>{std::move(f)};
  });
}

BENCHMARK_DRAW_LINE();
BENCHMARK(BigFunctionStdFunctionCreateInvoke, iters) {
  runBigCreateAndInvokeBenchmark(
      iters, [](auto& f) { return std::function<size_t(size_t&)>{f}; });
}
BENCHMARK(BigFunctionStdFunctionReferenceWrapperCreateInvoke, iters) {
  runBigCreateAndInvokeBenchmark(iters, [](auto& f) {
    return std::function<size_t(size_t&)>{std::ref(f)};
  });
}
BENCHMARK(BigFunctionFollyFunctionCreateInvoke, iters) {
  runBigCreateAndInvokeBenchmark(
      iters, [](auto& f) { return folly::Function<size_t(size_t&)>{f}; });
}
BENCHMARK(BigFunctionFollyFunctionRefCreateInvoke, iters) {
  runBigCreateAndInvokeBenchmark(
      iters, [](auto& f) { return folly::FunctionRef<size_t(size_t&)>{f}; });
}
BENCHMARK(BigFunctionFollyInlineFunctionRefCreateInvoke, iters) {
  runSmallInvokeBenchmark(iters, [](auto f) {
    return detail::InlineFunctionRef<size_t(size_t&), 24>{std::move(f)};
  });
}
} // namespace folly

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
}
