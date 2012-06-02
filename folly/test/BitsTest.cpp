/*
 * Copyright 2012 Facebook, Inc.
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

// @author Tudor Bosman (tudorb@fb.com)

#include <gflags/gflags.h>
#include "folly/Bits.h"
#include "folly/Benchmark.h"
#include <gtest/gtest.h>

using namespace folly;

namespace {

template <class INT>
void testFFS() {
  EXPECT_EQ(0, findFirstSet(static_cast<INT>(0)));
  size_t bits = std::numeric_limits<
    typename std::make_unsigned<INT>::type>::digits;
  for (size_t i = 0; i < bits; i++) {
    INT v = (static_cast<INT>(1) << (bits - 1)) |
            (static_cast<INT>(1) << i);
    EXPECT_EQ(i+1, findFirstSet(v));
  }
}

template <class INT>
unsigned int findLastSetPortable(INT x) {
  return detail::findLastSetPortable(
      static_cast<typename std::make_unsigned<INT>::type>(x));
}

template <class INT>
void testFLS() {
  typedef typename std::make_unsigned<INT>::type UINT;
  EXPECT_EQ(0, findLastSet(static_cast<INT>(0)));
  size_t bits = std::numeric_limits<UINT>::digits;
  for (size_t i = 0; i < bits; i++) {
    INT v1 = static_cast<UINT>(1) << i;
    EXPECT_EQ(i + 1, findLastSet(v1));
    EXPECT_EQ(i + 1, findLastSetPortable(v1));

    INT v2 = (static_cast<UINT>(1) << i) - 1;
    EXPECT_EQ(i, findLastSet(v2));
    EXPECT_EQ(i, findLastSetPortable(v2));
  }
}

}  // namespace

TEST(Bits, FindFirstSet) {
  testFFS<char>();
  testFFS<signed char>();
  testFFS<unsigned char>();
  testFFS<short>();
  testFFS<unsigned short>();
  testFFS<int>();
  testFFS<unsigned int>();
  testFFS<long>();
  testFFS<unsigned long>();
  testFFS<long long>();
  testFFS<unsigned long long>();
}

TEST(Bits, FindLastSet) {
  testFLS<char>();
  testFLS<signed char>();
  testFLS<unsigned char>();
  testFLS<short>();
  testFLS<unsigned short>();
  testFLS<int>();
  testFLS<unsigned int>();
  testFLS<long>();
  testFLS<unsigned long>();
  testFLS<long long>();
  testFLS<unsigned long long>();
}

#define testPowTwo(nextPowTwoFunc) {                              \
  EXPECT_EQ(1, nextPowTwoFunc(0u));                               \
  EXPECT_EQ(1, nextPowTwoFunc(1u));                               \
  EXPECT_EQ(2, nextPowTwoFunc(2u));                               \
  EXPECT_EQ(4, nextPowTwoFunc(3u));                               \
  EXPECT_EQ(4, nextPowTwoFunc(4u));                               \
  EXPECT_EQ(8, nextPowTwoFunc(5u));                               \
  EXPECT_EQ(8, nextPowTwoFunc(6u));                               \
  EXPECT_EQ(8, nextPowTwoFunc(7u));                               \
  EXPECT_EQ(8, nextPowTwoFunc(8u));                               \
  EXPECT_EQ(16, nextPowTwoFunc(9u));                              \
  EXPECT_EQ(16, nextPowTwoFunc(13u));                             \
  EXPECT_EQ(16, nextPowTwoFunc(16u));                             \
  EXPECT_EQ(512, nextPowTwoFunc(510u));                           \
  EXPECT_EQ(512, nextPowTwoFunc(511u));                           \
  EXPECT_EQ(512, nextPowTwoFunc(512u));                           \
  EXPECT_EQ(1024, nextPowTwoFunc(513u));                          \
  EXPECT_EQ(1024, nextPowTwoFunc(777u));                          \
  EXPECT_EQ(1ul << 31, nextPowTwoFunc((1ul << 31) - 1));          \
  EXPECT_EQ(1ul << 32, nextPowTwoFunc((1ul << 32) - 1));          \
  EXPECT_EQ(1ull << 63, nextPowTwoFunc((1ull << 62) + 1));        \
}


#ifdef __GNUC__

TEST(Bits, nextPowTwoClz) {
  testPowTwo(nextPowTwo);
}

int x; // prevent the loop from getting optimized away
BENCHMARK(nextPowTwoClz, iters) {
  x = folly::nextPowTwo(iters);
}

#endif

TEST(Bits, nextPowTwoPortable) {
  testPowTwo(detail::nextPowTwoPortable);
}

BENCHMARK(nextPowTwoPortable, iters) {
  x = detail::nextPowTwoPortable(iters);
}

BENCHMARK_DRAW_LINE();

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);
  auto ret = RUN_ALL_TESTS();
  if (!ret && FLAGS_benchmark) {
    folly::runBenchmarks();
  }
  return ret;
}

/*
Benchmarks run on dual Xeon X5650's @ 2.67GHz w/hyperthreading enabled
  (12 physical cores, 12 MB cache, 72 GB RAM)

Benchmark                               Iters   Total t    t/iter iter/sec
------------------------------------------------------------------------------
*       nextPowTwoClz                 1000000  1.659 ms  1.659 ns  574.8 M
 +66.8% nextPowTwoPortable            1000000  2.767 ms  2.767 ns  344.7 M
*/
