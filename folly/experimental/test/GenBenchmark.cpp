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

static vector<fbstring> testStrVector =
    seq(1, testSize.load())
  | eachTo<fbstring>()
  | as<vector>();

static vector<vector<int>> testVectorVector =
    seq(1, 100)
  | map([](int i) {
      return seq(1, i) | as<vector>();
    })
  | as<vector>();
static vector<fbstring> strings =
    from(testVector)
  | eachTo<fbstring>()
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

BENCHMARK(Member, iters) {
  int s = 0;
  while(iters--) {
    s += from(strings)
       | member(&fbstring::size)
       | sum;
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_RELATIVE(MapMember, iters) {
  int s = 0;
  while(iters--) {
    s += from(strings)
       | map([](const fbstring& x) { return x.size(); })
       | sum;
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
    auto fib = GENERATOR(int) {
      int a = 0;
      int b = 1;
      for (;;) {
        yield(a += b);
        yield(b += a);
      }
    };
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

BENCHMARK(Sample, iters) {
  size_t s = 0;
  while (iters--) {
    auto sampler = seq(1, 10 * 1000 * 1000) | sample(1000);
    s += (sampler | sum);
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

BENCHMARK(StringUnsplit_Old, iters) {
  size_t s = 0;
  while (iters--) {
    fbstring joined;
    join(',', testStrVector, joined);
    s += joined.size();
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_RELATIVE(StringUnsplit_Old_ReusedBuffer, iters) {
  size_t s = 0;
  fbstring joined;
  while (iters--) {
    joined.clear();
    join(',', testStrVector, joined);
    s += joined.size();
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_RELATIVE(StringUnsplit_Gen, iters) {
  size_t s = 0;
  StringPiece line(kLine);
  while (iters--) {
    fbstring joined = from(testStrVector) | unsplit(',');
    s += joined.size();
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_RELATIVE(StringUnsplit_Gen_ReusedBuffer, iters) {
  size_t s = 0;
  fbstring buffer;
  while (iters--) {
    buffer.clear();
    from(testStrVector) | unsplit(',', &buffer);
    s += buffer.size();
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_DRAW_LINE()

void StringUnsplit_Gen(size_t iters, size_t joinSize) {
  std::vector<fbstring> v;
  BENCHMARK_SUSPEND {
    FOR_EACH_RANGE(i, 0, joinSize) {
      v.push_back(to<fbstring>(rand()));
    }
  }
  size_t s = 0;
  fbstring buffer;
  while (iters--) {
    buffer.clear();
    from(v) | unsplit(',', &buffer);
    s += buffer.size();
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_PARAM(StringUnsplit_Gen, 1000)
BENCHMARK_RELATIVE_PARAM(StringUnsplit_Gen, 2000)
BENCHMARK_RELATIVE_PARAM(StringUnsplit_Gen, 4000)
BENCHMARK_RELATIVE_PARAM(StringUnsplit_Gen, 8000)

BENCHMARK_DRAW_LINE()

fbstring records
= seq<size_t>(1, 1000)
  | mapped([](size_t i) {
      return folly::to<fbstring>(i, ' ', i * i, ' ', i * i * i);
    })
  | unsplit('\n');

BENCHMARK(Records_EachToTuple, iters) {
  size_t s = 0;
  for (size_t i = 0; i < iters; i += 1000) {
    s += split(records, '\n')
       | eachToTuple<int, size_t, StringPiece>(' ')
       | get<1>()
       | sum;
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_RELATIVE(Records_VectorStringPieceReused, iters) {
  size_t s = 0;
  std::vector<StringPiece> fields;
  for (size_t i = 0; i < iters; i += 1000) {
    s += split(records, '\n')
       | mapped([&](StringPiece line) {
           fields.clear();
           folly::split(' ', line, fields);
           CHECK(fields.size() == 3);
           return std::make_tuple(
             folly::to<int>(fields[0]),
             folly::to<size_t>(fields[1]),
             StringPiece(fields[2]));
         })
       | get<1>()
       | sum;
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_RELATIVE(Records_VectorStringPiece, iters) {
  size_t s = 0;
  for (size_t i = 0; i < iters; i += 1000) {
    s += split(records, '\n')
       | mapped([](StringPiece line) {
           std::vector<StringPiece> fields;
           folly::split(' ', line, fields);
           CHECK(fields.size() == 3);
           return std::make_tuple(
             folly::to<int>(fields[0]),
             folly::to<size_t>(fields[1]),
             StringPiece(fields[2]));
         })
       | get<1>()
       | sum;
  }
  folly::doNotOptimizeAway(s);
}

BENCHMARK_RELATIVE(Records_VectorString, iters) {
  size_t s = 0;
  for (size_t i = 0; i < iters; i += 1000) {
    s += split(records, '\n')
       | mapped([](StringPiece line) {
           std::vector<std::string> fields;
           folly::split(' ', line, fields);
           CHECK(fields.size() == 3);
           return std::make_tuple(
             folly::to<int>(fields[0]),
             folly::to<size_t>(fields[1]),
             StringPiece(fields[2]));
         })
       | get<1>()
       | sum;
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

// ============================================================================
// folly/experimental/test/GenBenchmark.cpp        relative  time/iter  iters/s
// ============================================================================
// Sum_Basic_NoGen                                            374.39ns    2.67M
// Sum_Basic_Gen                                    101.05%   370.48ns    2.70M
// ----------------------------------------------------------------------------
// Sum_Vector_NoGen                                           198.84ns    5.03M
// Sum_Vector_Gen                                    98.14%   202.60ns    4.94M
// ----------------------------------------------------------------------------
// Member                                                       4.56us  219.11K
// MapMember                                        400.21%     1.14us  876.89K
// ----------------------------------------------------------------------------
// Count_Vector_NoGen                                          13.99us   71.47K
// Count_Vector_Gen                                 106.73%    13.11us   76.28K
// ----------------------------------------------------------------------------
// Fib_Sum_NoGen                                                4.27us  234.07K
// Fib_Sum_Gen                                       43.18%     9.90us  101.06K
// Fib_Sum_Gen_Static                                92.08%     4.64us  215.53K
// ----------------------------------------------------------------------------
// VirtualGen_0Virtual                                         12.07us   82.83K
// VirtualGen_1Virtual                               32.46%    37.19us   26.89K
// VirtualGen_2Virtual                               24.36%    49.55us   20.18K
// VirtualGen_3Virtual                               18.16%    66.49us   15.04K
// ----------------------------------------------------------------------------
// Concat_NoGen                                                 1.90us  527.40K
// Concat_Gen                                        86.73%     2.19us  457.39K
// ----------------------------------------------------------------------------
// Composed_NoGen                                             546.18ns    1.83M
// Composed_Gen                                     100.41%   543.93ns    1.84M
// Composed_GenRegular                              100.42%   543.92ns    1.84M
// ----------------------------------------------------------------------------
// Sample                                                     146.68ms     6.82
// ----------------------------------------------------------------------------
// StringResplitter_Big                                       124.80us    8.01K
// StringResplitter_Small                            15.11%   825.74us    1.21K
// ----------------------------------------------------------------------------
// StringSplit_Old                                            393.49ns    2.54M
// StringSplit_Gen_Vector                           121.47%   323.93ns    3.09M
// ----------------------------------------------------------------------------
// StringSplit_Old_ReuseVector                                 80.77ns   12.38M
// StringSplit_Gen_ReuseVector                      102.02%    79.17ns   12.63M
// StringSplit_Gen                                  123.78%    65.25ns   15.32M
// StringSplit_Gen_Take                             123.44%    65.43ns   15.28M
// ----------------------------------------------------------------------------
// StringUnsplit_Old                                           29.36us   34.06K
// StringUnsplit_Old_ReusedBuffer                   100.25%    29.29us   34.14K
// StringUnsplit_Gen                                103.38%    28.40us   35.21K
// StringUnsplit_Gen_ReusedBuffer                   109.85%    26.73us   37.41K
// ----------------------------------------------------------------------------
// StringUnsplit_Gen(1000)                                     32.30us   30.96K
// StringUnsplit_Gen(2000)                           49.75%    64.93us   15.40K
// StringUnsplit_Gen(4000)                           24.74%   130.60us    7.66K
// StringUnsplit_Gen(8000)                           12.31%   262.35us    3.81K
// ----------------------------------------------------------------------------
// Records_EachToTuple                                         75.03ns   13.33M
// Records_VectorStringPieceReused                   81.79%    91.74ns   10.90M
// Records_VectorStringPiece                         36.47%   205.77ns    4.86M
// Records_VectorString                              12.90%   581.70ns    1.72M
// ----------------------------------------------------------------------------
// ByLine_Pipes                                               121.68ns    8.22M
// ============================================================================

int main(int argc, char *argv[]) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  initStringResplitterBenchmark();
  runBenchmarks();
  return 0;
}
