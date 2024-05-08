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

#include <folly/algorithm/simd/FindFixed.h>

#include <folly/Benchmark.h>
#include <folly/init/Init.h>

#include <fmt/core.h>

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <span>
#include <vector>

namespace folly {
namespace {

template <typename T, std::size_t N>
const auto kInput = [] {
  std::vector<T> res(N, T{0});
  std::iota(res.begin(), res.end(), T{0});
  return res;
}();

template <std::size_t N>
using IndexConstant = std::integral_constant<std::size_t, N>;

template <typename T, std::size_t N>
void stdFindBenchmark(unsigned n) {
  const auto& in = kInput<T, N>;

  while (n--) {
    for (auto x : in) {
      folly::doNotOptimizeAway(std::ranges::find(in, x));
    }
  }
}

template <typename T, std::size_t N>
void follyFindFixedBenchmark(unsigned n) {
  const auto& in = kInput<T, N>;

  while (n--) {
    for (auto x : in) {
      std::span<const T, N> s(in.data(), N);
      folly::doNotOptimizeAway(folly::findFixed(s, x));
    }
  }
}

template <typename T, std::size_t N>
void registerBenchmark(T, IndexConstant<N>) {
  (void)kInput<T, N>;

  folly::addBenchmark(
      __FILE__,
      fmt::format(
          "total size:{}, sizeof(T):{} std::find", N * sizeof(T), sizeof(T)),
      [](unsigned n) -> unsigned {
        stdFindBenchmark<T, N>(n);
        return n;
      });
  folly::addBenchmark(
      __FILE__,
      fmt::format(
          "total size:{}, sizeof(T):{} folly::findFixed",
          N * sizeof(T),
          sizeof(T)),
      [](unsigned n) -> unsigned {
        follyFindFixedBenchmark<T, N>(n);
        return n;
      });
}

void drawLine() {
  folly::addBenchmark(__FILE__, "-", []() -> unsigned { return 0; });
}

void registerAllBenchmarks() {
  // 8 bytes
  registerBenchmark(std::int8_t{}, IndexConstant<8>{});
  registerBenchmark(std::int16_t{}, IndexConstant<4>{});
  registerBenchmark(std::int32_t{}, IndexConstant<2>{});
  drawLine();
  // 16 bytes
  registerBenchmark(std::int8_t{}, IndexConstant<16>{});
  registerBenchmark(std::int16_t{}, IndexConstant<8>{});
  registerBenchmark(std::int32_t{}, IndexConstant<4>{});
  registerBenchmark(std::int64_t{}, IndexConstant<2>{});
  drawLine();
  // 32 bytes
  registerBenchmark(std::int8_t{}, IndexConstant<32>{});
  registerBenchmark(std::int16_t{}, IndexConstant<16>{});
  registerBenchmark(std::int32_t{}, IndexConstant<8>{});
  registerBenchmark(std::int64_t{}, IndexConstant<4>{});
  drawLine();
  // 40 bytes
  registerBenchmark(std::int8_t{}, IndexConstant<40>{});
  registerBenchmark(std::int16_t{}, IndexConstant<20>{});
  registerBenchmark(std::int32_t{}, IndexConstant<10>{});
  registerBenchmark(std::int64_t{}, IndexConstant<5>{});
}

} // namespace
} // namespace folly

int main(int argc, char** argv) {
  folly::Init init(&argc, &argv, true);
  folly::registerAllBenchmarks();
  folly::runBenchmarks();
}
