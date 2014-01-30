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

#include "folly/Range.h"
#include "folly/Benchmark.h"
#include "folly/Foreach.h"
#include <algorithm>
#include <iostream>
#include <random>
#include <string>

namespace folly { namespace detail {
// declaration of functions in Range.cpp
size_t qfind_first_byte_of_memchr(const StringPiece& haystack,
                                  const StringPiece& needles);

size_t qfind_first_byte_of_byteset(const StringPiece& haystack,
                                   const StringPiece& needles);

size_t qfind_first_byte_of_nosse(const StringPiece& haystack,
                                 const StringPiece& needles);
}}

using namespace folly;
using namespace std;

namespace {

std::string str;
constexpr int kVstrSize = 16;
std::vector<std::string> vstr;
std::vector<StringPiece> vstrp;

void initStr(int len) {
  cout << "string length " << len << ':' << endl;
  str.clear();
  str.reserve(len + 1);
  str.append(len, 'a');
  str.append(1, 'b');

  // create 16 copies of str, each with a different 16byte alignment.
  // Useful because some implementations of find_first_of have different
  // behaviors based on byte alignment.
  for (int i = 0; i < kVstrSize; ++i) {
    string s(i, '$');
    s += str;
    if (i % 2) {
      // some find_first_of implementations have special (page-safe) logic
      // for handling the end of a string.  Flex that logic only sometimes.
      s += string(32, '$');
    }
    vstr.push_back(s);
    StringPiece sp(vstr.back());
    sp.advance(i);
    vstrp.push_back(sp);
  }
}

std::mt19937 rnd;
string ffoTestString;
const size_t ffoDelimSize = 128;
vector<string> ffoDelim;

string generateString(int len) {
  std::uniform_int_distribution<uint32_t> validChar(1, 255);  // no null-char
  string ret;
  while (len--) {
    ret.push_back(validChar(rnd));
  }
  return ret;
}

void initDelims(int len) {
  ffoDelim.clear();

  string s(len - 1, '\0');  // find_first_of won't finish until last char
  s.push_back('a');
  ffoTestString = s;

  for (int i = 0; i < ffoDelimSize; ++i) {
    // most delimiter sets are pretty small, but occasionally there could
    // be a big one.
    auto n = rnd() % 8 + 1;
    if (n == 8) {
      n = 32;
    }
    auto s = generateString(n);
    if (rnd() % 2) {
      // ~half of tests will find a hit
      s[rnd() % s.size()] = 'a';  // yes, this could mean 'a' is a duplicate
    }
    ffoDelim.push_back(s);
  }
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

// it's useful to compare our custom implementations vs. the standard library
inline size_t qfind_first_byte_of_std(const StringPiece& haystack,
                                      const StringPiece& needles) {
  return qfind_first_of(haystack, needles, asciiCaseSensitive);
}

template <class Func>
void findFirstOfRange(StringPiece needles, Func func, size_t n) {
  FOR_EACH_RANGE (i, 0, n) {
    const StringPiece& haystack = vstr[i % kVstrSize];
    doNotOptimizeAway(func(haystack, needles));
    char x = haystack[0];
    doNotOptimizeAway(&x);
  }
}

const string delims2 = "bc";

BENCHMARK(FindFirstOf2NeedlesBase, n) {
  findFirstOfRange(delims2, detail::qfind_first_byte_of, n);
}

BENCHMARK_RELATIVE(FindFirstOf2NeedlesNoSSE, n) {
  findFirstOfRange(delims2, detail::qfind_first_byte_of_nosse, n);
}

BENCHMARK_RELATIVE(FindFirstOf2NeedlesStd, n) {
  findFirstOfRange(delims2, qfind_first_byte_of_std, n);
}

BENCHMARK_RELATIVE(FindFirstOf2NeedlesMemchr, n) {
  findFirstOfRange(delims2, detail::qfind_first_byte_of_memchr, n);
}

BENCHMARK_RELATIVE(FindFirstOf2NeedlesByteSet, n) {
  findFirstOfRange(delims2, detail::qfind_first_byte_of_byteset, n);
}

BENCHMARK_DRAW_LINE();

const string delims4 = "bcde";

BENCHMARK(FindFirstOf4NeedlesBase, n) {
  findFirstOfRange(delims4, detail::qfind_first_byte_of, n);
}

BENCHMARK_RELATIVE(FindFirstOf4NeedlesNoSSE, n) {
  findFirstOfRange(delims4, detail::qfind_first_byte_of_nosse, n);
}

BENCHMARK_RELATIVE(FindFirstOf4NeedlesStd, n) {
  findFirstOfRange(delims4, qfind_first_byte_of_std, n);
}

BENCHMARK_RELATIVE(FindFirstOf4NeedlesMemchr, n) {
  findFirstOfRange(delims4, detail::qfind_first_byte_of_memchr, n);
}

BENCHMARK_RELATIVE(FindFirstOf4NeedlesByteSet, n) {
  findFirstOfRange(delims4, detail::qfind_first_byte_of_byteset, n);
}

BENCHMARK_DRAW_LINE();

const string delims8 = "0123456b";

BENCHMARK(FindFirstOf8NeedlesBase, n) {
  findFirstOfRange(delims8, detail::qfind_first_byte_of, n);
}

BENCHMARK_RELATIVE(FindFirstOf8NeedlesNoSSE, n) {
  findFirstOfRange(delims8, detail::qfind_first_byte_of_nosse, n);
}

BENCHMARK_RELATIVE(FindFirstOf8NeedlesStd, n) {
  findFirstOfRange(delims8, qfind_first_byte_of_std, n);
}

BENCHMARK_RELATIVE(FindFirstOf8NeedlesMemchr, n) {
  findFirstOfRange(delims8, detail::qfind_first_byte_of_memchr, n);
}

BENCHMARK_RELATIVE(FindFirstOf8NeedlesByteSet, n) {
  findFirstOfRange(delims8, detail::qfind_first_byte_of_byteset, n);
}

BENCHMARK_DRAW_LINE();

const string delims16 = "0123456789bcdefg";

BENCHMARK(FindFirstOf16NeedlesBase, n) {
  findFirstOfRange(delims16, detail::qfind_first_byte_of, n);
}

BENCHMARK_RELATIVE(FindFirstOf16NeedlesNoSSE, n) {
  findFirstOfRange(delims16, detail::qfind_first_byte_of_nosse, n);
}

BENCHMARK_RELATIVE(FindFirstOf16NeedlesStd, n) {
  findFirstOfRange(delims16, qfind_first_byte_of_std, n);
}

BENCHMARK_RELATIVE(FindFirstOf16NeedlesMemchr, n) {
  findFirstOfRange(delims16, detail::qfind_first_byte_of_memchr, n);
}

BENCHMARK_RELATIVE(FindFirstOf16NeedlesByteSet, n) {
  findFirstOfRange(delims16, detail::qfind_first_byte_of_byteset, n);
}

BENCHMARK_DRAW_LINE();

const string delims32 = "!bcdefghijklmnopqrstuvwxyz_012345";

BENCHMARK(FindFirstOf32NeedlesBase, n) {
  findFirstOfRange(delims32, detail::qfind_first_byte_of, n);
}

BENCHMARK_RELATIVE(FindFirstOf32NeedlesNoSSE, n) {
  findFirstOfRange(delims32, detail::qfind_first_byte_of_nosse, n);
}

BENCHMARK_RELATIVE(FindFirstOf32NeedlesStd, n) {
  findFirstOfRange(delims32, qfind_first_byte_of_std, n);
}

BENCHMARK_RELATIVE(FindFirstOf32NeedlesMemchr, n) {
  findFirstOfRange(delims32, detail::qfind_first_byte_of_memchr, n);
}

BENCHMARK_RELATIVE(FindFirstOf32NeedlesByteSet, n) {
  findFirstOfRange(delims32, detail::qfind_first_byte_of_byteset, n);
}

BENCHMARK_DRAW_LINE();

const string delims64 = "!bcdefghijklmnopqrstuvwxyz_"
                        "ABCDEFGHIJKLMNOPQRSTUVWXYZ-0123456789$";

BENCHMARK(FindFirstOf64NeedlesBase, n) {
  findFirstOfRange(delims64, detail::qfind_first_byte_of, n);
}

BENCHMARK_RELATIVE(FindFirstOf64NeedlesNoSSE, n) {
  findFirstOfRange(delims64, detail::qfind_first_byte_of_nosse, n);
}

BENCHMARK_RELATIVE(FindFirstOf64NeedlesStd, n) {
  findFirstOfRange(delims64, qfind_first_byte_of_std, n);
}

BENCHMARK_RELATIVE(FindFirstOf64NeedlesMemchr, n) {
  findFirstOfRange(delims64, detail::qfind_first_byte_of_memchr, n);
}

BENCHMARK_RELATIVE(FindFirstOf64NeedlesByteSet, n) {
  findFirstOfRange(delims64, detail::qfind_first_byte_of_byteset, n);
}

BENCHMARK_DRAW_LINE();

template <class Func>
void findFirstOfRandom(Func func, size_t iters) {
  for (int i = 0; i < iters; ++i) {
    auto test = i % ffoDelim.size();
    auto p = func(ffoTestString, ffoDelim[test]);
    doNotOptimizeAway(p);
  }
}

BENCHMARK(FindFirstOfRandomBase, n) {
  findFirstOfRandom(detail::qfind_first_byte_of, n);
}

BENCHMARK_RELATIVE(FindFirstOfRandomNoSSE, n) {
  findFirstOfRandom(detail::qfind_first_byte_of_nosse, n);
}

BENCHMARK_RELATIVE(FindFirstOfRandomStd, n) {
  findFirstOfRandom(qfind_first_byte_of_std, n);
}

BENCHMARK_RELATIVE(FindFirstOfRandomMemchr, n) {
  findFirstOfRandom(detail::qfind_first_byte_of_memchr, n);
}

BENCHMARK_RELATIVE(FindFirstOfRandomByteSet, n) {
  findFirstOfRandom(detail::qfind_first_byte_of_byteset, n);
}

BENCHMARK_DRAW_LINE();

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

BENCHMARK_DRAW_LINE();

int main(int argc, char** argv) {
  google::ParseCommandLineFlags(&argc, &argv, true);

  for (int len : {1, 8, 10, 16, 32, 64, 128, 256, 10*1024, 1024*1024}) {
    initStr(len);
    initDelims(len);
    runBenchmarks();
  }
  return 0;
}
