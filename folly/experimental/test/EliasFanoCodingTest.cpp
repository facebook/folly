/*
 * Copyright 2016 Facebook, Inc.
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

#include <algorithm>
#include <numeric>
#include <random>
#include <vector>

#include <folly/Benchmark.h>
#include <folly/experimental/EliasFanoCoding.h>
#include <folly/experimental/Select64.h>
#include <folly/experimental/test/CodingTestUtils.h>

using namespace folly::compression;

#ifndef EF_TEST_ARCH
#define EF_TEST_ARCH Default
#endif  // EF_TEST_ARCH

class EliasFanoCodingTest : public ::testing::Test {
 public:
  void doTestEmpty() {
    typedef EliasFanoEncoderV2<uint32_t, size_t> Encoder;
    typedef EliasFanoReader<Encoder> Reader;
    testEmpty<Reader, Encoder>();
  }

  template <size_t kSkipQuantum, size_t kForwardQuantum>
  void doTestAll() {
    typedef EliasFanoEncoderV2<
      uint32_t, uint32_t, kSkipQuantum, kForwardQuantum> Encoder;
    typedef EliasFanoReader<Encoder, instructions::EF_TEST_ARCH> Reader;
    testAll<Reader, Encoder>({0});
    testAll<Reader, Encoder>(generateRandomList(100 * 1000, 10 * 1000 * 1000));
    testAll<Reader, Encoder>(generateSeqList(1, 100000, 100));
  }
};

TEST_F(EliasFanoCodingTest, Empty) {
  doTestEmpty();
}

TEST_F(EliasFanoCodingTest, Simple) {
  doTestAll<0, 0>();
}

TEST_F(EliasFanoCodingTest, SkipPointers) {
  doTestAll<128, 0>();
}

TEST_F(EliasFanoCodingTest, ForwardPointers) {
  doTestAll<0, 128>();
}

TEST_F(EliasFanoCodingTest, SkipForwardPointers) {
  doTestAll<128, 128>();
}

TEST_F(EliasFanoCodingTest, Select64) {
  typedef instructions::EF_TEST_ARCH instr;
  constexpr uint64_t kPrime = uint64_t(-59);
  for (uint64_t x = kPrime, i = 0; i < (1 << 20); x *= kPrime, i += 1) {
    size_t w = instr::popcount(x);
    for (size_t k = 0; k < w; ++k) {
      auto pos = folly::select64<instr>(x, k);
      CHECK_EQ((x >> pos) & 1, 1);
      CHECK_EQ(instr::popcount(x & ((uint64_t(1) << pos) - 1)), k);
    }
  }
}

namespace bm {

typedef EliasFanoEncoderV2<uint32_t, uint32_t, 128, 128> Encoder;
typedef EliasFanoReader<Encoder> Reader;

std::vector<uint32_t> data;
std::vector<size_t> order;

std::vector<uint32_t> encodeSmallData;
std::vector<uint32_t> encodeLargeData;

typename Encoder::MutableCompressedList list;

void init() {
  std::mt19937 gen;

  data = generateRandomList(100 * 1000, 10 * 1000 * 1000, gen);
  list = Encoder::encode(data.begin(), data.end());

  order.resize(data.size());
  std::iota(order.begin(), order.end(), size_t());
  std::shuffle(order.begin(), order.end(), gen);

  encodeSmallData = generateRandomList(10, 100 * 1000, gen);
  encodeLargeData = generateRandomList(1000 * 1000, 100 * 1000 * 1000, gen);
}

void free() {
  list.free();
}

}  // namespace bm

BENCHMARK(Next, iters) {
  bmNext<bm::Reader>(bm::list, bm::data, iters);
}

size_t Skip_ForwardQ128(size_t iters, size_t logAvgSkip) {
  bmSkip<bm::Reader>(bm::list, bm::data, logAvgSkip, iters);
  return iters;
}

BENCHMARK_NAMED_PARAM_MULTI(Skip_ForwardQ128, 1, 0)
BENCHMARK_NAMED_PARAM_MULTI(Skip_ForwardQ128, 2, 1)
BENCHMARK_NAMED_PARAM_MULTI(Skip_ForwardQ128, 4_pm_1, 2)
BENCHMARK_NAMED_PARAM_MULTI(Skip_ForwardQ128, 16_pm_4, 4)
BENCHMARK_NAMED_PARAM_MULTI(Skip_ForwardQ128, 64_pm_16, 6)
BENCHMARK_NAMED_PARAM_MULTI(Skip_ForwardQ128, 256_pm_64, 8)
BENCHMARK_NAMED_PARAM_MULTI(Skip_ForwardQ128, 1024_pm_256, 10)

BENCHMARK(Jump_ForwardQ128, iters) {
  bmJump<bm::Reader>(bm::list, bm::data, bm::order, iters);
}

BENCHMARK_DRAW_LINE();

size_t SkipTo_SkipQ128(size_t iters, size_t logAvgSkip) {
  bmSkipTo<bm::Reader>(bm::list, bm::data, logAvgSkip, iters);
  return iters;
}

BENCHMARK_NAMED_PARAM_MULTI(SkipTo_SkipQ128, 1, 0)
BENCHMARK_NAMED_PARAM_MULTI(SkipTo_SkipQ128, 2, 1)
BENCHMARK_NAMED_PARAM_MULTI(SkipTo_SkipQ128, 4_pm_1, 2)
BENCHMARK_NAMED_PARAM_MULTI(SkipTo_SkipQ128, 16_pm_4, 4)
BENCHMARK_NAMED_PARAM_MULTI(SkipTo_SkipQ128, 64_pm_16, 6)
BENCHMARK_NAMED_PARAM_MULTI(SkipTo_SkipQ128, 256_pm_64, 8)
BENCHMARK_NAMED_PARAM_MULTI(SkipTo_SkipQ128, 1024_pm_256, 10)

BENCHMARK(JumpTo_SkipQ128, iters) {
  bmJumpTo<bm::Reader>(bm::list, bm::data, bm::order, iters);
}

BENCHMARK_DRAW_LINE();

BENCHMARK(Encode_10) {
  auto list = bm::Encoder::encode(bm::encodeSmallData.begin(),
                                  bm::encodeSmallData.end());
  list.free();
}

BENCHMARK(Encode) {
  auto list = bm::Encoder::encode(bm::encodeLargeData.begin(),
                                  bm::encodeLargeData.end());
  list.free();
}

#if 0
Intel(R) Xeon(R) CPU E5-2673 v3 @ 2.40GHz (turbo off),
using instructions::Haswell and GCC 4.9 with --bm_min_usec 100000.
============================================================================
folly/experimental/test/EliasFanoCodingTest.cpp relative  time/iter  iters/s
============================================================================
Next                                                         2.59ns  386.60M
Skip_ForwardQ128(1)                                          4.03ns  248.16M
Skip_ForwardQ128(2)                                          5.28ns  189.39M
Skip_ForwardQ128(4_pm_1)                                     7.48ns  133.75M
Skip_ForwardQ128(16_pm_4)                                   20.28ns   49.32M
Skip_ForwardQ128(64_pm_16)                                  28.19ns   35.47M
Skip_ForwardQ128(256_pm_64)                                 31.99ns   31.26M
Skip_ForwardQ128(1024_pm_256)                               32.51ns   30.76M
Jump_ForwardQ128                                            33.77ns   29.61M
----------------------------------------------------------------------------
SkipTo_SkipQ128(1)                                           4.34ns  230.66M
SkipTo_SkipQ128(2)                                           8.90ns  112.38M
SkipTo_SkipQ128(4_pm_1)                                     12.12ns   82.49M
SkipTo_SkipQ128(16_pm_4)                                    32.52ns   30.75M
SkipTo_SkipQ128(64_pm_16)                                   44.82ns   22.31M
SkipTo_SkipQ128(256_pm_64)                                  49.52ns   20.19M
SkipTo_SkipQ128(1024_pm_256)                                52.88ns   18.91M
JumpTo_SkipQ128                                             54.65ns   18.30M
----------------------------------------------------------------------------
Encode_10                                                   98.70ns   10.13M
Encode                                                       5.48ms   182.33
============================================================================
#endif

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  auto ret = RUN_ALL_TESTS();
  if (ret == 0 && FLAGS_benchmark) {
    bm::init();
    folly::runBenchmarks();
    bm::free();
  }

  return ret;
}
