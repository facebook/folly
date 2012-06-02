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

#include "folly/String.h"

#include <random>
#include <boost/algorithm/string.hpp>
#include <gtest/gtest.h>

#include "folly/Benchmark.h"

using namespace folly;
using namespace std;

TEST(StringPrintf, BasicTest) {
  EXPECT_EQ("abc", stringPrintf("%s", "abc"));
  EXPECT_EQ("abc", stringPrintf("%sbc", "a"));
  EXPECT_EQ("abc", stringPrintf("a%sc", "b"));
  EXPECT_EQ("abc", stringPrintf("ab%s", "c"));

  EXPECT_EQ("abc", stringPrintf("abc"));
}

TEST(StringPrintf, NumericFormats) {
  EXPECT_EQ("12", stringPrintf("%d", 12));
  EXPECT_EQ("5000000000", stringPrintf("%ld", 5000000000UL));
  EXPECT_EQ("5000000000", stringPrintf("%ld", 5000000000L));
  EXPECT_EQ("-5000000000", stringPrintf("%ld", -5000000000L));
  EXPECT_EQ("-1", stringPrintf("%d", 0xffffffff));
  EXPECT_EQ("-1", stringPrintf("%ld", 0xffffffffffffffff));
  EXPECT_EQ("-1", stringPrintf("%ld", 0xffffffffffffffffUL));

  EXPECT_EQ("7.7", stringPrintf("%1.1f", 7.7));
  EXPECT_EQ("7.7", stringPrintf("%1.1lf", 7.7));
  EXPECT_EQ("7.70000000000000018",
            stringPrintf("%.17f", 7.7));
  EXPECT_EQ("7.70000000000000018",
            stringPrintf("%.17lf", 7.7));
}

TEST(StringPrintf, Appending) {
  string s;
  stringAppendf(&s, "a%s", "b");
  stringAppendf(&s, "%c", 'c');
  EXPECT_EQ(s, "abc");
  stringAppendf(&s, " %d", 123);
  EXPECT_EQ(s, "abc 123");
}

TEST(StringPrintf, VariousSizes) {
  // Test a wide variety of output sizes
  for (int i = 0; i < 100; ++i) {
    string expected(i + 1, 'a');
    EXPECT_EQ("X" + expected + "X", stringPrintf("X%sX", expected.c_str()));
  }

  EXPECT_EQ("abc12345678910111213141516171819202122232425xyz",
            stringPrintf("abc%d%d%d%d%d%d%d%d%d%d%d%d%d%d"
                         "%d%d%d%d%d%d%d%d%d%d%dxyz",
                         1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                         17, 18, 19, 20, 21, 22, 23, 24, 25));
}

TEST(StringPrintf, oldStringPrintfTests) {
  EXPECT_EQ(string("a/b/c/d"),
            stringPrintf("%s/%s/%s/%s", "a", "b", "c", "d"));

  EXPECT_EQ(string("    5    10"),
            stringPrintf("%5d %5d", 5, 10));

  // check printing w/ a big buffer
  for (int size = (1 << 8); size <= (1 << 15); size <<= 1) {
    string a(size, 'z');
    string b = stringPrintf("%s", a.c_str());
    EXPECT_EQ(a.size(), b.size());
  }
}

TEST(StringPrintf, oldStringAppendf) {
  string s = "hello";
  stringAppendf(&s, "%s/%s/%s/%s", "a", "b", "c", "d");
  EXPECT_EQ(string("helloa/b/c/d"), s);
}

BENCHMARK(new_stringPrintfSmall, iters) {
  for (int64_t i = 0; i < iters; ++i) {
    int32_t x = int32_t(i);
    int32_t y = int32_t(i + 1);
    string s =
      stringPrintf("msg msg msg msg msg msg msg msg:  %d, %d, %s",
                   x, y, "hello");
  }
}

