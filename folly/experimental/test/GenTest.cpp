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

#include <glog/logging.h>
#include <gtest/gtest.h>
#include <iostream>
#include <set>
#include <vector>
#include "folly/experimental/Gen.h"
#include "folly/experimental/StringGen.h"
#include "folly/experimental/FileGen.h"
#include "folly/experimental/TestUtil.h"
#include "folly/FBVector.h"
#include "folly/Format.h"
#include "folly/dynamic.h"

using namespace folly::gen;
using namespace folly;
using std::ostream;
using std::pair;
using std::set;
using std::unique_ptr;
using std::vector;
using std::string;
using std::tuple;
using std::make_tuple;
//using std::unordered_map;

#define EXPECT_SAME(A, B) \
  static_assert(std::is_same<A, B>::value, "Mismatched: " #A ", " #B)
EXPECT_SAME(int&&, typename ArgumentReference<int>::type);
EXPECT_SAME(int&, typename ArgumentReference<int&>::type);
EXPECT_SAME(const int&, typename ArgumentReference<const int&>::type);
EXPECT_SAME(const int&, typename ArgumentReference<const int>::type);

template<typename T>
ostream& operator<<(ostream& os, const set<T>& values) {
  return os << from(values);
}

template<typename T>
ostream& operator<<(ostream& os, const vector<T>& values) {
  os << "[";
  for (auto& value : values) {
    if (&value != &values.front()) {
      os << " ";
    }
    os << value;
  }
  return os << "]";
}

auto square = [](int x) { return x * x; };
auto add = [](int a, int b) { return a + b; };
auto multiply = [](int a, int b) { return a * b; };

auto product = foldl(1, multiply);

template<typename A, typename B>
ostream& operator<<(ostream& os, const pair<A, B>& pair) {
  return os << "(" << pair.first << ", " << pair.second << ")";
}

TEST(Gen, Count) {
  auto gen = seq(1, 10);
  EXPECT_EQ(10, gen | count);
  EXPECT_EQ(5, gen | take(5) | count);
}

TEST(Gen, Sum) {
  auto gen = seq(1, 10);
  EXPECT_EQ((1 + 10) * 10 / 2, gen | sum);
  EXPECT_EQ((1 + 5) * 5 / 2, gen | take(5) | sum);
}

TEST(Gen, Foreach) {
  auto gen = seq(1, 4);
  int accum = 0;
  gen | [&](int x) { accum += x; };
  EXPECT_EQ(10, accum);
  int accum2 = 0;
  gen | take(3) | [&](int x) { accum2 += x; };
  EXPECT_EQ(6, accum2);
}

TEST(Gen, Map) {
  auto expected = vector<int>{4, 9, 16};
  auto gen = from({2, 3, 4}) | map(square);
  EXPECT_EQ((vector<int>{4, 9, 16}), gen | as<vector>());
  EXPECT_EQ((vector<int>{4, 9}), gen | take(2) | as<vector>());
}

TEST(Gen, Seq) {
  // cover the fenceposts of the loop unrolling
  for (int n = 1; n < 100; ++n) {
    EXPECT_EQ(n, seq(1, n) | count);
    EXPECT_EQ(n + 1, seq(1) | take(n + 1) | count);
  }
}

TEST(Gen, Range) {
  // cover the fenceposts of the loop unrolling
  for (int n = 1; n < 100; ++n) {
    EXPECT_EQ(range(0, n) | count, n);
  }
}

TEST(Gen, FromIterators) {
  vector<int> source {2, 3, 5, 7, 11};
  auto gen = from(makeRange(source.begin() + 1, source.end() - 1));
  EXPECT_EQ(3 * 5 * 7, gen | product);
}

TEST(Gen, FromMap) {
  auto source = seq(0, 10)
              | map([](int i) { return std::make_pair(i, i * i); })
              | as<std::map<int, int>>();
  auto gen = fromConst(source)
           | map([&](const std::pair<const int, int>& p) {
               return p.second - p.first;
             });
  EXPECT_EQ(330, gen | sum);
}

TEST(Gen, Filter) {
  const auto expected = vector<int>{1, 2, 4, 5, 7, 8};
  auto actual =
      seq(1, 9)
    | filter([](int x) { return x % 3; })
    | as<vector<int>>();
  EXPECT_EQ(expected, actual);
}

TEST(Gen, Contains) {
  {
    auto gen =
        seq(1, 9)
      | map(square);
    EXPECT_TRUE(gen | contains(49));
    EXPECT_FALSE(gen | contains(50));
  }
  {
    auto gen =
        seq(1) // infinite, to prove laziness
      | map(square)
      | eachTo<std::string>();

    // std::string gen, const char* needle
    EXPECT_TRUE(gen | take(9999) | contains("49"));
  }
}

TEST(Gen, Take) {
  auto expected = vector<int>{1, 4, 9, 16};
  auto actual =
      seq(1, 1000)
    | mapped([](int x) { return x * x; })
    | take(4)
    | as<vector<int>>();
  EXPECT_EQ(expected, actual);
}

TEST(Gen, Skip) {
  auto gen =
      seq(1, 1000)
    | mapped([](int x) { return x * x; })
    | skip(4)
    | take(4);
  EXPECT_EQ((vector<int>{25, 36, 49, 64}), gen | as<vector>());
}

TEST(Gen, Until) {
  auto gen =
      seq(1) //infinite
    | mapped([](int x) { return x * x; })
    | until([](int x) { return x >= 1000; });
  EXPECT_EQ(31, gen | count);
}

TEST(Gen, Composed) {
  // Operator, Operator
  auto valuesOf =
      filter([](Optional<int>& o) { return o.hasValue(); })
    | map([](Optional<int>& o) -> int& { return o.value(); });
  std::vector<Optional<int>> opts {
    none, 4, none, 6, none
  };
  EXPECT_EQ(4 * 4 + 6 * 6, from(opts) | valuesOf | map(square) | sum);
  // Operator, Sink
  auto sumOpt = valuesOf | sum;
  EXPECT_EQ(10, from(opts) | sumOpt);
}

TEST(Gen, Chain) {
  std::vector<int> nums {2, 3, 5, 7};
  std::map<int, int> mappings { { 3, 9}, {5, 25} };
  auto gen = from(nums) + (from(mappings) | get<1>());
  EXPECT_EQ(51, gen | sum);
  EXPECT_EQ(5, gen | take(2) | sum);
  EXPECT_EQ(26, gen | take(5) | sum);
}

TEST(Gen, Concat) {
  std::vector<std::vector<int>> nums {{2, 3}, {5, 7}};
  auto gen = from(nums) | rconcat;
  EXPECT_EQ(17, gen | sum);
  EXPECT_EQ(10, gen | take(3) | sum);
}

TEST(Gen, ConcatGen) {
  auto gen = seq(1, 10)
           | map([](int i) { return seq(1, i); })
           | concat;
  EXPECT_EQ(220, gen | sum);
  EXPECT_EQ(10, gen | take(6) | sum);
}

TEST(Gen, ConcatAlt) {
  std::vector<std::vector<int>> nums {{2, 3}, {5, 7}};
  auto actual = from(nums)
              | map([](std::vector<int>& v) { return from(v); })
              | concat
              | sum;
  auto expected = 17;
  EXPECT_EQ(expected, actual);
}

TEST(Gen, Order) {
  auto expected = vector<int>{0, 3, 5, 6, 7, 8, 9};
  auto actual =
      from({8, 6, 7, 5, 3, 0, 9})
    | order
    | as<vector>();
  EXPECT_EQ(expected, actual);
}

TEST(Gen, OrderMoved) {
  auto expected = vector<int>{0, 9, 25, 36, 49, 64, 81};
  auto actual =
      from({8, 6, 7, 5, 3, 0, 9})
    | move
    | order
    | map(square)
    | as<vector>();
  EXPECT_EQ(expected, actual);
}

TEST(Gen, OrderTake) {
  auto expected = vector<int>{9, 8, 7};
  auto actual =
      from({8, 6, 7, 5, 3, 0, 9})
    | orderByDescending(square)
    | take(3)
    | as<vector>();
  EXPECT_EQ(expected, actual);
}

TEST(Gen, MinBy) {
  EXPECT_EQ(7, seq(1, 10)
             | minBy([](int i) -> double {
                 double d = i - 6.8;
                 return d * d;
               }));
}

TEST(Gen, MaxBy) {
  auto gen = from({"three", "eleven", "four"});

  EXPECT_EQ("eleven", gen | maxBy(&strlen));
}

TEST(Gen, Append) {
  fbstring expected = "facebook";
  fbstring actual = "face";
  from(StringPiece("book")) | appendTo(actual);
  EXPECT_EQ(expected, actual);
}

TEST(Gen, FromRValue) {
  {
    // AFAICT The C++ Standard does not specify what happens to the rvalue
    // reference of a std::vector when it is used as the 'other' for an rvalue
    // constructor.  Use fbvector because we're sure its size will be zero in
    // this case.
    folly::fbvector<int> v({1,2,3,4});
    auto q1 = from(v);
    EXPECT_EQ(v.size(), 4);  // ensure that the lvalue version was called!
    auto expected = 1 * 2 * 3 * 4;
    EXPECT_EQ(expected, q1 | product);

    auto q2 = from(std::move(v));
    EXPECT_EQ(v.size(), 0);  // ensure that rvalue version was called
    EXPECT_EQ(expected, q2 | product);
  }
  {
    auto expected = 7;
    auto q = from([] {return vector<int>({3,7,5}); }());
    EXPECT_EQ(expected, q | max);
  }
  {
    for (auto size: {5, 1024, 16384, 1<<20}) {
      auto q1 = from(vector<int>(size, 2));
      auto q2 = from(vector<int>(size, 3));
      // If the rvalue specialization is broken/gone, then the compiler will
      // (disgustingly!) just store a *reference* to the temporary object,
      // which is bad.  Try to catch this by allocating two temporary vectors
      // of the same size, so that they'll probably use the same underlying
      // buffer if q1's vector is destructed before q2's vector is constructed.
      EXPECT_EQ(size * 2 + size * 3, (q1 | sum) + (q2 | sum));
    }
  }
  {
    auto q = from(set<int>{1,2,3,2,1});
    EXPECT_EQ(q | sum, 6);
  }
}

TEST(Gen, OrderBy) {
  auto expected = vector<int>{5, 6, 4, 7, 3, 8, 2, 9, 1, 10};
  auto actual =
      seq(1, 10)
    | orderBy([](int x) { return (5.1 - x) * (5.1 - x); })
    | as<vector>();
  EXPECT_EQ(expected, actual);
}

TEST(Gen, Foldl) {
  int expected = 2 * 3 * 4 * 5;
  auto actual =
      seq(2, 5)
    | foldl(1, multiply);
  EXPECT_EQ(expected, actual);
}

TEST(Gen, Reduce) {
  int expected = 2 + 3 + 4 + 5;
  auto actual = seq(2, 5) | reduce(add);
  EXPECT_EQ(expected, actual);
}

TEST(Gen, ReduceBad) {
  auto gen = seq(1) | take(0);
  try {
    EXPECT_TRUE(true);
    gen | reduce(add);
    EXPECT_TRUE(false);
  } catch (...) {
  }
}

TEST(Gen, Moves) {
  std::vector<unique_ptr<int>> ptrs;
  ptrs.emplace_back(new int(1));
  EXPECT_NE(ptrs.front().get(), nullptr);
  auto ptrs2 = from(ptrs) | move | as<vector>();
  EXPECT_EQ(ptrs.front().get(), nullptr);
  EXPECT_EQ(**ptrs2.data(), 1);
}

TEST(Gen, First) {
  auto gen =
      seq(0)
    | filter([](int x) { return x > 3; });
  EXPECT_EQ(4, gen | first);
}

TEST(Gen, FromCopy) {
  vector<int> v {3, 5};
  auto src = from(v);
  auto copy = fromCopy(v);
  EXPECT_EQ(8, src | sum);
  EXPECT_EQ(8, copy | sum);
  v[1] = 7;
  EXPECT_EQ(10, src | sum);
  EXPECT_EQ(8, copy | sum);
}

TEST(Gen, Get) {
  std::map<int, int> pairs {
    {1, 1},
    {2, 4},
    {3, 9},
    {4, 16},
  };
  auto pairSrc = from(pairs);
  auto keys = pairSrc | get<0>();
  auto values = pairSrc | get<1>();
  EXPECT_EQ(10, keys | sum);
  EXPECT_EQ(30, values | sum);
  EXPECT_EQ(30, keys | map(square) | sum);
  pairs[5] = 25;
  EXPECT_EQ(15, keys | sum);
  EXPECT_EQ(55, values | sum);

  vector<tuple<int, int, int>> tuples {
    make_tuple(1, 1, 1),
    make_tuple(2, 4, 8),
    make_tuple(3, 9, 27),
  };
  EXPECT_EQ(36, from(tuples) | get<2>() | sum);
}

TEST(Gen, Any) {
  EXPECT_TRUE(seq(0) | any);
  EXPECT_TRUE(seq(0, 1) | any);
  EXPECT_TRUE(seq(0, 10) | any([](int i) { return i == 7; }));
  EXPECT_FALSE(seq(0, 10) | any([](int i) { return i == 11; }));

  EXPECT_TRUE(from({1}) | any);
  EXPECT_FALSE(range(0, 0) | any);
  EXPECT_FALSE(from({1}) | take(0) | any);
}

TEST(Gen, All) {
  EXPECT_TRUE(seq(0, 10) | all([](int i) { return i < 11; }));
  EXPECT_FALSE(seq(0, 10) | all([](int i) { return i < 5; }));
  EXPECT_FALSE(seq(0) | take(9999) | all([](int i) { return i < 10; }));

  // empty lists satisfies all
  EXPECT_TRUE(seq(0) | take(0) | all([](int i) { return i < 50; }));
  EXPECT_TRUE(seq(0) | take(0) | all([](int i) { return i > 50; }));
}

TEST(Gen, Yielders) {
  auto gen = GENERATOR(int) {
    for (int i = 1; i <= 5; ++i) {
      yield(i);
    }
    yield(7);
    for (int i = 3; ; ++i) {
      yield(i * i);
    }
  };
  vector<int> expected {
    1, 2, 3, 4, 5, 7, 9, 16, 25
  };
  EXPECT_EQ(expected, gen | take(9) | as<vector>());
}

TEST(Gen, NestedYield) {
  auto nums = GENERATOR(int) {
    for (int i = 1; ; ++i) {
      yield(i);
    }
  };
  auto gen = GENERATOR(int) {
    nums | take(10) | yield;
    seq(1, 5) | [&](int i) {
      yield(i);
    };
  };
  EXPECT_EQ(70, gen | sum);
}

TEST(Gen, MapYielders) {
  auto gen = seq(1, 5)
           | map([](int n) {
               return GENERATOR(int) {
                 int i;
                 for (i = 1; i < n; ++i)
                   yield(i);
                 for (; i >= 1; --i)
                   yield(i);
               };
             })
           | concat;
  vector<int> expected {
                1,
             1, 2, 1,
          1, 2, 3, 2, 1,
       1, 2, 3, 4, 3, 2, 1,
    1, 2, 3, 4, 5, 4, 3, 2, 1,
  };
  EXPECT_EQ(expected, gen | as<vector>());
}

TEST(Gen, VirtualGen) {
  VirtualGen<int> v(seq(1, 10));
  EXPECT_EQ(55, v | sum);
  v = v | map(square);
  EXPECT_EQ(385, v | sum);
  v = v | take(5);
  EXPECT_EQ(55, v | sum);
  EXPECT_EQ(30, v | take(4) | sum);
}


TEST(Gen, CustomType) {
  struct Foo{
    int y;
  };
  auto gen = from({Foo{2}, Foo{3}})
           | map([](const Foo& f) { return f.y; });
  EXPECT_EQ(5, gen | sum);
}

TEST(Gen, NoNeedlessCopies) {
  auto gen = seq(1, 5)
           | map([](int x) { return unique_ptr<int>(new int(x)); })
           | map([](unique_ptr<int> p) { return p; })
           | map([](unique_ptr<int>&& p) { return std::move(p); })
           | map([](const unique_ptr<int>& p) { return *p; });
  EXPECT_EQ(15, gen | sum);
  EXPECT_EQ(6, gen | take(3) | sum);
}

namespace {
class TestIntSeq : public GenImpl<int, TestIntSeq> {
 public:
  TestIntSeq() { }

  template <class Body>
  bool apply(Body&& body) const {
    for (int i = 1; i < 6; ++i) {
      if (!body(i)) {
        return false;
      }
    }
    return true;
  }

  TestIntSeq(TestIntSeq&&) = default;
  TestIntSeq& operator=(TestIntSeq&&) = default;
  TestIntSeq(const TestIntSeq&) = delete;
  TestIntSeq& operator=(const TestIntSeq&) = delete;
};
}  // namespace

TEST(Gen, NoGeneratorCopies) {
  EXPECT_EQ(15, TestIntSeq() | sum);
  auto x = TestIntSeq() | take(3);
  EXPECT_EQ(6, std::move(x) | sum);
}

TEST(Gen, FromArray) {
  int source[] = {2, 3, 5, 7};
  auto gen = from(source);
  EXPECT_EQ(2 * 3 * 5 * 7, gen | product);
}

TEST(Gen, FromStdArray) {
  std::array<int,4> source {{2, 3, 5, 7}};
  auto gen = from(source);
  EXPECT_EQ(2 * 3 * 5 * 7, gen | product);
}

TEST(Gen, StringConcat) {
  auto gen = seq(1, 10)
           | map([](int n) { return folly::to<fbstring>(n); })
           | rconcat;
  EXPECT_EQ("12345678910", gen | as<fbstring>());
}

struct CopyCounter {
  static int alive;
  int copies;
  int moves;

  CopyCounter() : copies(0), moves(0) {
    ++alive;
  }

  CopyCounter(CopyCounter&& source) {
    *this = std::move(source);
    ++alive;
  }

  CopyCounter(const CopyCounter& source) {
    *this = source;
    ++alive;
  }

  ~CopyCounter() {
    --alive;
  }

  CopyCounter& operator=(const CopyCounter& source) {
    this->copies = source.copies + 1;
    this->moves = source.moves;
    return *this;
  }

  CopyCounter& operator=(CopyCounter&& source) {
    this->copies = source.copies;
    this->moves = source.moves + 1;
    return *this;
  }
};

int CopyCounter::alive = 0;

TEST(Gen, CopyCount) {
  vector<CopyCounter> originals;
  originals.emplace_back();
  EXPECT_EQ(1, originals.size());
  EXPECT_EQ(0, originals.back().copies);

  vector<CopyCounter> copies = from(originals) | as<vector>();
  EXPECT_EQ(1, copies.back().copies);
  EXPECT_EQ(0, copies.back().moves);

  vector<CopyCounter> moves = from(originals) | move | as<vector>();
  EXPECT_EQ(0, moves.back().copies);
  EXPECT_EQ(1, moves.back().moves);
}

// test dynamics with various layers of nested arrays.
TEST(Gen, Dynamic) {
  dynamic array1 = {1, 2};
  EXPECT_EQ(dynamic(3), from(array1) | sum);
  dynamic array2 = {{1}, {1, 2}};
  EXPECT_EQ(dynamic(4), from(array2) | rconcat | sum);
  dynamic array3 = {{{1}}, {{1}, {1, 2}}};
  EXPECT_EQ(dynamic(5), from(array3) | rconcat | rconcat | sum);
}

TEST(StringGen, EmptySplit) {
  auto collect = eachTo<std::string>() | as<vector>();
  {
    auto pieces = split("", ',') | collect;
    EXPECT_EQ(0, pieces.size());
  }

  // The last delimiter is eaten, just like std::getline
  {
    auto pieces = split(",", ',') | collect;
    EXPECT_EQ(1, pieces.size());
    EXPECT_EQ("", pieces[0]);
  }

  {
    auto pieces = split(",,", ',') | collect;
    EXPECT_EQ(2, pieces.size());
    EXPECT_EQ("", pieces[0]);
    EXPECT_EQ("", pieces[1]);
  }

  {
    auto pieces = split(",,", ',') | take(1) | collect;
    EXPECT_EQ(1, pieces.size());
    EXPECT_EQ("", pieces[0]);
  }
}

TEST(StringGen, Split) {
  auto collect = eachTo<std::string>() | as<vector>();
  {
    auto pieces = split("hello,, world, goodbye, meow", ',') | collect;
    EXPECT_EQ(5, pieces.size());
    EXPECT_EQ("hello", pieces[0]);
    EXPECT_EQ("", pieces[1]);
    EXPECT_EQ(" world", pieces[2]);
    EXPECT_EQ(" goodbye", pieces[3]);
    EXPECT_EQ(" meow", pieces[4]);
  }

  {
    auto pieces = split("hello,, world, goodbye, meow", ',')
                | take(3) | collect;
    EXPECT_EQ(3, pieces.size());
    EXPECT_EQ("hello", pieces[0]);
    EXPECT_EQ("", pieces[1]);
    EXPECT_EQ(" world", pieces[2]);
  }

  {
    auto pieces = split("hello,, world, goodbye, meow", ',')
                | take(5) | collect;
    EXPECT_EQ(5, pieces.size());
    EXPECT_EQ("hello", pieces[0]);
    EXPECT_EQ("", pieces[1]);
    EXPECT_EQ(" world", pieces[2]);
  }
}

TEST(StringGen, EmptyResplit) {
  auto collect = eachTo<std::string>() | as<vector>();
  {
    auto pieces = from({""}) | resplit(',') | collect;
    EXPECT_EQ(0, pieces.size());
  }

  // The last delimiter is eaten, just like std::getline
  {
    auto pieces = from({","}) | resplit(',') | collect;
    EXPECT_EQ(1, pieces.size());
    EXPECT_EQ("", pieces[0]);
  }

  {
    auto pieces = from({",,"}) | resplit(',') | collect;
    EXPECT_EQ(2, pieces.size());
    EXPECT_EQ("", pieces[0]);
    EXPECT_EQ("", pieces[1]);
  }
}

TEST(StringGen, Resplit) {
  auto collect = eachTo<std::string>() | as<vector>();
  {
    auto pieces = from({"hello,, world, goodbye, meow"}) |
      resplit(',') | collect;
    EXPECT_EQ(5, pieces.size());
    EXPECT_EQ("hello", pieces[0]);
    EXPECT_EQ("", pieces[1]);
    EXPECT_EQ(" world", pieces[2]);
    EXPECT_EQ(" goodbye", pieces[3]);
    EXPECT_EQ(" meow", pieces[4]);
  }
  {
    auto pieces = from({"hel", "lo,", ", world", ", goodbye, m", "eow"}) |
      resplit(',') | collect;
    EXPECT_EQ(5, pieces.size());
    EXPECT_EQ("hello", pieces[0]);
    EXPECT_EQ("", pieces[1]);
    EXPECT_EQ(" world", pieces[2]);
    EXPECT_EQ(" goodbye", pieces[3]);
    EXPECT_EQ(" meow", pieces[4]);
  }
}

template<typename F>
void runUnsplitSuite(F fn) {
  fn("hello, world");
  fn("hello,world,goodbye");
  fn(" ");
  fn("");
  fn(", ");
  fn(", a, b,c");
}

TEST(StringGen, Unsplit) {

  auto basicFn = [](const StringPiece& s) {
    EXPECT_EQ(split(s, ',') | unsplit(','), s);
  };

  auto existingBuffer = [](const StringPiece& s) {
    folly::fbstring buffer("asdf");
    split(s, ',') | unsplit(',', &buffer);
    auto expected = folly::to<folly::fbstring>(
        "asdf", s.empty() ? "" : ",", s);
    EXPECT_EQ(expected, buffer);
  };

  auto emptyBuffer = [](const StringPiece& s) {
    std::string buffer;
    split(s, ',') | unsplit(',', &buffer);
    EXPECT_EQ(s, buffer);
  };

  auto stringDelim = [](const StringPiece& s) {
    EXPECT_EQ(s, split(s, ',') | unsplit(","));
    std::string buffer;
    split(s, ',') | unsplit(",", &buffer);
    EXPECT_EQ(buffer, s);
  };

  runUnsplitSuite(basicFn);
  runUnsplitSuite(existingBuffer);
  runUnsplitSuite(emptyBuffer);
  runUnsplitSuite(stringDelim);
  EXPECT_EQ("1, 2, 3", seq(1, 3) | unsplit(", "));
}

TEST(FileGen, ByLine) {
  auto collect = eachTo<std::string>() | as<vector>();
  test::TemporaryFile file("ByLine");
  static const std::string lines(
      "Hello world\n"
      "This is the second line\n"
      "\n"
      "\n"
      "a few empty lines above\n"
      "incomplete last line");
  EXPECT_EQ(lines.size(), write(file.fd(), lines.data(), lines.size()));

  auto expected = from({lines}) | resplit('\n') | collect;
  auto found = byLine(file.path().c_str()) | collect;

  EXPECT_TRUE(expected == found);
}

class FileGenBufferedTest : public ::testing::TestWithParam<int> { };

TEST_P(FileGenBufferedTest, FileWriter) {
  size_t bufferSize = GetParam();
  test::TemporaryFile file("FileWriter");

  static const std::string lines(
      "Hello world\n"
      "This is the second line\n"
      "\n"
      "\n"
      "a few empty lines above\n");

  auto src = from({lines, lines, lines, lines, lines, lines, lines, lines});
  auto collect = eachTo<std::string>() | as<vector>();
  auto expected = src | resplit('\n') | collect;

  src | eachAs<StringPiece>() | toFile(file.fd(), bufferSize);
  auto found = byLine(file.path().c_str()) | collect;

  EXPECT_TRUE(expected == found);
}

INSTANTIATE_TEST_CASE_P(
    DifferentBufferSizes,
    FileGenBufferedTest,
    ::testing::Values(0, 1, 2, 4, 8, 64, 4096));

int main(int argc, char *argv[]) {
  testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);
  return RUN_ALL_TESTS();
}
