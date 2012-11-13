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

#include "folly/experimental/Gen.h"
#include "folly/experimental/StringGen.h"
#include "folly/experimental/FileGen.h"

#include <atomic>
#include <thread>

#include <glog/logging.h>

#include "folly/Benchmark.h"

using namespace folly;
using namespace folly::gen;
using std::ostream;
using std::pair;
using std::set;
using std::vector;
using std::tuple;

static std::atomic<int> testSize(1000);
static vector<int> testVector =
    seq(1, testSize.load())
  | mapped([](int) { return rand(); })
  | as<vector>();
static vector<vector<int>> testVectorVector =
    seq(1, 100)
  | map([](int i) {
      return seq(1, i) | as<vector>();
    })
  | as<vector>();

auto square = [](int x) { return x * x; };
auto add = [](int a, int b) { return a + b; };
auto multiply = [](int a, int b) { return a * b; };

BENCHMARK(Sum_Basic_NoGen, iters) {
  int limit = testSize.load();
  int s = 0;
  while (iters--) {
    for (int i = 0; i < limit; ++i) {
      s += i;
    }
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_RELATIVE(Sum_Basic_Gen, iters) {
  int limit = testSize.load();
  int s = 0;
  while (iters--) {
    s += range(0, limit) | sum;
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_DRAW_LINE()

BENCHMARK(Sum_Vector_NoGen, iters) {
  int s = 0;
  while (iters--) {
    for (auto& i : testVector) {
      s += i;
    }
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_RELATIVE(Sum_Vector_Gen, iters) {
  int s = 0;
  while (iters--) {
    s += from(testVector) | sum;
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_DRAW_LINE()

BENCHMARK(Count_Vector_NoGen, iters) {
  int s = 0;
  while (iters--) {
    for (auto& i : testVector) {
      if (i * 2 < rand()) {
        ++s;
      }
    }
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_RELATIVE(Count_Vector_Gen, iters) {
  int s = 0;
  while (iters--) {
    s += from(testVector)
       | filter([](int i) {
                  return i * 2 < rand();
                })
       | count;
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_DRAW_LINE()

BENCHMARK(Fib_Sum_NoGen, iters) {
  int s = 0;
  while (iters--) {
    auto fib = [](int limit) -> vector<int> {
      vector<int> ret;
      int a = 0;
      int b = 1;
      for (int i = 0; i * 2 < limit; ++i) {
        ret.push_back(a += b);
        ret.push_back(b += a);
      }
      return ret;
    };
    for (auto& v : fib(testSize.load())) {
      s += v;
    }
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_RELATIVE(Fib_Sum_Gen, iters) {
  int s = 0;
  while (iters--) {
    auto fib = GENERATOR(int, {
      int a = 0;
      int b = 1;
      for (;;) {
        yield(a += b);
        yield(b += a);
      }
    });
    s += fib | take(testSize.load()) | sum;
  }
  folly::doNotOptimizeAway(s);
}

struct FibYielder {
  template<class Yield>
  void operator()(Yield&& yield) const {
    int a = 0;
    int b = 1;
    for (;;) {
      yield(a += b);
      yield(b += a);
    }
  }
};

BENCHMARK_RELATIVE(Fib_Sum_Gen_Static, iters) {
  int s = 0;
  while (iters--) {
    auto fib = generator<int>(FibYielder());
    s += fib | take(testSize.load()) | sum;
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_DRAW_LINE()

BENCHMARK(VirtualGen_0Virtual, iters) {
  int s = 0;
  while (iters--) {
    auto numbers = seq(1, 10000);
    auto squares = numbers | map(square);
    auto quads = squares | map(square);
    s += quads | sum;
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_RELATIVE(VirtualGen_1Virtual, iters) {
  int s = 0;
  while (iters--) {
    VirtualGen<int> numbers = seq(1, 10000);
    auto squares = numbers | map(square);
    auto quads = squares | map(square);
    s += quads | sum;
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_RELATIVE(VirtualGen_2Virtual, iters) {
  int s = 0;
  while (iters--) {
    VirtualGen<int> numbers = seq(1, 10000);
    VirtualGen<int> squares = numbers | map(square);
    auto quads = squares | map(square);
    s += quads | sum;
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_RELATIVE(VirtualGen_3Virtual, iters) {
  int s = 0;
  while (iters--) {
    VirtualGen<int> numbers = seq(1, 10000);
    VirtualGen<int> squares = numbers | map(square);
    VirtualGen<int> quads = squares | map(square);
    s += quads | sum;
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_DRAW_LINE()

BENCHMARK(Concat_NoGen, iters) {
  int s = 0;
  while (iters--) {
    for (auto& v : testVectorVector) {
      for (auto& i : v) {
        s += i;
      }
    }
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_RELATIVE(Concat_Gen, iters) {
  int s = 0;
  while (iters--) {
    s += from(testVectorVector) | rconcat | sum;
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_DRAW_LINE()

BENCHMARK(Composed_NoGen, iters) {
  int s = 0;
  while (iters--) {
    for (auto& i : testVector) {
      s += i * i;
    }
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_RELATIVE(Composed_Gen, iters) {
  int s = 0;
  auto sumSq = map(square) | sum;
  while (iters--) {
    s += from(testVector) | sumSq;
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_RELATIVE(Composed_GenRegular, iters) {
  int s = 0;
  while (iters--) {
    s += from(testVector) | map(square) | sum;
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_DRAW_LINE()

namespace {

const char* const kLine = "The quick brown fox jumped over the lazy dog.\n";
const size_t kLineCount = 10000;
std::string bigLines;
const size_t kSmallLineSize = 17;
std::vector<std::string> smallLines;

void initStringResplitterBenchmark() {
  bigLines.reserve(kLineCount * strlen(kLine));
  for (size_t i = 0; i < kLineCount; ++i) {
    bigLines += kLine;
  }
  size_t remaining = bigLines.size();
  size_t pos = 0;
  while (remaining) {
    size_t n = std::min(kSmallLineSize, remaining);
    smallLines.push_back(bigLines.substr(pos, n));
    pos += n;
    remaining -= n;
  }
}

size_t len(folly::StringPiece s) { return s.size(); }

}  // namespace

BENCHMARK(StringResplitter_Big, iters) {
  size_t s = 0;
  while (iters--) {
    s += from({bigLines}) | resplit('\n') | map(&len) | sum;
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_RELATIVE(StringResplitter_Small, iters) {
  size_t s = 0;
  while (iters--) {
    s += from(smallLines) | resplit('\n') | map(&len) | sum;
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_DRAW_LINE()

BENCHMARK(ByLine_Pipes, iters) {
  std::thread thread;
  int rfd;
  int wfd;
  BENCHMARK_SUSPEND {
    int p[2];
    CHECK_ERR(::pipe(p));
    rfd = p[0];
    wfd = p[1];
    thread = std::thread([wfd, iters] {
      char x = 'x';
      PCHECK(::write(wfd, &x, 1) == 1);  // signal startup
      FILE* f = fdopen(wfd, "w");
      PCHECK(f);
      for (int i = 1; i <= iters; ++i) {
        fprintf(f, "%d\n", i);
      }
      fclose(f);
    });
    char buf;
    PCHECK(::read(rfd, &buf, 1) == 1);  // wait for startup
  }

  auto s = byLine(rfd) | eachTo<int64_t>() | sum;
  folly::doNotOptimizeAway(s);

  BENCHMARK_SUSPEND {
    ::close(rfd);
    CHECK_EQ(s, int64_t(iters) * (iters + 1) / 2);
    thread.join();
  }
}

// Results from a dual core Xeon L5520 @ 2.27GHz:
//
// ============================================================================
// folly/experimental/test/GenBenchmark.cpp        relative  time/iter  iters/s
// ============================================================================
// Sum_Basic_NoGen                                            301.60ns    3.32M
// Sum_Basic_Gen                                    104.27%   289.24ns    3.46M
// ----------------------------------------------------------------------------
// Sum_Vector_NoGen                                           200.33ns    4.99M
// Sum_Vector_Gen                                    99.81%   200.70ns    4.98M
// ----------------------------------------------------------------------------
// Count_Vector_NoGen                                          12.37us   80.84K
// Count_Vector_Gen                                 103.09%    12.00us   83.33K
// ----------------------------------------------------------------------------
// Fib_Sum_NoGen                                                3.66us  273.21K
// Fib_Sum_Gen                                       43.06%     8.50us  117.65K
// Fib_Sum_Gen_Static                                87.81%     4.17us  239.89K
// ----------------------------------------------------------------------------
// VirtualGen_0Virtual                                         10.04us   99.61K
// VirtualGen_1Virtual                               29.59%    33.93us   29.47K
// VirtualGen_2Virtual                               20.45%    49.10us   20.37K
// VirtualGen_3Virtual                               15.49%    64.82us   15.43K
// ----------------------------------------------------------------------------
// Concat_NoGen                                                 2.50us  400.37K
// Concat_Gen                                       102.50%     2.44us  410.37K
// ----------------------------------------------------------------------------
// Composed_NoGen                                             549.54ns    1.82M
// Composed_Gen                                     101.39%   542.00ns    1.85M
// Composed_GenRegular                               99.66%   551.40ns    1.81M
// ============================================================================

int main(int argc, char *argv[]) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  initStringResplitterBenchmark();
  runBenchmarks();
  return 0;
}
