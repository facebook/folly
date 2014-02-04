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

#include "folly/Benchmark.h"
#include "folly/experimental/EliasFanoCoding.h"
#include "folly/experimental/test/CodingTestUtils.h"

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
typename Encoder::CompressedList list;

void init() {
  data = generateRandomList(100 * 1000, 10 * 1000 * 1000);
  //data = loadList("/home/philipp/pl_test_dump.txt");
  Encoder::encode(data.data(), data.size(), bm::list);
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

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);

  auto ret = RUN_ALL_TESTS();
  if (ret == 0 && FLAGS_benchmark) {
    bm::init();
    folly::runBenchmarks();
    bm::free();
  }

  return ret;
}
