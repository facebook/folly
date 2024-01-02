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

#include <folly/container/tape.h>

#include <random>
#include <string>
#include <vector>
#include <folly/Benchmark.h>
#include <folly/init/Init.h>

namespace {

using vec_str = std::vector<std::string>;
using st_tape = folly::string_tape;
using vv_int = std::vector<std::vector<int>>;
using tv_int = folly::tape<std::vector<int>>;

template <typename Cont>
struct ContGenerator {
  ContGenerator(std::size_t minLen, std::size_t maxLen)
      : g(minLen + maxLen), len(minLen, maxLen) {}

  const Cont& operator()() {
    buf.resize(len(g));
    return buf; // we don't care for contents
  }

  std::mt19937 g;
  std::uniform_int_distribution<std::size_t> len;

  Cont buf;
};

template <typename Cont, std::size_t n, std::size_t minLen, std::size_t maxLen>
const std::vector<Cont>& generateContainers() {
  static const std::vector<Cont> res = [] {
    ContGenerator<Cont> gen{minLen, maxLen};
    std::vector<Cont> r;
    r.reserve(n);
    for (std::size_t i = 0; i != n; ++i) {
      r.push_back(gen());
    }
    return r;
  }();

  return res;
}

constexpr std::size_t kContainerCopies = 100'000;

template <
    typename ContainerType,
    typename RecordType,
    std::size_t n,
    std::size_t minLen,
    std::size_t maxLen>
const std::vector<ContainerType>& generateContainerCopies() {
  static const std::vector<ContainerType> res = [] {
    const auto& input = generateContainers<RecordType, n, minLen, maxLen>();
    const ContainerType sample(input.begin(), input.end());

    std::vector<ContainerType> copies(kContainerCopies, sample);
    std::shuffle(copies.begin(), copies.end(), std::mt19937{minLen + maxLen});

    return copies;
  }();
  return res;
}

template <
    typename StringContainer,
    std::size_t n,
    std::size_t minLen,
    std::size_t maxLen>
void constructorStrings(std::size_t iters) {
  const auto& strings = generateContainers<std::string, n, minLen, maxLen>();

  while (iters--) {
    StringContainer cont{strings.begin(), strings.end()};
    folly::doNotOptimizeAway(cont);
  }
}

template <typename T>
void pushBackByOne(
    std::vector<std::vector<T>>& cont, const std::vector<std::vector<T>>& in) {
  for (const auto& v : in) {
    cont.emplace_back();
    for (const auto& x : v) {
      cont.back().push_back(x);
    }
  }
}

template <typename T>
void pushBackByOne(
    folly::tape<std::vector<T>>& cont, const std::vector<std::vector<T>>& in) {
  for (const auto& v : in) {
    auto builder = cont.record_builder();
    for (const auto& x : v) {
      builder.push_back(x);
    }
  }
}

template <
    typename Container,
    std::size_t n,
    std::size_t minLen,
    std::size_t maxLen>
void pushBackInts(std::size_t iters) {
  const auto& vecs = generateContainers<std::vector<int>, n, minLen, maxLen>();

  while (iters--) {
    Container r;
    pushBackByOne(r, vecs);
    folly::doNotOptimizeAway(r);
  }
}

template <typename Container>
void iterateBenchImpl(const std::vector<Container>& copies, std::size_t iters) {
  std::size_t i = 0;

  while (iters--) {
    for (const auto& v : copies[i]) {
      for (const auto& x : v) {
        auto xCopy = x;
        folly::doNotOptimizeAway(x);
        folly::doNotOptimizeAway(xCopy);
      }
    }
    ++i;
    i %= copies.size();
  }
}

template <
    typename Container,
    std::size_t n,
    std::size_t minLen,
    std::size_t maxLen>
void iterateInts(std::size_t iters) {
  const auto& copies =
      generateContainerCopies<Container, std::vector<int>, n, minLen, maxLen>();
  iterateBenchImpl(copies, iters);
}

template <
    typename Container,
    std::size_t n,
    std::size_t minLen,
    std::size_t maxLen>
void iterateStrings(std::size_t iters) {
  const auto& copies =
      generateContainerCopies<Container, std::string, n, minLen, maxLen>();
  iterateBenchImpl(copies, iters);
}

// Disabling clang format for table formatting
// clang-format off
BENCHMARK(IterateVecIntsCacheMiss_20_0_15,     n) { iterateInts   <vv_int,  20, 0, 15>(n); }
BENCHMARK(IterateTapeIntsCacheMiss_20_0_15,    n) { iterateInts   <tv_int,  20, 0, 15>(n); }
BENCHMARK(IterateVecStringsCacheMiss_20_0_15,  n) { iterateStrings<vec_str, 20, 0, 15>(n); }
BENCHMARK(IterateTapeStringsCacheMiss_20_0_15, n) { iterateStrings<st_tape, 20, 0, 15>(n); }
BENCHMARK_DRAW_LINE();
BENCHMARK(PushBackVecInts_20_0_15,   n) { pushBackInts<vv_int, 20, 0, 15>(n); }
BENCHMARK(PushBackTapeInts_20_0_15,  n) { pushBackInts<tv_int, 20, 0, 15>(n); }
BENCHMARK_DRAW_LINE();
BENCHMARK(ConstructVecSmallStrings_20_0_15,   n) { constructorStrings<vec_str, 20, 0, 15>(n); }
BENCHMARK(ConstructTapeSmallStrings_20_0_15,  n) { constructorStrings<st_tape, 20, 0, 15>(n); }
BENCHMARK(ConstructVecLargeStrings_20_16_32,  n) { constructorStrings<vec_str, 20, 16, 32>(n); }
BENCHMARK(ConstructTapeLargeStrings_20_16_32, n) { constructorStrings<st_tape, 20, 16, 32>(n); }
BENCHMARK_DRAW_LINE();
BENCHMARK(ConstructVec2SmallStrings,    n) { constructorStrings<vec_str, 2, 0, 15>(n); }
BENCHMARK(ConstructTape2SmallStrings,   n) { constructorStrings<st_tape, 2, 0, 15>(n); }
// clang-format on

} // namespace

int main(int argc, char** argv) {
  folly::Init init(&argc, &argv);
  folly::runBenchmarks();
  return 0;
}
