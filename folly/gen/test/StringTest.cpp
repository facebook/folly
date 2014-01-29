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

#include <glog/logging.h>
#include <gtest/gtest.h>
#include <iosfwd>
#include <map>
#include <vector>

#include "folly/gen/String.h"

using namespace folly::gen;
using namespace folly;
using std::make_tuple;
using std::ostream;
using std::pair;
using std::string;
using std::tuple;
using std::unique_ptr;
using std::vector;

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

TEST(StringGen, EachToTuple) {
  {
    auto lines = "2:1.414:yo 3:1.732:hi";
    auto actual
      = split(lines, ' ')
      | eachToTuple<int, double, std::string>(':')
      | as<vector>();
    vector<tuple<int, double, std::string>> expected {
      make_tuple(2, 1.414, "yo"),
      make_tuple(3, 1.732, "hi"),
    };
    EXPECT_EQ(expected, actual);
  }
  {
    auto lines = "2 3";
    auto actual
      = split(lines, ' ')
      | eachToTuple<int>(',')
      | as<vector>();
    vector<tuple<int>> expected {
      make_tuple(2),
      make_tuple(3),
    };
    EXPECT_EQ(expected, actual);
  }
  {
    // StringPiece target
    auto lines = "1:cat 2:dog";
    auto actual
      = split(lines, ' ')
      | eachToTuple<int, StringPiece>(':')
      | as<vector>();
    vector<tuple<int, StringPiece>> expected {
      make_tuple(1, "cat"),
      make_tuple(2, "dog"),
    };
    EXPECT_EQ(expected, actual);
  }
  {
    // Empty field
    auto lines = "2:tjackson:4 3::5";
    auto actual
      = split(lines, ' ')
      | eachToTuple<int, fbstring, int>(':')
      | as<vector>();
    vector<tuple<int, fbstring, int>> expected {
      make_tuple(2, "tjackson", 4),
      make_tuple(3, "", 5),
    };
    EXPECT_EQ(expected, actual);
  }
  {
    // Excess fields
    auto lines = "1:2 3:4:5";
    EXPECT_THROW((split(lines, ' ')
                    | eachToTuple<int, int>(':')
                    | as<vector>()),
                 std::runtime_error);
  }
  {
    // Missing fields
    auto lines = "1:2:3 4:5";
    EXPECT_THROW((split(lines, ' ')
                    | eachToTuple<int, int, int>(':')
                    | as<vector>()),
                 std::runtime_error);
  }
}

TEST(StringGen, EachToPair) {
  {
    // char delimiters
    auto lines = "2:1.414 3:1.732";
    auto actual
      = split(lines, ' ')
      | eachToPair<int, double>(':')
      | as<std::map<int, double>>();
    std::map<int, double> expected {
      { 3, 1.732 },
      { 2, 1.414 },
    };
    EXPECT_EQ(expected, actual);
  }
  {
    // string delimiters
    auto lines = "ab=>cd ef=>gh";
    auto actual
      = split(lines, ' ')
      | eachToPair<string, string>("=>")
      | as<std::map<string, string>>();
    std::map<string, string> expected {
      { "ab", "cd" },
      { "ef", "gh" },
    };
    EXPECT_EQ(expected, actual);
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

  auto basicFn = [](StringPiece s) {
    EXPECT_EQ(split(s, ',') | unsplit(','), s);
  };

  auto existingBuffer = [](StringPiece s) {
    folly::fbstring buffer("asdf");
    split(s, ',') | unsplit(',', &buffer);
    auto expected = folly::to<folly::fbstring>(
        "asdf", s.empty() ? "" : ",", s);
    EXPECT_EQ(expected, buffer);
  };

  auto emptyBuffer = [](StringPiece s) {
    std::string buffer;
    split(s, ',') | unsplit(',', &buffer);
    EXPECT_EQ(s, buffer);
  };

  auto stringDelim = [](StringPiece s) {
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

int main(int argc, char *argv[]) {
  testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);
  return RUN_ALL_TESTS();
}
