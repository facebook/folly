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

//
// Author: andrei.alexandrescu@fb.com

#include <folly/Traits.h>
#include <folly/Random.h>
#include <folly/FBString.h>
#include <folly/FBVector.h>

#include <gflags/gflags.h>

#include <gtest/gtest.h>
#include <list>
#include <map>
#include <memory>
#include <boost/random.hpp>

using namespace std;
using namespace folly;

auto static const seed = randomNumberSeed();
typedef boost::mt19937 RandomT;
static RandomT rng(seed);
static const size_t maxString = 100;
static const bool avoidAliasing = true;

template <class Integral1, class Integral2>
Integral2 random(Integral1 low, Integral2 up) {
  boost::uniform_int<> range(low, up);
  return range(rng);
}

template <class String>
void randomString(String* toFill, unsigned int maxSize = 1000) {
  assert(toFill);
  toFill->resize(random(0, maxSize));
  FOR_EACH (i, *toFill) {
    *i = random('a', 'z');
  }
}

template <class String, class Integral>
void Num2String(String& str, Integral n) {
  str.resize(10, '\0');
  sprintf(&str[0], "%ul", 10);
  str.resize(strlen(str.c_str()));
}

std::list<char> RandomList(unsigned int maxSize) {
  std::list<char> lst(random(0u, maxSize));
  std::list<char>::iterator i = lst.begin();
  for (; i != lst.end(); ++i) {
    *i = random('a', 'z');
  }
  return lst;
}

template<class T> T randomObject();

template<> int randomObject<int>() {
  return random(0, 1024);
}

template<> folly::fbstring randomObject<folly::fbstring>() {
  folly::fbstring result;
  randomString(&result);
  return result;
}

////////////////////////////////////////////////////////////////////////////////
// Tests begin here
////////////////////////////////////////////////////////////////////////////////

TEST(fbvector, clause_23_3_6_1_3_ambiguity) {
  fbvector<int> v(10, 20);
  EXPECT_EQ(v.size(), 10);
  FOR_EACH (i, v) {
    EXPECT_EQ(*i, 20);
  }
}

TEST(fbvector, clause_23_3_6_1_11_ambiguity) {
  fbvector<int> v;
  v.assign(10, 20);
  EXPECT_EQ(v.size(), 10);
  FOR_EACH (i, v) {
    EXPECT_EQ(*i, 20);
  }
}

TEST(fbvector, clause_23_3_6_2_6) {
  fbvector<int> v;
  auto const n = random(0U, 10000U);
  v.reserve(n);
  auto const n1 = random(0U, 10000U);
  auto const obj = randomObject<int>();
  v.assign(n1, obj);
  v.shrink_to_fit();
  // Nothing to verify except that the call made it through
}

TEST(fbvector, clause_23_3_6_4_ambiguity) {
  fbvector<int> v;
  fbvector<int>::const_iterator i = v.end();
  v.insert(i, 10, 20);
  EXPECT_EQ(v.size(), 10);
  FOR_EACH (i, v) {
    EXPECT_EQ(*i, 20);
  }
}

TEST(fbvector, composition) {
  fbvector< fbvector<double> > matrix(100, fbvector<double>(100));
}

TEST(fbvector, works_with_std_string) {
  fbvector<std::string> v(10, "hello");
  EXPECT_EQ(v.size(), 10);
  v.push_back("world");
}

namespace {
class UserDefinedType { int whatevs_; };
}

FOLLY_ASSUME_FBVECTOR_COMPATIBLE(UserDefinedType);

TEST(fbvector, works_with_user_defined_type) {
  fbvector<UserDefinedType> v(10);
  EXPECT_EQ(v.size(), 10);
  v.push_back(UserDefinedType());
}

TEST(fbvector, move_construction) {
  fbvector<int> v1(100, 100);
  fbvector<int> v2;
  EXPECT_EQ(v1.size(), 100);
  EXPECT_EQ(v1.front(), 100);
  EXPECT_EQ(v2.size(), 0);
  v2 = std::move(v1);
  EXPECT_EQ(v1.size(), 0);
  EXPECT_EQ(v2.size(), 100);
  EXPECT_EQ(v2.front(), 100);

  v1.assign(100, 100);
  auto other = std::move(v1);
  EXPECT_EQ(v1.size(), 0);
  EXPECT_EQ(other.size(), 100);
  EXPECT_EQ(other.front(), 100);
}

