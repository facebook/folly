/*
 * Copyright 2013 Facebook, Inc.
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

#include "folly/experimental/test/CodingTestUtils.h"
#include "folly/experimental/EliasFanoCoding.h"
#include "folly/Benchmark.h"

using namespace folly::compression;

template <class List>
void testAll() {
  typedef EliasFanoReader<List> Reader;
  testAll<Reader, List>(generateRandomList(100 * 1000, 10 * 1000 * 1000));
  testAll<Reader, List>(generateSeqList(1, 100000, 100));
}

TEST(EliasFanoCompressedList, Empty) {
  typedef EliasFanoCompressedList<uint32_t> List;
  typedef EliasFanoReader<List> Reader;
  testEmpty<Reader, List>();
}

TEST(EliasFanoCompressedList, Simple) {
  testAll<EliasFanoCompressedList<uint32_t> >();
}

TEST(EliasFanoCompressedList, SkipPointers) {
  testAll<EliasFanoCompressedList<uint32_t, uint32_t, 128, 0> >();
}

TEST(EliasFanoCompressedList, ForwardPointers) {
  testAll<EliasFanoCompressedList<uint32_t, uint32_t, 0, 128> >();
}

TEST(EliasFanoCompressedList, SkipForwardPointers) {
  testAll<EliasFanoCompressedList<uint32_t, uint32_t, 128, 128> >();
}

namespace bm {

constexpr size_t k1M = 1000000;
typedef EliasFanoCompressedList<uint32_t, uint32_t, 128, 128> List;
typedef EliasFanoReader<List> Reader;

std::vector<uint32_t> data;
List list;

void init() {
  data = generateRandomList(100 * 1000, 10 * 1000 * 1000);
  //data = loadList("/home/philipp/pl_test_dump.txt");
  List::encode(data.data(), data.size(), bm::list);
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
