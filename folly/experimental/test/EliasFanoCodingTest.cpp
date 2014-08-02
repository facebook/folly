/*
 * Copyright 2014 Facebook, Inc.
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
#include <random>
#include <vector>

#include <folly/Benchmark.h>
#include <folly/experimental/EliasFanoCoding.h>
#include <folly/experimental/test/CodingTestUtils.h>

using namespace folly::compression;

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
    typedef EliasFanoReader<Encoder> Reader;
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

  order.clear();
  order.reserve(data.size());
  for (size_t i = 0; i < data.size(); ++i) {
    order.push_back(i);
  }
  std::shuffle(order.begin(), order.end(), gen);

  encodeSmallData = generateRandomList(10, 100 * 1000, gen);
  encodeLargeData = generateRandomList(1000 * 1000, 100 * 1000 * 1000, gen);
}

void free() {
  list.free();
}

}  // namespace bm

BENCHMARK(Next_1M) {
  bmNext<bm::Reader>(bm::list, bm::data, bm::k1M);
}

BENCHMARK(Skip1_ForwarQ128_1M) {
  bmSkip<bm::Reader>(bm::list, bm::data, 1, bm::k1M);
}

BENCHMARK(Skip10_ForwarQ128_1M) {
  bmSkip<bm::Reader>(bm::list, bm::data, 10, bm::k1M);
}

BENCHMARK(Skip100_ForwardQ128_1M) {
  bmSkip<bm::Reader>(bm::list, bm::data, 100, bm::k1M);
}

BENCHMARK(Skip1000_ForwardQ128_1M) {
  bmSkip<bm::Reader>(bm::list, bm::data, 1000, bm::k1M);
}

BENCHMARK(Jump_ForwardQ128_1M) {
  bmJump<bm::Reader>(bm::list, bm::data, bm::order, bm::k1M);
}

BENCHMARK_DRAW_LINE();

BENCHMARK(SkipTo1_SkipQ128_1M) {
  bmSkipTo<bm::Reader>(bm::list, bm::data, 1, bm::k1M);
}

BENCHMARK(SkipTo10_SkipQ128_1M) {
  bmSkipTo<bm::Reader>(bm::list, bm::data, 10, bm::k1M);
}

BENCHMARK(SkipTo100_SkipQ128_1M) {
  bmSkipTo<bm::Reader>(bm::list, bm::data, 100, bm::k1M);
}

BENCHMARK(SkipTo1000_SkipQ128_1M) {
  bmSkipTo<bm::Reader>(bm::list, bm::data, 1000, bm::k1M);
}

BENCHMARK(JumpTo_SkipQ128_1M) {
  bmJumpTo<bm::Reader>(bm::list, bm::data, bm::order, bm::k1M);
}

BENCHMARK_DRAW_LINE();

BENCHMARK(Encode_10) {
  auto list = bm::Encoder::encode(bm::encodeSmallData.begin(),
                                  bm::encodeSmallData.end());
  list.free();
}

BENCHMARK(Encode_1M) {
  auto list = bm::Encoder::encode(bm::encodeLargeData.begin(),
                                  bm::encodeLargeData.end());
  list.free();
}

#if 0
Intel(R) Xeon(R) CPU E5-2660 @ 2.7GHz (turbo on), using instructions::Fast.

============================================================================
folly/experimental/test/EliasFanoCodingTest.cpp relative  time/iter  iters/s
============================================================================
Next_1M                                                      4.61ms   216.70
Skip1_ForwarQ128_1M                                          5.33ms   187.71
Skip10_ForwarQ128_1M                                        14.23ms    70.26
Skip100_ForwardQ128_1M                                      29.10ms    34.37
Skip1000_ForwardQ128_1M                                     21.15ms    47.28
Jump_ForwardQ128_1M                                         46.30ms    21.60
----------------------------------------------------------------------------
SkipTo1_SkipQ128_1M                                         12.03ms    83.15
SkipTo10_SkipQ128_1M                                        36.11ms    27.69
SkipTo100_SkipQ128_1M                                       42.91ms    23.30
SkipTo1000_SkipQ128_1M                                      36.92ms    27.08
JumpTo_SkipQ128_1M                                          78.51ms    12.74
----------------------------------------------------------------------------
Encode_10                                                  199.19ns    5.02M
Encode_1M                                                    8.82ms   113.37
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
