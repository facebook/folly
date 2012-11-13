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
#include "folly/String.h"

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

BENCHMARK(StringSplit_Old, iters) {
  size_t s = 0;
  std::string line(kLine);
  while (iters--) {
    std::vector<StringPiece> parts;
    split(' ', line, parts);
    s += parts.size();
  }
  folly::doNotOptimizeAway(s);
}


BENCHMARK_RELATIVE(StringSplit_Gen_Vector, iters) {
  size_t s = 0;
  StringPiece line(kLine);
  while (iters--) {
    s += (split(line, ' ') | as<vector>()).size();
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_DRAW_LINE()

BENCHMARK(StringSplit_Old_ReuseVector, iters) {
  size_t s = 0;
  std::string line(kLine);
  std::vector<StringPiece> parts;
  while (iters--) {
    parts.clear();
    split(' ', line, parts);
    s += parts.size();
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_RELATIVE(StringSplit_Gen_ReuseVector, iters) {
  size_t s = 0;
  StringPiece line(kLine);
  std::vector<StringPiece> parts;
  while (iters--) {
    parts.clear();
    split(line, ' ') | appendTo(parts);
    s += parts.size();
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_RELATIVE(StringSplit_Gen, iters) {
  size_t s = 0;
  StringPiece line(kLine);
  while (iters--) {
    s += split(line, ' ') | count;
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_RELATIVE(StringSplit_Gen_Take, iters) {
  size_t s = 0;
  StringPiece line(kLine);
  while (iters--) {
    s += split(line, ' ') | take(10) | count;
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
// Sum_Basic_NoGen                                            293.77ns    3.40M
// Sum_Basic_Gen                                    100.24%   293.08ns    3.41M
// ----------------------------------------------------------------------------
// Sum_Vector_NoGen                                           199.09ns    5.02M
// Sum_Vector_Gen                                    98.57%   201.98ns    4.95M
// ----------------------------------------------------------------------------
// Count_Vector_NoGen                                          12.40us   80.66K
// Count_Vector_Gen                                 103.07%    12.03us   83.13K
// ----------------------------------------------------------------------------
// Fib_Sum_NoGen                                                3.65us  274.29K
// Fib_Sum_Gen                                       41.95%     8.69us  115.06K
// Fib_Sum_Gen_Static                                86.10%     4.23us  236.15K
// ----------------------------------------------------------------------------
// VirtualGen_0Virtual                                         10.10us   99.03K
// VirtualGen_1Virtual                               29.67%    34.04us   29.38K
// VirtualGen_2Virtual                               20.53%    49.19us   20.33K
// VirtualGen_3Virtual                               15.22%    66.36us   15.07K
// ----------------------------------------------------------------------------
// Concat_NoGen                                                 2.33us  428.35K
// Concat_Gen                                        85.36%     2.74us  365.62K
// ----------------------------------------------------------------------------
// Composed_NoGen                                             552.78ns    1.81M
// Composed_Gen                                     100.48%   550.14ns    1.82M
// Composed_GenRegular                              100.60%   549.50ns    1.82M
// ----------------------------------------------------------------------------
// StringResplitter_Big                                       118.40us    8.45K
// StringResplitter_Small                            12.96%   913.23us    1.10K
// ----------------------------------------------------------------------------
// StringSplit_Old                                            567.61ns    1.76M
// StringSplit_Gen_Vector                           146.52%   387.41ns    2.58M
// ----------------------------------------------------------------------------
// StringSplit_Old_ReuseVector                                 74.90ns   13.35M
// StringSplit_Gen_ReuseVector                      112.29%    66.71ns   14.99M
// StringSplit_Gen                                  122.42%    61.18ns   16.34M
// StringSplit_Gen_Take                             134.49%    55.70ns   17.95M
// ----------------------------------------------------------------------------
// ByLine_Pipes                                               131.18ns    7.62M
// ============================================================================

int main(int argc, char *argv[]) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  initStringResplitterBenchmark();
  runBenchmarks();
  return 0;
}