TEST(Escape, cEscape) {
  EXPECT_EQ("hello world", cEscape<std::string>("hello world"));
  EXPECT_EQ("hello \\\\world\\\" goodbye",
            cEscape<std::string>("hello \\world\" goodbye"));
  EXPECT_EQ("hello\\nworld", cEscape<std::string>("hello\nworld"));
  EXPECT_EQ("hello\\377\\376", cEscape<std::string>("hello\xff\xfe"));
}

TEST(Escape, cUnescape) {
  EXPECT_EQ("hello world", cUnescape<std::string>("hello world"));
  EXPECT_EQ("hello \\world\" goodbye",
            cUnescape<std::string>("hello \\\\world\\\" goodbye"));
  EXPECT_EQ("hello\nworld", cUnescape<std::string>("hello\\nworld"));
  EXPECT_EQ("hello\nworld", cUnescape<std::string>("hello\\012world"));
  EXPECT_EQ("hello\nworld", cUnescape<std::string>("hello\\x0aworld"));
  EXPECT_EQ("hello\xff\xfe", cUnescape<std::string>("hello\\377\\376"));
  EXPECT_EQ("hello\xff\xfe", cUnescape<std::string>("hello\\xff\\xfe"));

  EXPECT_THROW({cUnescape<std::string>("hello\\");},
               std::invalid_argument);
  EXPECT_THROW({cUnescape<std::string>("hello\\x");},
               std::invalid_argument);
  EXPECT_THROW({cUnescape<std::string>("hello\\q");},
               std::invalid_argument);
}

namespace {
fbstring bmString;
fbstring bmEscapedString;
fbstring escapedString;
fbstring unescapedString;
const size_t kBmStringLength = 64 << 10;
const uint32_t kPrintablePercentage = 90;

void initBenchmark() {
  bmString.reserve(kBmStringLength);

  std::mt19937 rnd;
  std::uniform_int_distribution<uint32_t> printable(32, 126);
  std::uniform_int_distribution<uint32_t> nonPrintable(0, 160);
  std::uniform_int_distribution<uint32_t> percentage(0, 99);

  for (size_t i = 0; i < kBmStringLength; ++i) {
    unsigned char c;
    if (percentage(rnd) < kPrintablePercentage) {
      c = printable(rnd);
    } else {
      c = nonPrintable(rnd);
      // Generate characters in both non-printable ranges:
      // 0..31 and 127..255
      if (c >= 32) {
        c += (126 - 32) + 1;
      }
    }
    bmString.push_back(c);
  }

  bmEscapedString = cEscape<fbstring>(bmString);
}

BENCHMARK(BM_cEscape, iters) {
  while (iters--) {
    escapedString = cEscape<fbstring>(bmString);
    doNotOptimizeAway(escapedString.size());
  }
}

BENCHMARK(BM_cUnescape, iters) {
  while (iters--) {
    unescapedString = cUnescape<fbstring>(bmEscapedString);
    doNotOptimizeAway(unescapedString.size());
  }
}

}  // namespace

namespace {

double pow2(int exponent) {
  return double(int64_t(1) << exponent);
}

}  // namespace

