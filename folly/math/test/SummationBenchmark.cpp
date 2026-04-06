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

#include <folly/math/AccurateSummation.h>
#include <folly/math/KahanSummation.h>
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

// ============================================================================
// Naive baseline
// ============================================================================

BENCHMARK(SumDouble_Naive_1M, iters) {
  while (iters--) {
    double sum = 0;
    for (auto const& x : kDoubleData) {
      sum += x;
    }
    folly::doNotOptimizeAway(sum);
  }
}

BENCHMARK(SumFloat_Naive_1M, iters) {
  while (iters--) {
    float sum = 0;
    for (auto const& x : kFloatData) {
      sum += x;
    }
    folly::doNotOptimizeAway(sum);
  }
}

BENCHMARK_DRAW_LINE();

// ============================================================================
// Kahan (Kahan-Babushka-Neumaier) compensated summation
// ============================================================================

BENCHMARK(SumDouble_KahanOneAtATime_10, iters) {
  while (iters--) {
    folly::kahan_accumulator<double> acc;
    for (size_t j = 0; j < 10; ++j) {
      acc += kDoubleData[j];
    }
    folly::doNotOptimizeAway(acc.value());
  }
}

BENCHMARK_RELATIVE(SumDouble_KahanSum_10, iters) {
  while (iters--) {
    folly::doNotOptimizeAway(
        folly::kahan_sum(kDoubleData.begin(), kDoubleData.begin() + 10));
  }
}

BENCHMARK_DRAW_LINE();

BENCHMARK(SumDouble_KahanOneAtATime_1000, iters) {
  while (iters--) {
    folly::kahan_accumulator<double> acc;
    for (size_t j = 0; j < 1000; ++j) {
      acc += kDoubleData[j];
    }
    folly::doNotOptimizeAway(acc.value());
  }
}

BENCHMARK_RELATIVE(SumDouble_KahanSum_1000, iters) {
  while (iters--) {
    folly::doNotOptimizeAway(
        folly::kahan_sum(kDoubleData.begin(), kDoubleData.begin() + 1000));
  }
}

BENCHMARK_DRAW_LINE();

BENCHMARK(SumDouble_KahanOneAtATime_1M, iters) {
  while (iters--) {
    folly::kahan_accumulator<double> acc;
    for (auto const& x : kDoubleData) {
      acc += x;
    }
    folly::doNotOptimizeAway(acc.value());
  }
}

BENCHMARK_RELATIVE(SumDouble_KahanSum_1M, iters) {
  while (iters--) {
    folly::doNotOptimizeAway(folly::kahan_sum(kDoubleData));
  }
}

BENCHMARK_DRAW_LINE();

BENCHMARK(SumFloat_KahanOneAtATime_1M, iters) {
  while (iters--) {
    folly::kahan_accumulator<float> acc;
    for (auto const& x : kFloatData) {
      acc += x;
    }
    folly::doNotOptimizeAway(acc.value());
  }
}

BENCHMARK_RELATIVE(SumFloat_KahanSum_1M, iters) {
  while (iters--) {
    folly::doNotOptimizeAway(folly::kahan_sum(kFloatData));
  }
}

BENCHMARK_DRAW_LINE();

// ============================================================================
// Accurate summation (wider-type path for small n, else Kahan)
// ============================================================================

BENCHMARK(SumDouble_AccurateSum_1M, iters) {
  while (iters--) {
    folly::doNotOptimizeAway(folly::accurate_sum(kDoubleData));
  }
}

BENCHMARK(SumFloat_AccurateSum_1M, iters) {
  while (iters--) {
    folly::doNotOptimizeAway(folly::accurate_sum(kFloatData));
  }
}

BENCHMARK_DRAW_LINE();

// ============================================================================
// Long double accumulation
// ============================================================================

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

BENCHMARK_DRAW_LINE();

// ============================================================================
// Reproducible accumulator
// ============================================================================

BENCHMARK(SumDouble_ReproducibleOneAtATime_10, iters) {
  while (iters--) {
    reproducible_accumulator<double> rfa;
    for (size_t j = 0; j < 10; ++j) {
      rfa += kDoubleData[j];
    }
    folly::doNotOptimizeAway(rfa.value());
  }
}

BENCHMARK_RELATIVE(SumDouble_ReproducibleBatchAdd_10, iters) {
  while (iters--) {
    reproducible_accumulator<double> rfa;
    rfa.add(kDoubleData.begin(), kDoubleData.begin() + 10);
    folly::doNotOptimizeAway(rfa.value());
  }
}

BENCHMARK_DRAW_LINE();

BENCHMARK(SumDouble_ReproducibleOneAtATime_1000, iters) {
  while (iters--) {
    reproducible_accumulator<double> rfa;
    for (size_t j = 0; j < 1000; ++j) {
      rfa += kDoubleData[j];
    }
    folly::doNotOptimizeAway(rfa.value());
  }
}

BENCHMARK_RELATIVE(SumDouble_ReproducibleBatchAdd_1000, iters) {
  while (iters--) {
    reproducible_accumulator<double> rfa;
    rfa.add(kDoubleData.begin(), kDoubleData.begin() + 1000);
    folly::doNotOptimizeAway(rfa.value());
  }
}

BENCHMARK_DRAW_LINE();

BENCHMARK(SumDouble_ReproducibleOneAtATime_1M, iters) {
  while (iters--) {
    reproducible_accumulator<double> rfa;
    for (auto const& x : kDoubleData) {
      rfa += x;
    }
    folly::doNotOptimizeAway(rfa.value());
  }
}

BENCHMARK_RELATIVE(SumDouble_ReproducibleBatchAdd_1M, iters) {
  while (iters--) {
    reproducible_accumulator<double> rfa;
    rfa.add(kDoubleData.begin(), kDoubleData.end());
    folly::doNotOptimizeAway(rfa.value());
  }
}

BENCHMARK_DRAW_LINE();

BENCHMARK(SumFloat_ReproducibleOneAtATime_1M, iters) {
  while (iters--) {
    reproducible_accumulator<float> rfa;
    for (auto const& x : kFloatData) {
      rfa += x;
    }
    folly::doNotOptimizeAway(rfa.value());
  }
}

BENCHMARK_RELATIVE(SumFloat_ReproducibleBatchAdd_1M, iters) {
  while (iters--) {
    reproducible_accumulator<float> rfa;
    rfa.add(kFloatData.begin(), kFloatData.end());
    folly::doNotOptimizeAway(rfa.value());
  }
}

int main(int argc, char** argv) {
  folly::Init init(&argc, &argv);
  folly::runBenchmarks();
  return 0;
}
