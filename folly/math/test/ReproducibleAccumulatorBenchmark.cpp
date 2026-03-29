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

#include <folly/math/ReproducibleAccumulator.h>

#include <random>
#include <vector>

#include <folly/Benchmark.h>
#include <folly/init/Init.h>

using folly::reproducible_accumulator;

namespace {

template <typename T>
std::vector<T> makeData(size_t n) {
  std::mt19937 gen(42);
  std::uniform_real_distribution<T> dist(T(-1000), T(1000));
  std::vector<T> v(n);
  for (auto& x : v) {
    x = dist(gen);
  }
  return v;
}

size_t const kN = 1'000'000;
auto const kDoubleData = makeData<double>(kN);
auto const kFloatData = makeData<float>(kN);

} // namespace

// Small input: below ILP threshold (serial path)
BENCHMARK(SumDouble_OneAtATime_10, iters) {
  while (iters--) {
    reproducible_accumulator<double> rfa;
    for (size_t j = 0; j < 10; ++j) {
      rfa += kDoubleData[j];
    }
    folly::doNotOptimizeAway(rfa.value());
  }
}

BENCHMARK_RELATIVE(SumDouble_BatchAdd_10, iters) {
  while (iters--) {
    reproducible_accumulator<double> rfa;
    rfa.add(kDoubleData.begin(), kDoubleData.begin() + 10);
    folly::doNotOptimizeAway(rfa.value());
  }
}

BENCHMARK_DRAW_LINE();

// Medium input: above ILP threshold
BENCHMARK(SumDouble_OneAtATime_1000, iters) {
  while (iters--) {
    reproducible_accumulator<double> rfa;
    for (size_t j = 0; j < 1000; ++j) {
      rfa += kDoubleData[j];
    }
    folly::doNotOptimizeAway(rfa.value());
  }
}

BENCHMARK_RELATIVE(SumDouble_BatchAdd_1000, iters) {
  while (iters--) {
    reproducible_accumulator<double> rfa;
    rfa.add(kDoubleData.begin(), kDoubleData.begin() + 1000);
    folly::doNotOptimizeAway(rfa.value());
  }
}

BENCHMARK_DRAW_LINE();

// Large input: 1M elements
BENCHMARK(SumDouble_OneAtATime_1M, iters) {
  while (iters--) {
    reproducible_accumulator<double> rfa;
    for (auto const& x : kDoubleData) {
      rfa += x;
    }
    folly::doNotOptimizeAway(rfa.value());
  }
}

BENCHMARK_RELATIVE(SumDouble_BatchAdd_1M, iters) {
  while (iters--) {
    reproducible_accumulator<double> rfa;
    rfa.add(kDoubleData.begin(), kDoubleData.end());
    folly::doNotOptimizeAway(rfa.value());
  }
}

BENCHMARK_DRAW_LINE();

BENCHMARK(SumFloat_OneAtATime_1M, iters) {
  while (iters--) {
    reproducible_accumulator<float> rfa;
    for (auto const& x : kFloatData) {
      rfa += x;
    }
    folly::doNotOptimizeAway(rfa.value());
  }
}

BENCHMARK_RELATIVE(SumFloat_BatchAdd_1M, iters) {
  while (iters--) {
    reproducible_accumulator<float> rfa;
    rfa.add(kFloatData.begin(), kFloatData.end());
    folly::doNotOptimizeAway(rfa.value());
  }
}

BENCHMARK_DRAW_LINE();

BENCHMARK(SumDouble_NaiveBaseline_1M, iters) {
  while (iters--) {
    double sum = 0;
    for (auto const& x : kDoubleData) {
      sum += x;
    }
    folly::doNotOptimizeAway(sum);
  }
}

BENCHMARK(SumFloat_NaiveBaseline_1M, iters) {
  while (iters--) {
    float sum = 0;
    for (auto const& x : kFloatData) {
      sum += x;
    }
    folly::doNotOptimizeAway(sum);
  }
}

BENCHMARK_DRAW_LINE();

BENCHMARK(SumDouble_Kahan_1M, iters) {
  while (iters--) {
    double sum = 0;
    double c = 0;
    for (auto const& x : kDoubleData) {
      double y = x - c;
      double t = sum + y;
      folly::makeUnpredictable(t);
      c = (t - sum) - y;
      sum = t;
    }
    folly::doNotOptimizeAway(sum);
  }
}

BENCHMARK(SumFloat_Kahan_1M, iters) {
  while (iters--) {
    float sum = 0;
    float c = 0;
    for (auto const& x : kFloatData) {
      float y = x - c;
      float t = sum + y;
      folly::makeUnpredictable(t);
      c = (t - sum) - y;
      sum = t;
    }
    folly::doNotOptimizeAway(sum);
  }
}

BENCHMARK_DRAW_LINE();

BENCHMARK(SumDouble_LongDouble_1M, iters) {
  while (iters--) {
    long double sum = 0;
    for (auto const& x : kDoubleData) {
      sum += static_cast<long double>(x);
    }
    folly::doNotOptimizeAway(static_cast<double>(sum));
  }
}

BENCHMARK(SumFloat_LongDouble_1M, iters) {
  while (iters--) {
    long double sum = 0;
    for (auto const& x : kFloatData) {
      sum += static_cast<long double>(x);
    }
    folly::doNotOptimizeAway(static_cast<float>(sum));
  }
}

int main(int argc, char** argv) {
  folly::Init init(&argc, &argv);
  folly::runBenchmarks();
  return 0;
}