TEST(PrettyPrint, Basic) {
  // check time printing
  EXPECT_EQ(string("8.53e+07 s "), prettyPrint(85.3e6, PRETTY_TIME));
  EXPECT_EQ(string("85.3 s "), prettyPrint(85.3, PRETTY_TIME));
  EXPECT_EQ(string("85.3 ms"), prettyPrint(85.3e-3, PRETTY_TIME));
  EXPECT_EQ(string("85.3 us"), prettyPrint(85.3e-6, PRETTY_TIME));
  EXPECT_EQ(string("85.3 ns"), prettyPrint(85.3e-9, PRETTY_TIME));
  EXPECT_EQ(string("85.3 ps"), prettyPrint(85.3e-12, PRETTY_TIME));
  EXPECT_EQ(string("8.53e-14 s "), prettyPrint(85.3e-15, PRETTY_TIME));

  EXPECT_EQ(string("0 s "), prettyPrint(0, PRETTY_TIME));
  EXPECT_EQ(string("1 s "), prettyPrint(1.0, PRETTY_TIME));
  EXPECT_EQ(string("1 ms"), prettyPrint(1.0e-3, PRETTY_TIME));
  EXPECT_EQ(string("1 us"), prettyPrint(1.0e-6, PRETTY_TIME));
  EXPECT_EQ(string("1 ns"), prettyPrint(1.0e-9, PRETTY_TIME));
  EXPECT_EQ(string("1 ps"), prettyPrint(1.0e-12, PRETTY_TIME));

  // check bytes printing
  EXPECT_EQ(string("853 B "), prettyPrint(853., PRETTY_BYTES));
  EXPECT_EQ(string("833 kB"), prettyPrint(853.e3, PRETTY_BYTES));
  EXPECT_EQ(string("813.5 MB"), prettyPrint(853.e6, PRETTY_BYTES));
  EXPECT_EQ(string("7.944 GB"), prettyPrint(8.53e9, PRETTY_BYTES));
  EXPECT_EQ(string("794.4 GB"), prettyPrint(853.e9, PRETTY_BYTES));
  EXPECT_EQ(string("775.8 TB"), prettyPrint(853.e12, PRETTY_BYTES));

  EXPECT_EQ(string("0 B "), prettyPrint(0, PRETTY_BYTES));
  EXPECT_EQ(string("1 B "), prettyPrint(pow2(0), PRETTY_BYTES));
  EXPECT_EQ(string("1 kB"), prettyPrint(pow2(10), PRETTY_BYTES));
  EXPECT_EQ(string("1 MB"), prettyPrint(pow2(20), PRETTY_BYTES));
  EXPECT_EQ(string("1 GB"), prettyPrint(pow2(30), PRETTY_BYTES));
  EXPECT_EQ(string("1 TB"), prettyPrint(pow2(40), PRETTY_BYTES));

  // check bytes metric printing
  EXPECT_EQ(string("853 B "), prettyPrint(853., PRETTY_BYTES_METRIC));
  EXPECT_EQ(string("853 kB"), prettyPrint(853.e3, PRETTY_BYTES_METRIC));
  EXPECT_EQ(string("853 MB"), prettyPrint(853.e6, PRETTY_BYTES_METRIC));
  EXPECT_EQ(string("8.53 GB"), prettyPrint(8.53e9, PRETTY_BYTES_METRIC));
  EXPECT_EQ(string("853 GB"), prettyPrint(853.e9, PRETTY_BYTES_METRIC));
  EXPECT_EQ(string("853 TB"), prettyPrint(853.e12, PRETTY_BYTES_METRIC));

  EXPECT_EQ(string("0 B "), prettyPrint(0, PRETTY_BYTES_METRIC));
  EXPECT_EQ(string("1 B "), prettyPrint(1.0, PRETTY_BYTES_METRIC));
  EXPECT_EQ(string("1 kB"), prettyPrint(1.0e+3, PRETTY_BYTES_METRIC));
  EXPECT_EQ(string("1 MB"), prettyPrint(1.0e+6, PRETTY_BYTES_METRIC));

  EXPECT_EQ(string("1 GB"), prettyPrint(1.0e+9, PRETTY_BYTES_METRIC));
  EXPECT_EQ(string("1 TB"), prettyPrint(1.0e+12, PRETTY_BYTES_METRIC));

  // check metric-units (powers of 1000) printing
  EXPECT_EQ(string("853  "), prettyPrint(853., PRETTY_UNITS_METRIC));
  EXPECT_EQ(string("853 k"), prettyPrint(853.e3, PRETTY_UNITS_METRIC));
  EXPECT_EQ(string("853 M"), prettyPrint(853.e6, PRETTY_UNITS_METRIC));
  EXPECT_EQ(string("8.53 bil"), prettyPrint(8.53e9, PRETTY_UNITS_METRIC));
  EXPECT_EQ(string("853 bil"), prettyPrint(853.e9, PRETTY_UNITS_METRIC));
  EXPECT_EQ(string("853 tril"), prettyPrint(853.e12, PRETTY_UNITS_METRIC));

  // check binary-units (powers of 1024) printing
  EXPECT_EQ(string("0  "), prettyPrint(0, PRETTY_UNITS_BINARY));
  EXPECT_EQ(string("1  "), prettyPrint(pow2(0), PRETTY_UNITS_BINARY));
  EXPECT_EQ(string("1 k"), prettyPrint(pow2(10), PRETTY_UNITS_BINARY));
  EXPECT_EQ(string("1 M"), prettyPrint(pow2(20), PRETTY_UNITS_BINARY));
  EXPECT_EQ(string("1 G"), prettyPrint(pow2(30), PRETTY_UNITS_BINARY));
  EXPECT_EQ(string("1 T"), prettyPrint(pow2(40), PRETTY_UNITS_BINARY));

  EXPECT_EQ(string("1023  "),
      prettyPrint(pow2(10) - 1, PRETTY_UNITS_BINARY));
  EXPECT_EQ(string("1024 k"),
      prettyPrint(pow2(20) - 1, PRETTY_UNITS_BINARY));
  EXPECT_EQ(string("1024 M"),
      prettyPrint(pow2(30) - 1, PRETTY_UNITS_BINARY));
  EXPECT_EQ(string("1024 G"),
      prettyPrint(pow2(40) - 1, PRETTY_UNITS_BINARY));

  // check that negative values work
  EXPECT_EQ(string("-85.3 s "), prettyPrint(-85.3, PRETTY_TIME));
  EXPECT_EQ(string("-85.3 ms"), prettyPrint(-85.3e-3, PRETTY_TIME));
  EXPECT_EQ(string("-85.3 us"), prettyPrint(-85.3e-6, PRETTY_TIME));
  EXPECT_EQ(string("-85.3 ns"), prettyPrint(-85.3e-9, PRETTY_TIME));
}

