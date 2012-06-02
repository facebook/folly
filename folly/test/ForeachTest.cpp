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

#include "folly/Foreach.h"

#include "folly/Benchmark.h"
#include <gtest/gtest.h>
#include <map>
#include <string>
#include <vector>
#include <list>

using namespace folly;
using namespace folly::detail;

TEST(Foreach, ForEachKV) {
  std::map<std::string, int> testMap;
  testMap["abc"] = 1;
  testMap["def"] = 2;
  std::string keys = "";
  int values = 0;
  int numEntries = 0;
  FOR_EACH_KV (key, value, testMap) {
    keys += key;
    values += value;
    ++numEntries;
  }
  EXPECT_EQ("abcdef", keys);
  EXPECT_EQ(3, values);
  EXPECT_EQ(2, numEntries);
}

TEST(Foreach, ForEachKVBreak) {
  std::map<std::string, int> testMap;
  testMap["abc"] = 1;
  testMap["def"] = 2;
  std::string keys = "";
  int values = 0;
  int numEntries = 0;
  FOR_EACH_KV (key, value, testMap) {
    keys += key;
    values += value;
    ++numEntries;
    break;
  }
  EXPECT_EQ("abc", keys);
  EXPECT_EQ(1, values);
  EXPECT_EQ(1, numEntries);
}

TEST(Foreach, ForEachKvWithMultiMap) {
  std::multimap<std::string, int> testMap;
  testMap.insert(std::make_pair("abc", 1));
  testMap.insert(std::make_pair("abc", 2));
  testMap.insert(std::make_pair("def", 3));
  std::string keys = "";
  int values = 0;
  int numEntries = 0;
  FOR_EACH_KV (key, value, testMap) {
    keys += key;
    values += value;
    ++numEntries;
  }
  EXPECT_EQ("abcabcdef", keys);
  EXPECT_EQ(6, values);
  EXPECT_EQ(3, numEntries);
}

TEST(Foreach, ForEachEnumerate) {
  std::vector<int> vv;
  int sumAA = 0;
  int sumIter = 0;
  int numIterations = 0;
  FOR_EACH_ENUMERATE(aa, iter, vv) {
    sumAA += aa;
    sumIter += *iter;
    ++numIterations;
  }
  EXPECT_EQ(sumAA, 0);
  EXPECT_EQ(sumIter, 0);
  EXPECT_EQ(numIterations, 0);

  vv.push_back(1);
  vv.push_back(3);
  vv.push_back(5);
  FOR_EACH_ENUMERATE(aa, iter, vv) {
    sumAA += aa;
    sumIter += *iter;
    ++numIterations;
  }
  EXPECT_EQ(sumAA, 3);   // 0 + 1 + 2
  EXPECT_EQ(sumIter, 9); // 1 + 3 + 5
  EXPECT_EQ(numIterations, 3);
}

TEST(Foreach, ForEachEnumerateBreak) {
  std::vector<int> vv;
  int sumAA = 0;
  int sumIter = 0;
  int numIterations = 0;
  vv.push_back(1);
  vv.push_back(2);
  vv.push_back(4);
  vv.push_back(8);
  FOR_EACH_ENUMERATE(aa, iter, vv) {
    sumAA += aa;
    sumIter += *iter;
    ++numIterations;
    if (aa == 1) break;
  }
  EXPECT_EQ(sumAA, 1);   // 0 + 1
  EXPECT_EQ(sumIter, 3); // 1 + 2
  EXPECT_EQ(numIterations, 2);
}

TEST(Foreach, ForEachRangeR) {
  int sum = 0;

  FOR_EACH_RANGE_R (i, 0, 0) {
    sum += i;
  }
  EXPECT_EQ(0, sum);

  FOR_EACH_RANGE_R (i, 0, -1) {
    sum += i;
  }
  EXPECT_EQ(0, sum);

  FOR_EACH_RANGE_R (i, 0, 5) {
    sum += i;
  }
  EXPECT_EQ(10, sum);

  std::list<int> lst = { 0, 1, 2, 3, 4 };
  sum = 0;
  FOR_EACH_RANGE_R (i, lst.begin(), lst.end()) {
    sum += *i;
  }
  EXPECT_EQ(10, sum);
}

// Benchmarks:
// 1. Benchmark iterating through the man with FOR_EACH, and also assign
//    iter->first and iter->second to local vars inside the FOR_EACH loop.
// 2. Benchmark iterating through the man with FOR_EACH, but use iter->first and
//    iter->second as is, without assigning to local variables.
// 3. Use FOR_EACH_KV loop to iterate through the map.

std::map<int, std::string> bmMap;  // For use in benchmarks below.

void setupBenchmark(int iters) {
  bmMap.clear();
  for (int i = 0; i < iters; ++i) {
    bmMap[i] = "teststring";
  }
}

BENCHMARK(ForEachKVNoMacroAssign, iters) {
  int sumKeys = 0;
  std::string sumValues;

  BENCHMARK_SUSPEND {
    setupBenchmark(iters);
    int sumKeys = 0;
    std::string sumValues = "";
  }

  FOR_EACH (iter, bmMap) {
    const int k = iter->first;
    const std::string v = iter->second;
    sumKeys += k;
    sumValues += v;
  }
}

BENCHMARK(ForEachKVNoMacroNoAssign, iters) {
  int sumKeys = 0;
  std::string sumValues;

  BENCHMARK_SUSPEND {
    setupBenchmark(iters);
  }

  FOR_EACH (iter, bmMap) {
    sumKeys += iter->first;
    sumValues += iter->second;
  }
}

BENCHMARK(ManualLoopNoAssign, iters) {
  BENCHMARK_SUSPEND {
    setupBenchmark(iters);
  }
  int sumKeys = 0;
  std::string sumValues;

  for (auto iter = bmMap.begin(); iter != bmMap.end(); ++iter) {
    sumKeys += iter->first;
    sumValues += iter->second;
  }
}

BENCHMARK(ForEachKVMacro, iters) {
  BENCHMARK_SUSPEND {
    setupBenchmark(iters);
  }
  int sumKeys = 0;
  std::string sumValues;

  FOR_EACH_KV (k, v, bmMap) {
    sumKeys += k;
    sumValues += v;
  }
}

BENCHMARK(ForEachManual, iters) {
  int sum = 1;
  for (auto i = 1; i < iters; ++i) {
    sum *= i;
  }
  doNotOptimizeAway(sum);
}

BENCHMARK(ForEachRange, iters) {
  int sum = 1;
  FOR_EACH_RANGE (i, 1, iters) {
    sum *= i;
  }
  doNotOptimizeAway(sum);
}

BENCHMARK(ForEachDescendingManual, iters) {
  int sum = 1;
  for (auto i = iters; i-- > 1; ) {
    sum *= i;
  }
  doNotOptimizeAway(sum);
}

BENCHMARK(ForEachRangeR, iters) {
  int sum = 1;
  FOR_EACH_RANGE_R (i, 1, iters) {
    sum *= i;
  }
  doNotOptimizeAway(sum);
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  auto r = RUN_ALL_TESTS();
  if (r) {
    return r;
  }
  runBenchmarks();
  return 0;
}
