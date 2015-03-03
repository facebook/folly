/*
 * Copyright 2015 Facebook, Inc.
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

#if defined(EF_TEST_NEHALEM)
#define EF_TEST_ARCH Nehalem
#elif defined(EF_TEST_HASWELL)
#define EF_TEST_ARCH Haswell
#else
#define EF_TEST_ARCH Default
#endif

template <size_t kVersion>
struct TestType {
  static constexpr size_t Version = kVersion;
};

template <class T>
class EliasFanoCodingTest : public ::testing::Test {
 public:
  void doTestEmpty() {
    typedef EliasFanoEncoder<uint32_t, size_t, 0, 0, T::Version> Encoder;
    typedef EliasFanoReader<Encoder> Reader;
    testEmpty<Reader, Encoder>();
  }

  template <size_t kSkipQuantum, size_t kForwardQuantum>
  void doTestAll() {
    typedef EliasFanoEncoder<
      uint32_t, uint32_t, kSkipQuantum, kForwardQuantum, T::Version> Encoder;
    typedef EliasFanoReader<Encoder, instructions::EF_TEST_ARCH> Reader;
    testAll<Reader, Encoder>(generateRandomList(100 * 1000, 10 * 1000 * 1000));
    testAll<Reader, Encoder>(generateSeqList(1, 100000, 100));
  }
};

typedef ::testing::Types<TestType<0>, TestType<1>> TestTypes;
TYPED_TEST_CASE(EliasFanoCodingTest, TestTypes);

TYPED_TEST(EliasFanoCodingTest, Empty) {
  TestFixture::doTestEmpty();
}

TYPED_TEST(EliasFanoCodingTest, Simple) {
  TestFixture::template doTestAll<0, 0>();
}

TYPED_TEST(EliasFanoCodingTest, SkipPointers) {
  TestFixture::template doTestAll<128, 0>();
}

TYPED_TEST(EliasFanoCodingTest, ForwardPointers) {
  TestFixture::template doTestAll<0, 128>();
}

TYPED_TEST(EliasFanoCodingTest, SkipForwardPointers) {
  TestFixture::template doTestAll<128, 128>();
}

namespace bm {

constexpr size_t k1M = 1000000;
constexpr size_t kVersion = 1;

typedef EliasFanoEncoder<uint32_t, uint32_t, 128, 128, kVersion> Encoder;
typedef EliasFanoReader<Encoder> Reader;

std::vector<uint32_t> data;
std::vector<size_t> order;

std::vector<uint32_t> encodeSmallData;
std::vector<uint32_t> encodeLargeData;

typename Encoder::CompressedList list;

void init() {
  std::mt19937 gen;

  data = generateRandomList(100 * 1000, 10 * 1000 * 1000, gen);
  //data = loadList("/home/philipp/pl_test_dump.txt");
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

BENCHMARK_DRAW_LINE();

BENCHMARK(Select64, iters) {
  typedef instructions::EF_TEST_ARCH instr;
  constexpr uint64_t kPrime = uint64_t(-59);
  for (uint64_t x = kPrime, i = 0; i < iters; x *= kPrime, i += 1) {
    size_t w = instr::popcount(x);
    folly::doNotOptimizeAway(folly::select64<instr>(x, w - 1));
  }
}

#if 0
Intel(R) Xeon(R) CPU E5-2673 v3 @ 2.40GHz (turbo off),
using instructions::Haswell and GCC 4.9 with --bm_min_usec 100000.
============================================================================
folly/experimental/test/EliasFanoCodingTest.cpp relative  time/iter  iters/s
============================================================================
Next                                                         2.52ns  397.28M
Skip_ForwardQ128(1)                                          3.92ns  255.28M
Skip_ForwardQ128(2)                                          5.08ns  197.04M
Skip_ForwardQ128(4_pm_1)                                     7.04ns  142.02M
Skip_ForwardQ128(16_pm_4)                                   19.68ns   50.82M
Skip_ForwardQ128(64_pm_16)                                  27.58ns   36.26M
Skip_ForwardQ128(256_pm_64)                                 32.49ns   30.78M
Skip_ForwardQ128(1024_pm_256)                               33.39ns   29.95M
Jump_ForwardQ128                                            34.05ns   29.37M
----------------------------------------------------------------------------
SkipTo_SkipQ128(1)                                           4.42ns  226.49M
SkipTo_SkipQ128(2)                                           8.58ns  116.55M
SkipTo_SkipQ128(4_pm_1)                                     11.43ns   87.50M
SkipTo_SkipQ128(16_pm_4)                                    31.19ns   32.06M
SkipTo_SkipQ128(64_pm_16)                                   43.88ns   22.79M
SkipTo_SkipQ128(256_pm_64)                                  49.08ns   20.37M
SkipTo_SkipQ128(1024_pm_256)                                52.24ns   19.14M
JumpTo_SkipQ128                                             54.61ns   18.31M
----------------------------------------------------------------------------
Encode_10                                                  117.24ns    8.53M
Encode                                                       5.64ms   177.15
----------------------------------------------------------------------------
Select64                                                     8.04ns  124.35M
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
