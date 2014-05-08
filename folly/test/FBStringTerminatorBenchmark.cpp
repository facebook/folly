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

#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <gflags/gflags.h>

/**
 * fbstring::c_str/data used to always write '\0' if
 * FBSTRING_CONSERVATIVE was not defined.
 *
 * Multiple threads calling c_str/data on the same fbstring leads to
 * cache thrashing.
 *
 * fbstring::c_str was changed to conditionally write '\0'.
 *
 */

// #define FBSTRING_PERVERSE
// #define FBSTRING_CONSERVATIVE
#include "folly/FBString.h"
#include "folly/Benchmark.h"
#include "folly/Range.h"


template <class S>
class Bench {
 public:
  void benchStatic(size_t n) const {
    static S what = "small string";
    for (size_t i = 0; i < n; i++) {
      folly::doNotOptimizeAway(what.data());
    }
  }

  void benchPrivate(size_t n) const {
    S what = "small string";
    for (size_t i = 0; i < n; i++) {
      folly::doNotOptimizeAway(what.data());
    }
  }
};

void threadify(std::function<void()> fn, int nrThreads) {
  std::vector<std::thread> threads;
  for (int i = 0; i < nrThreads; i++) {
    threads.emplace_back(fn);
  }
  for (auto& t : threads) { t.join(); }
}

/*
 * Private/static benchmark pairs should give the same numbers if run
 * in single threaded mode (regardless of '\0' being conditionally
 * written or not).
 */
BENCHMARK(static_std_1t, n) {
  threadify([n] { Bench<std::string>().benchStatic(n); }, 1);
}

BENCHMARK_RELATIVE(privat_std_1t, n) {
  threadify([n] { Bench<std::string>().benchPrivate(n); }, 1);
}

BENCHMARK_RELATIVE(static_fbs_1t, n) {
  threadify([n] { Bench<folly::fbstring>().benchStatic(n); }, 1);
}

BENCHMARK_RELATIVE(privat_fbs_1t, n) {
  threadify([n] { Bench<folly::fbstring>().benchPrivate(n); }, 1);
}

BENCHMARK_RELATIVE(static_sp__1t, n) {
  threadify([n] { Bench<folly::StringPiece>().benchStatic(n); }, 1);
}

BENCHMARK_RELATIVE(privat_sp__1t, n) {
  threadify([n] { Bench<folly::StringPiece>().benchPrivate(n); }, 1);
}

/*
 * Private/static benchmark pairs when run on multiple threads:
 *
 * - should be similar if '\0' is conditionally written or when
 *   FBSTRING_CONSERVATIVE is defined (in this case '\0' is not written at all)
 *
 * - static used to be significanlty slower than private for fbstring
 *   with unconditional '\0' written at the end.
 */
BENCHMARK(static_std_32t, n) {
  threadify([n] { Bench<std::string>().benchStatic(n); }, 32);
}

BENCHMARK_RELATIVE(privat_std_32t, n) {
  threadify([n] { Bench<std::string>().benchPrivate(n); }, 32);
}

BENCHMARK_RELATIVE(static_fbs_32t, n) {
  threadify([n] { Bench<folly::fbstring>().benchStatic(n); }, 32);
}

BENCHMARK_RELATIVE(privat_fbs_32t, n) {
  threadify([n] { Bench<folly::fbstring>().benchPrivate(n); }, 32);
}

BENCHMARK_RELATIVE(static_sp__32t, n) {
  threadify([n] { Bench<folly::StringPiece>().benchStatic(n); }, 32);
}

BENCHMARK_RELATIVE(privat_sp__32t, n) {
  threadify([n] { Bench<folly::StringPiece>().benchPrivate(n); }, 32);
}

int main(int argc, char *argv[]) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
