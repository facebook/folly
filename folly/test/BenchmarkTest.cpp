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

#include "folly/Benchmark.h"
#include "folly/Foreach.h"
#include "folly/String.h"
#include <iostream>
using namespace folly;
using namespace std;

void fun() {
  static double x = 1;
  ++x;
  doNotOptimizeAway(x);
}
BENCHMARK(bmFun) { fun(); }
BENCHMARK(bmRepeatedFun, n) {
  FOR_EACH_RANGE (i, 0, n) {
    fun();
  }
}
BENCHMARK_DRAW_LINE()

BENCHMARK(gun) {
  static double x = 1;
  x *= 2000;
  doNotOptimizeAway(x);
}

BENCHMARK_DRAW_LINE()

BENCHMARK(baselinevector) {
  vector<int> v;

  BENCHMARK_SUSPEND {
    v.resize(1000);
  }

  FOR_EACH_RANGE (i, 0, 100) {
    v.push_back(42);
  }
}

BENCHMARK_RELATIVE(bmVector) {
  vector<int> v;
  FOR_EACH_RANGE (i, 0, 100) {
    v.resize(v.size() + 1, 42);
  }
}

BENCHMARK_DRAW_LINE()

BENCHMARK(superslow) {
  sleep(1);
}

int main(int argc, char** argv) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  runBenchmarks();
  runBenchmarksOnFlag();
}
