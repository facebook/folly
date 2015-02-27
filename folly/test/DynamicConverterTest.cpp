/*
 * Copyright 2015 Facebook, Inc.
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

// @author Nicholas Ormrod <njormrod@fb.com>

#include <folly/DynamicConverter.h>

#include <algorithm>
#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include <map>
#include <vector>

#include <folly/Benchmark.h>

using namespace folly;
using namespace folly::dynamicconverter_detail;

TEST(DynamicConverter, template_metaprogramming) {
  struct A {};

  bool c1f = is_container<int>::value;
  bool c2f = is_container<std::pair<int, int>>::value;
  bool c3f = is_container<A>::value;

  bool c1t = is_container<std::vector<int>>::value;
  bool c2t = is_container<std::set<int>>::value;
  bool c3t = is_container<std::map<int, int>>::value;

  EXPECT_EQ(c1f, false);
  EXPECT_EQ(c2f, false);
  EXPECT_EQ(c3f, false);
  EXPECT_EQ(c1t, true);
  EXPECT_EQ(c2t, true);
  EXPECT_EQ(c3t, true);


  bool m1f = is_map<int>::value;
  bool m2f = is_map<std::set<int>>::value;

  bool m1t = is_map<std::map<int, int>>::value;

  EXPECT_EQ(m1f, false);
  EXPECT_EQ(m2f, false);
  EXPECT_EQ(m1t, true);


  bool r1f = is_range<int>::value;

  bool r1t = is_range<std::set<int>>::value;
  bool r2t = is_range<std::vector<int>>::value;

  EXPECT_EQ(r1f, false);
  EXPECT_EQ(r1t, true);
  EXPECT_EQ(r2t, true);
}

TEST(DynamicConverter, arithmetic_types) {
  dynamic d1 = 12;
  auto i1 = convertTo<int>(d1);
  EXPECT_EQ(i1, 12);

  dynamic d2 = 123456789012345;
  auto i2 = convertTo<int64_t>(d2);
  EXPECT_EQ(i2, 123456789012345);

  dynamic d4 = 3.141;
  auto i4 = convertTo<float>(d4);
  EXPECT_EQ((int)(i4*100), 314);

  dynamic d5 = true;
  auto i5 = convertTo<bool>(d5);
  EXPECT_EQ(i5, true);

  dynamic d6 = 15;
  const auto i6 = convertTo<const int>(d6);
  EXPECT_EQ(i6, 15);

  dynamic d7 = "87";
  auto i7 = convertTo<int>(d7);
  EXPECT_EQ(i7, 87);

  dynamic d8 = "false";
  auto i8 = convertTo<bool>(d8);
  EXPECT_EQ(i8, false);
}

TEST(DynamicConverter, simple_builtins) {
  dynamic d1 = "Haskell";
  auto i1 = convertTo<folly::fbstring>(d1);
  EXPECT_EQ(i1, "Haskell");

  dynamic d2 = 13;
  auto i2 = convertTo<std::string>(d2);
  EXPECT_EQ(i2, "13");

  dynamic d3 = { 12, "Scala" };
  auto i3 = convertTo<std::pair<int, std::string>>(d3);
  EXPECT_EQ(i3.first, 12);
  EXPECT_EQ(i3.second, "Scala");

  dynamic d4 = dynamic::object("C", "C++");
  auto i4 = convertTo<std::pair<std::string, folly::fbstring>>(d4);
  EXPECT_EQ(i4.first, "C");
  EXPECT_EQ(i4.second, "C++");
}

TEST(DynamicConverter, simple_fbvector) {
  dynamic d1 = { 1, 2, 3 };
  auto i1 = convertTo<folly::fbvector<int>>(d1);
  decltype(i1) i1b = { 1, 2, 3 };
  EXPECT_EQ(i1, i1b);
}

TEST(DynamicConverter, simple_container) {
  dynamic d1 = { 1, 2, 3 };
  auto i1 = convertTo<std::vector<int>>(d1);
  decltype(i1) i1b = { 1, 2, 3 };
  EXPECT_EQ(i1, i1b);

  dynamic d2 = { 1, 3, 5, 2, 4 };
  auto i2 = convertTo<std::set<int>>(d2);
  decltype(i2) i2b = { 1, 2, 3, 5, 4 };
  EXPECT_EQ(i2, i2b);
}

TEST(DynamicConverter, simple_map) {
  dynamic d1 = dynamic::object(1, "one")(2, "two");
  auto i1 = convertTo<std::map<int, std::string>>(d1);
  decltype(i1) i1b = { { 1, "one" }, { 2, "two" } };
  EXPECT_EQ(i1, i1b);

  dynamic d2 = { { 3, "three" }, { 4, "four" } };
  auto i2 = convertTo<std::unordered_map<int, std::string>>(d2);
  decltype(i2) i2b = { { 3, "three" }, { 4, "four" } };
  EXPECT_EQ(i2, i2b);
}

TEST(DynamicConverter, map_keyed_by_string) {
  dynamic d1 = dynamic::object("1", "one")("2", "two");
  auto i1 = convertTo<std::map<std::string, std::string>>(d1);
  decltype(i1) i1b = { { "1", "one" }, { "2", "two" } };
  EXPECT_EQ(i1, i1b);

  dynamic d2 = { { "3", "three" }, { "4", "four" } };
  auto i2 = convertTo<std::unordered_map<std::string, std::string>>(d2);
  decltype(i2) i2b = { { "3", "three" }, { "4", "four" } };
  EXPECT_EQ(i2, i2b);
}

TEST(DynamicConverter, map_to_vector_of_pairs) {
  dynamic d1 = dynamic::object("1", "one")("2", "two");
  auto i1 = convertTo<std::vector<std::pair<std::string, std::string>>>(d1);
  std::sort(i1.begin(), i1.end());
  decltype(i1) i1b = { { "1", "one" }, { "2", "two" } };
  EXPECT_EQ(i1, i1b);
}

TEST(DynamicConverter, nested_containers) {
  dynamic d1 = { { 1 }, { }, { 2, 3 } };
  auto i1 = convertTo<folly::fbvector<std::vector<uint8_t>>>(d1);
  decltype(i1) i1b = { { 1 }, { }, { 2, 3 } };
  EXPECT_EQ(i1, i1b);

  dynamic h2a = { "3", ".", "1", "4" };
  dynamic h2b = { "2", ".", "7", "2" };
  dynamic d2 = dynamic::object(3.14, h2a)(2.72, h2b);
  auto i2 = convertTo<std::map<double, std::vector<folly::fbstring>>>(d2);
  decltype(i2) i2b =
    { { 3.14, { "3", ".", "1", "4" } },
      { 2.72, { "2", ".", "7", "2" } } };
  EXPECT_EQ(i2, i2b);
}

struct A {
  int i;
  bool operator==(const A & o) const { return i == o.i; }
};
namespace folly {
template <> struct DynamicConverter<A> {
  static A convert(const dynamic & d) {
    return { convertTo<int>(d["i"]) };
  }
};
}
TEST(DynamicConverter, custom_class) {
  dynamic d1 = dynamic::object("i", 17);
  auto i1 = convertTo<A>(d1);
  EXPECT_EQ(i1.i, 17);

  dynamic d2 = { dynamic::object("i", 18), dynamic::object("i", 19) };
  auto i2 = convertTo<std::vector<A>>(d2);
  decltype(i2) i2b = { { 18 }, { 19 } };
  EXPECT_EQ(i2, i2b);
}

TEST(DynamicConverter, crazy) {
  // we are going to create a vector<unordered_map<bool, T>>
  // we will construct some of the maps from dynamic objects,
  //   some from a vector of KV pairs.
  // T will be vector<set<string>>

  std::set<std::string>
    s1 = { "a", "e", "i", "o", "u" },
    s2 = { "2", "3", "5", "7" },
    s3 = { "Hello", "World" };

  std::vector<std::set<std::string>>
    v1 = {},
    v2 = { s1, s2 },
    v3 = { s3 };

  std::unordered_map<bool, std::vector<std::set<std::string>>>
    m1 = { { true, v1 }, { false, v2 } },
    m2 = { { true, v3 } };

  std::vector<std::unordered_map<bool, std::vector<std::set<std::string>>>>
    f1 = { m1, m2 };


  dynamic
    ds1 = { "a", "e", "i", "o", "u" },
    ds2 = { "2", "3", "5", "7" },
    ds3 = { "Hello", "World" };

  dynamic
    dv1 = {},
    dv2 = { ds1, ds2 },
    dv3 = { ds3 };

  dynamic
    dm1 = dynamic::object(true, dv1)(false, dv2),
    dm2 = { { true, dv3 } };

  dynamic
    df1 = { dm1, dm2 };


  auto i = convertTo<std::vector<std::unordered_map<bool, std::vector<
          std::set<std::string>>>>>(df1); // yes, that is 5 close-chevrons

  EXPECT_EQ(f1, i);
}

TEST(DynamicConverter, consts) {
  dynamic d1 = 7.5;
  auto i1 = convertTo<const double>(d1);
  EXPECT_EQ(7.5, i1);

  dynamic d2 = "Hello";
  auto i2 = convertTo<const std::string>(d2);
  decltype(i2) i2b = "Hello";
  EXPECT_EQ(i2b, i2);

  dynamic d3 = true;
  auto i3 = convertTo<const bool>(d3);
  EXPECT_EQ(true, i3);

  dynamic d4 = "true";
  auto i4 = convertTo<const bool>(d4);
  EXPECT_EQ(true, i4);

  dynamic d5 = { 1, 2 };
  auto i5 = convertTo<const std::pair<const int, const int>>(d5);
  decltype(i5) i5b = { 1, 2 };
  EXPECT_EQ(i5b, i5);
}

struct Token {
  int kind_;
  fbstring lexeme_;

  explicit Token(int kind, const fbstring& lexeme)
    : kind_(kind), lexeme_(lexeme) {}
};

namespace folly {
template <> struct DynamicConverter<Token> {
  static Token convert(const dynamic& d) {
    int k = convertTo<int>(d["KIND"]);
    fbstring lex = convertTo<fbstring>(d["LEXEME"]);
    return Token(k, lex);
  }
};
}

TEST(DynamicConverter, example) {
  dynamic d1 = dynamic::object("KIND", 2)("LEXEME", "a token");
  auto i1 = convertTo<Token>(d1);
  EXPECT_EQ(i1.kind_, 2);
  EXPECT_EQ(i1.lexeme_, "a token");
}

TEST(DynamicConverter, construct) {
  using std::vector;
  using std::map;
  using std::pair;
  using std::string;
  {
    vector<int> c { 1, 2, 3 };
    dynamic d = { 1, 2, 3 };
    EXPECT_EQ(d, toDynamic(c));
  }

  {
    map<int, int> c { { 2, 4 }, { 3, 9 } };
    dynamic d = dynamic::object(2, 4)(3, 9);
    EXPECT_EQ(d, toDynamic(c));
  }

  {
    map<string, string> c { { "a", "b" } };
    dynamic d = dynamic::object("a", "b");
    EXPECT_EQ(d, toDynamic(c));
  }

  {
    map<string, pair<string, int>> c { { "a", { "b", 3 } } };
    dynamic d = dynamic::object("a", dynamic { "b", 3 });
    EXPECT_EQ(d, toDynamic(c));
  }

  {
    map<string, pair<string, int>> c { { "a", { "b", 3 } } };
    dynamic d = dynamic::object("a", dynamic { "b", 3 });
    EXPECT_EQ(d, toDynamic(c));
  }

  {
    vector<int> vi { 2, 3, 4, 5 };
    auto c = std::make_pair(range(vi.begin(), vi.begin() + 3),
                            range(vi.begin() + 1, vi.begin() + 4));
    dynamic d = { { 2, 3, 4 }, { 3, 4, 5 } };
    EXPECT_EQ(d, toDynamic(c));
  }
}

TEST(DynamicConverter, errors) {
  const auto int32Over =
    static_cast<int64_t>(std::numeric_limits<int32_t>().max()) + 1;
  const auto floatOver =
    static_cast<double>(std::numeric_limits<float>().max()) * 2;

  dynamic d1 = int32Over;
  EXPECT_THROW(convertTo<int32_t>(d1), std::range_error);

  dynamic d2 = floatOver;
  EXPECT_THROW(convertTo<float>(d2), std::range_error);
}

int main(int argc, char ** argv) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  if (FLAGS_benchmark) {
    folly::runBenchmarks();
  }
  return RUN_ALL_TESTS();
}