TEST(PrettyPrint, HexDump) {
  std::string a("abc\x00\x02\xa0", 6);  // embedded NUL
  EXPECT_EQ(
    "00000000  61 62 63 00 02 a0                                 "
    "|abc...          |\n",
    hexDump(a.data(), a.size()));

  a = "abcdefghijklmnopqrstuvwxyz";
  EXPECT_EQ(
    "00000000  61 62 63 64 65 66 67 68  69 6a 6b 6c 6d 6e 6f 70  "
    "|abcdefghijklmnop|\n"
    "00000010  71 72 73 74 75 76 77 78  79 7a                    "
    "|qrstuvwxyz      |\n",
    hexDump(a.data(), a.size()));
}

TEST(System, errnoStr) {
  errno = EACCES;
  EXPECT_EQ(EACCES, errno);
  EXPECT_EQ(EACCES, errno);  // twice to make sure EXPECT_EQ doesn't change it

  fbstring expected = strerror(ENOENT);

  errno = EACCES;
  EXPECT_EQ(expected, errnoStr(ENOENT));
  // Ensure that errno isn't changed
  EXPECT_EQ(EACCES, errno);

  // Per POSIX, all errno values are positive, so -1 is invalid
  errnoStr(-1);

  // Ensure that errno isn't changed
  EXPECT_EQ(EACCES, errno);
}

namespace folly_test {
struct ThisIsAVeryLongStructureName {
};
}  // namespace folly_test

TEST(System, demangle) {
  EXPECT_EQ("folly_test::ThisIsAVeryLongStructureName",
            demangle(typeid(folly_test::ThisIsAVeryLongStructureName)));
}