TEST(fbvector, emplace) {
  fbvector<std::string> s(12, "asd");
  EXPECT_EQ(s.size(), 12);
  EXPECT_EQ(s.front(), "asd");
  s.emplace_back("funk");
  EXPECT_EQ(s.back(), "funk");
}

TEST(fbvector, initializer_lists) {
  fbvector<int> vec = { 1, 2, 3 };
  EXPECT_EQ(vec.size(), 3);
  EXPECT_EQ(vec[0], 1);
  EXPECT_EQ(vec[1], 2);
  EXPECT_EQ(vec[2], 3);

  vec = { 0, 0, 12, 16 };
  EXPECT_EQ(vec.size(), 4);
  EXPECT_EQ(vec[0], 0);
  EXPECT_EQ(vec[1], 0);
  EXPECT_EQ(vec[2], 12);
  EXPECT_EQ(vec[3], 16);

  vec.insert(vec.begin() + 1, { 23, 23 });
  EXPECT_EQ(vec.size(), 6);
  EXPECT_EQ(vec[0], 0);
  EXPECT_EQ(vec[1], 23);
  EXPECT_EQ(vec[2], 23);
  EXPECT_EQ(vec[3], 0);
  EXPECT_EQ(vec[4], 12);
  EXPECT_EQ(vec[5], 16);
}

TEST(fbvector, unique_ptr) {
  fbvector<std::unique_ptr<int> > v(12);
  std::unique_ptr<int> p(new int(12));
  v.push_back(std::move(p));
  EXPECT_EQ(*v.back(), 12);

  v[0] = std::move(p);
  EXPECT_FALSE(v[0].get());
  v[0].reset(new int(32));
  std::unique_ptr<int> somePtr;
  v.insert(v.begin(), std::move(somePtr));
  EXPECT_EQ(*v[1], 32);
}

TEST(FBVector, task858056) {
  fbvector<fbstring> cycle;
  cycle.push_back("foo");
  cycle.push_back("bar");
  cycle.push_back("baz");
  fbstring message("Cycle detected: ");
  FOR_EACH_R (node_name, cycle) {
    message += "[";
    message += *node_name;
    message += "] ";
  }
  EXPECT_EQ("Cycle detected: [baz] [bar] [foo] ", message);
}

TEST(FBVector, move_iterator) {
  fbvector<int> base = { 0, 1, 2 };

  auto cp1 = base;
  fbvector<int> fbvi1(std::make_move_iterator(cp1.begin()),
                      std::make_move_iterator(cp1.end()));
  EXPECT_EQ(fbvi1, base);

  auto cp2 = base;
  fbvector<int> fbvi2;
  fbvi2.assign(std::make_move_iterator(cp2.begin()),
               std::make_move_iterator(cp2.end()));
  EXPECT_EQ(fbvi2, base);

  auto cp3 = base;
  fbvector<int> fbvi3;
  fbvi3.insert(fbvi3.end(),
               std::make_move_iterator(cp3.begin()),
               std::make_move_iterator(cp3.end()));
  EXPECT_EQ(fbvi3, base);
}

TEST(FBVector, reserve_consistency) {
  struct S { int64_t a, b, c, d; };

  fbvector<S> fb1;
  for (size_t i = 0; i < 1000; ++i) {
    fb1.reserve(1);
    EXPECT_EQ(fb1.size(), 0);
    fb1.shrink_to_fit();
  }
}

TEST(FBVector, vector_of_maps) {
  fbvector<std::map<std::string, std::string>> v;

  v.push_back(std::map<std::string, std::string>());
  v.push_back(std::map<std::string, std::string>());

  EXPECT_EQ(2, v.size());

  v[1]["hello"] = "world";
  EXPECT_EQ(0, v[0].size());
  EXPECT_EQ(1, v[1].size());

  v[0]["foo"] = "bar";
  EXPECT_EQ(1, v[0].size());
  EXPECT_EQ(1, v[1].size());
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  return RUN_ALL_TESTS();
}
