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

#include "folly/Range.h"
#include "folly/Benchmark.h"
#include "folly/Foreach.h"
#include <algorithm>
#include <iostream>
#include <string>

using namespace folly;
using namespace std;

namespace {

std::string str;

void initStr(int len) {
  cout << "string length " << len << ':' << endl;
  str.clear();
  str.reserve(len + 1);
  str.append(len, 'a');
  str.append(1, 'b');
}

}  // anonymous namespace

BENCHMARK(FindSingleCharMemchr, n) {
  StringPiece haystack(str);
  FOR_EACH_RANGE (i, 0, n) {
    doNotOptimizeAway(haystack.find('b'));
    char x = haystack[0];
    doNotOptimizeAway(&x);
  }
}

BENCHMARK_RELATIVE(FindSingleCharRange, n) {
  const char c = 'b';
  StringPiece haystack(str);
  folly::StringPiece needle(&c, &c + 1);
  FOR_EACH_RANGE (i, 0, n) {
    doNotOptimizeAway(haystack.find(needle));
    char x = haystack[0];
    doNotOptimizeAway(&x);
  }
}

BENCHMARK_DRAW_LINE();

BENCHMARK(FindFirstOfRange, n) {
  StringPiece haystack(str);
  folly::StringPiece needles("bc");
  DCHECK_EQ(haystack.size() - 1, haystack.find_first_of(needles)); // it works!
  FOR_EACH_RANGE (i, 0, n) {
    doNotOptimizeAway(haystack.find_first_of(needles));
    char x = haystack[0];
    doNotOptimizeAway(&x);
  }
}

BENCHMARK(FindFirstOfOffsetRange, n) {
  StringPiece haystack(str);
  folly::StringPiece needles("bc");
  DCHECK_EQ(haystack.size() - 1, haystack.find_first_of(needles, 1)); // works!
  FOR_EACH_RANGE (i, 0, n) {
    size_t pos = i % 2; // not a constant to prevent optimization
    doNotOptimizeAway(haystack.find_first_of(needles, pos));
    char x = haystack[0];
    doNotOptimizeAway(&x);
  }
}

int main(int argc, char** argv) {
  google::ParseCommandLineFlags(&argc, &argv, true);

  for (int len : {1, 10, 256, 10*1024, 10*1024*1024}) {
    initStr(len);
    runBenchmarks();
  }
  return 0;
}