namespace {

template<template<class,class> class VectorType>
void splitTest() {
  VectorType<string,std::allocator<string> > parts;

  folly::split(',', "a,b,c", parts);
  EXPECT_EQ(parts.size(), 3);
  EXPECT_EQ(parts[0], "a");
  EXPECT_EQ(parts[1], "b");
  EXPECT_EQ(parts[2], "c");
  parts.clear();

  folly::split(',', string("a,b,c"), parts);
  EXPECT_EQ(parts.size(), 3);
  EXPECT_EQ(parts[0], "a");
  EXPECT_EQ(parts[1], "b");
  EXPECT_EQ(parts[2], "c");
  parts.clear();

  folly::split(',', "a,,c", parts);
  EXPECT_EQ(parts.size(), 3);
  EXPECT_EQ(parts[0], "a");
  EXPECT_EQ(parts[1], "");
  EXPECT_EQ(parts[2], "c");
  parts.clear();

  folly::split(',', string("a,,c"), parts);
  EXPECT_EQ(parts.size(), 3);
  EXPECT_EQ(parts[0], "a");
  EXPECT_EQ(parts[1], "");
  EXPECT_EQ(parts[2], "c");
  parts.clear();

  folly::split(',', "a,,c", parts, true);
  EXPECT_EQ(parts.size(), 2);
  EXPECT_EQ(parts[0], "a");
  EXPECT_EQ(parts[1], "c");
  parts.clear();

  folly::split(',', string("a,,c"), parts, true);
  EXPECT_EQ(parts.size(), 2);
  EXPECT_EQ(parts[0], "a");
  EXPECT_EQ(parts[1], "c");
  parts.clear();

  folly::split(',', string(",,a,,c,,,"), parts, true);
  EXPECT_EQ(parts.size(), 2);
  EXPECT_EQ(parts[0], "a");
  EXPECT_EQ(parts[1], "c");
  parts.clear();

  // test multiple split w/o clear
  folly::split(',', ",,a,,c,,,", parts, true);
  EXPECT_EQ(parts.size(), 2);
  EXPECT_EQ(parts[0], "a");
  EXPECT_EQ(parts[1], "c");
  folly::split(',', ",,a,,c,,,", parts, true);
  EXPECT_EQ(parts.size(), 4);
  EXPECT_EQ(parts[2], "a");
  EXPECT_EQ(parts[3], "c");
  parts.clear();

  // test splits that with multi-line delimiter
  folly::split("ab", "dabcabkdbkab", parts, true);
  EXPECT_EQ(parts.size(), 3);
  EXPECT_EQ(parts[0], "d");
  EXPECT_EQ(parts[1], "c");
  EXPECT_EQ(parts[2], "kdbk");
  parts.clear();

  string orig = "ab2342asdfv~~!";
  folly::split("", orig, parts, true);
  EXPECT_EQ(parts.size(), 1);
  EXPECT_EQ(parts[0], orig);
  parts.clear();

  folly::split("452x;o38asfsajsdlfdf.j", "asfds", parts, true);
  EXPECT_EQ(parts.size(), 1);
  EXPECT_EQ(parts[0], "asfds");
  parts.clear();

  folly::split("a", "", parts, true);
  EXPECT_EQ(parts.size(), 0);
  parts.clear();

  folly::split("a", "", parts);
  EXPECT_EQ(parts.size(), 1);
  EXPECT_EQ(parts[0], "");
  parts.clear();

  folly::split("a", "abcdefg", parts, true);
  EXPECT_EQ(parts.size(), 1);
  EXPECT_EQ(parts[0], "bcdefg");
  parts.clear();

  orig = "All, , your bases, are , , belong to us";
  folly::split(", ", orig, parts, true);
  EXPECT_EQ(parts.size(), 4);
  EXPECT_EQ(parts[0], "All");
  EXPECT_EQ(parts[1], "your bases");
  EXPECT_EQ(parts[2], "are ");
  EXPECT_EQ(parts[3], "belong to us");
  parts.clear();
  folly::split(", ", orig, parts);
  EXPECT_EQ(parts.size(), 6);
  EXPECT_EQ(parts[0], "All");
  EXPECT_EQ(parts[1], "");
  EXPECT_EQ(parts[2], "your bases");
  EXPECT_EQ(parts[3], "are ");
  EXPECT_EQ(parts[4], "");
  EXPECT_EQ(parts[5], "belong to us");
  parts.clear();

  orig = ", Facebook, rul,es!, ";
  folly::split(", ", orig, parts, true);
  EXPECT_EQ(parts.size(), 2);
  EXPECT_EQ(parts[0], "Facebook");
  EXPECT_EQ(parts[1], "rul,es!");
  parts.clear();
  folly::split(", ", orig, parts);
  EXPECT_EQ(parts.size(), 4);
  EXPECT_EQ(parts[0], "");
  EXPECT_EQ(parts[1], "Facebook");
  EXPECT_EQ(parts[2], "rul,es!");
  EXPECT_EQ(parts[3], "");
}

template<template<class,class> class VectorType>
void piecesTest() {
  VectorType<StringPiece,std::allocator<StringPiece> > pieces;
  VectorType<StringPiece,std::allocator<StringPiece> > pieces2;

  folly::split(',', "a,b,c", pieces);
  EXPECT_EQ(pieces.size(), 3);
  EXPECT_EQ(pieces[0], "a");
  EXPECT_EQ(pieces[1], "b");
  EXPECT_EQ(pieces[2], "c");

  pieces.clear();

  folly::split(',', "a,,c", pieces);
  EXPECT_EQ(pieces.size(), 3);
  EXPECT_EQ(pieces[0], "a");
  EXPECT_EQ(pieces[1], "");
  EXPECT_EQ(pieces[2], "c");
  pieces.clear();

  folly::split(',', "a,,c", pieces, true);
  EXPECT_EQ(pieces.size(), 2);
  EXPECT_EQ(pieces[0], "a");
  EXPECT_EQ(pieces[1], "c");
  pieces.clear();

  folly::split(',', ",,a,,c,,,", pieces, true);
  EXPECT_EQ(pieces.size(), 2);
  EXPECT_EQ(pieces[0], "a");
  EXPECT_EQ(pieces[1], "c");
  pieces.clear();

  // test multiple split w/o clear
  folly::split(',', ",,a,,c,,,", pieces, true);
  EXPECT_EQ(pieces.size(), 2);
  EXPECT_EQ(pieces[0], "a");
  EXPECT_EQ(pieces[1], "c");
  folly::split(',', ",,a,,c,,,", pieces, true);
  EXPECT_EQ(pieces.size(), 4);
  EXPECT_EQ(pieces[2], "a");
  EXPECT_EQ(pieces[3], "c");
  pieces.clear();

  // test multiple split rounds
  folly::split(",", "a_b,c_d", pieces);
  EXPECT_EQ(pieces.size(), 2);
  EXPECT_EQ(pieces[0], "a_b");
  EXPECT_EQ(pieces[1], "c_d");
  folly::split("_", pieces[0], pieces2);
  EXPECT_EQ(pieces2.size(), 2);
  EXPECT_EQ(pieces2[0], "a");
  EXPECT_EQ(pieces2[1], "b");
  pieces2.clear();
  folly::split("_", pieces[1], pieces2);
  EXPECT_EQ(pieces2.size(), 2);
  EXPECT_EQ(pieces2[0], "c");
  EXPECT_EQ(pieces2[1], "d");
  pieces.clear();
  pieces2.clear();

  // test splits that with multi-line delimiter
  folly::split("ab", "dabcabkdbkab", pieces, true);
  EXPECT_EQ(pieces.size(), 3);
  EXPECT_EQ(pieces[0], "d");
  EXPECT_EQ(pieces[1], "c");
  EXPECT_EQ(pieces[2], "kdbk");
  pieces.clear();

  string orig = "ab2342asdfv~~!";
  folly::split("", orig.c_str(), pieces, true);
  EXPECT_EQ(pieces.size(), 1);
  EXPECT_EQ(pieces[0], orig);
  pieces.clear();

  folly::split("452x;o38asfsajsdlfdf.j", "asfds", pieces, true);
  EXPECT_EQ(pieces.size(), 1);
  EXPECT_EQ(pieces[0], "asfds");
  pieces.clear();

  folly::split("a", "", pieces, true);
  EXPECT_EQ(pieces.size(), 0);
  pieces.clear();

  folly::split("a", "", pieces);
  EXPECT_EQ(pieces.size(), 1);
  EXPECT_EQ(pieces[0], "");
  pieces.clear();

  folly::split("a", "abcdefg", pieces, true);
  EXPECT_EQ(pieces.size(), 1);
  EXPECT_EQ(pieces[0], "bcdefg");
  pieces.clear();

  orig = "All, , your bases, are , , belong to us";
  folly::split(", ", orig, pieces, true);
  EXPECT_EQ(pieces.size(), 4);
  EXPECT_EQ(pieces[0], "All");
  EXPECT_EQ(pieces[1], "your bases");
  EXPECT_EQ(pieces[2], "are ");
  EXPECT_EQ(pieces[3], "belong to us");
  pieces.clear();
  folly::split(", ", orig, pieces);
  EXPECT_EQ(pieces.size(), 6);
  EXPECT_EQ(pieces[0], "All");
  EXPECT_EQ(pieces[1], "");
  EXPECT_EQ(pieces[2], "your bases");
  EXPECT_EQ(pieces[3], "are ");
  EXPECT_EQ(pieces[4], "");
  EXPECT_EQ(pieces[5], "belong to us");
  pieces.clear();

  orig = ", Facebook, rul,es!, ";
  folly::split(", ", orig, pieces, true);
  EXPECT_EQ(pieces.size(), 2);
  EXPECT_EQ(pieces[0], "Facebook");
  EXPECT_EQ(pieces[1], "rul,es!");
  pieces.clear();
  folly::split(", ", orig, pieces);
  EXPECT_EQ(pieces.size(), 4);
  EXPECT_EQ(pieces[0], "");
  EXPECT_EQ(pieces[1], "Facebook");
  EXPECT_EQ(pieces[2], "rul,es!");
  EXPECT_EQ(pieces[3], "");
  pieces.clear();

  const char* str = "a,b";
  folly::split(',', StringPiece(str), pieces);
  EXPECT_EQ(pieces.size(), 2);
  EXPECT_EQ(pieces[0], "a");
  EXPECT_EQ(pieces[1], "b");
  EXPECT_EQ(pieces[0].start(), str);
  EXPECT_EQ(pieces[1].start(), str + 2);

  std::set<StringPiece> unique;
  folly::splitTo<StringPiece>(":", "asd:bsd:asd:asd:bsd:csd::asd",
    std::inserter(unique, unique.begin()), true);
  EXPECT_EQ(unique.size(), 3);
  if (unique.size() == 3) {
    EXPECT_EQ(*unique.begin(), "asd");
    EXPECT_EQ(*--unique.end(), "csd");
  }

  VectorType<fbstring,std::allocator<fbstring> > blah;
  folly::split('-', "a-b-c-d-f-e", blah);
  EXPECT_EQ(blah.size(), 6);
}

}

TEST(Split, split_vector) {
  splitTest<std::vector>();
}
TEST(Split, split_fbvector) {
  splitTest<folly::fbvector>();
}
TEST(Split, pieces_vector) {
  piecesTest<std::vector>();
}
TEST(Split, pieces_fbvector) {
  piecesTest<folly::fbvector>();
}

//////////////////////////////////////////////////////////////////////

BENCHMARK(splitOnSingleChar, iters) {
  const std::string line = "one:two:three:four";
  for (int i = 0; i < iters << 4; ++i) {
    std::vector<StringPiece> pieces;
    folly::split(':', line, pieces);
  }
}

BENCHMARK(splitStr, iters) {
  const std::string line = "one-*-two-*-three-*-four";
  for (int i = 0; i < iters << 4; ++i) {
    std::vector<StringPiece> pieces;
    folly::split("-*-", line, pieces);
  }
}

BENCHMARK(boost_splitOnSingleChar, iters) {
  std::string line = "one:two:three:four";
  for (int i = 0; i < iters << 4; ++i) {
    std::vector<boost::iterator_range<std::string::iterator>> pieces;
    boost::split(pieces, line, [] (char c) { return c == ':'; });
  }
}

int main(int argc, char *argv[]) {
  testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);
  auto ret = RUN_ALL_TESTS();
  if (!ret) {
    initBenchmark();
    if (FLAGS_benchmark) {
      folly::runBenchmarks();
    }
  }
  return ret;
}

