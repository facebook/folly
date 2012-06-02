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

//
// Author: andrei.alexandrescu@fb.com

#include "folly/FBString.h"

#include <list>
#include <fstream>
#include <boost/algorithm/string.hpp>
#include <boost/random.hpp>
#include <gtest/gtest.h>

#include <gflags/gflags.h>

#include "folly/Foreach.h"
#include "folly/Random.h"
#include "folly/Benchmark.h"

using namespace std;
using namespace folly;

static const int seed = folly::randomNumberSeed();
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
  str.resize(30, '\0');
  sprintf(&str[0], "%lu", static_cast<unsigned long>(n));
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

// void preventOptimization(void * p) {
//   return folly::preventOptimization((int)(long) p);
// }

////////////////////////////////////////////////////////////////////////////////
// Tests begin here
////////////////////////////////////////////////////////////////////////////////

template <class String> void clause_21_3_1_a(String & test) {
  test.String::~String();
  new(&test) String();
}
template <class String> void clause_21_3_1_b(String & test) {
  // Copy constructor
  const size_t pos = random(0, test.size());
  String s(test, pos, random(0, (size_t)(test.size() - pos)));
  test = s;
}
template <class String> void clause_21_3_1_c(String & test) {
  // Constructor from char*, size_t
  const size_t
    pos = random(0, test.size()),
    n = random(0, test.size() - pos);
  std::string before(test.data(), test.size());
  String s(test.c_str() + pos, n);
  std::string after(test.data(), test.size());
  EXPECT_EQ(before, after);

  // Constructor from char*, char*
  String s1(test.begin(), test.end());
  EXPECT_EQ(test, s1);
  String s2(test.data(), test.data() + test.size());
  EXPECT_EQ(test, s2);

  // Constructor from iterators
  std::list<char> lst;
  for (auto c : test) lst.push_back(c);
  String s3(lst.begin(), lst.end());
  EXPECT_EQ(test, s3);

  // Constructor from wchar_t iterators
  std::list<wchar_t> lst1;
  for (auto c : test) lst1.push_back(c);
  String s4(lst1.begin(), lst1.end());
  EXPECT_EQ(test, s4);

  // Constructor from wchar_t pointers
  wchar_t t[20];
  t[0] = 'a';
  t[1] = 'b';
  String s5(t, t + 2);;
  EXPECT_EQ("ab", s5);

  test = s;
}
template <class String> void clause_21_3_1_d(String & test) {
  // Assignment
  auto size = random(0, 2000);
  String s(size, '\0');
  EXPECT_EQ(s.size(), size);
  FOR_EACH_RANGE (i, 0, s.size()) {
    s[i] = random('a', 'z');
  }
  test = s;
}
template <class String> void clause_21_3_1_e(String & test) {
  // Assignment from char*
  String s(random(0, 1000), '\0');
  size_t i = 0;
  for (; i != s.size(); ++i) {
    s[i] = random('a', 'z');
  }
  test = s.c_str();
}
template <class String> void clause_21_3_1_f(String & test) {
  // Aliased assign
  const size_t pos = random(0, test.size());
  if (avoidAliasing) {
    test = String(test.c_str() + pos);
  } else {
    test = test.c_str() + pos;
  }
}
template <class String> void clause_21_3_1_g(String & test) {
  // Assignment from char
  test = random('a', 'z');
}

template <class String> void clause_21_3_2(String & test) {
  // Iterators. The code below should leave test unchanged
  EXPECT_EQ(test.size(), test.end() - test.begin());
  EXPECT_EQ(test.size(), test.rend() - test.rbegin());

  auto s = test.size();
  test.resize(test.end() - test.begin());
  EXPECT_EQ(s, test.size());
  test.resize(test.rend() - test.rbegin());
  EXPECT_EQ(s, test.size());
}

template <class String> void clause_21_3_3(String & test) {
  // exercise capacity, size, max_size
  EXPECT_EQ(test.size(), test.length());
  EXPECT_LE(test.size(), test.max_size());
  EXPECT_LE(test.capacity(), test.max_size());
  EXPECT_LE(test.size(), test.capacity());
  // exercise empty
  if (test.empty()) test = "empty";
  else test = "not empty";
}

template <class String> void clause_21_3_4(String & test) {
  // exercise element access 21.3.4
  if (!test.empty()) {
    auto const i = random(0, test.size() - 1);
    EXPECT_EQ(test[i], test.at(i));
    test = test[i];
  }
}

template <class String> void clause_21_3_5_a(String & test) {
  // 21.3.5 modifiers (+=)
  String test1;
  randomString(&test1);
  assert(test1.size() == strlen(test1.c_str()));
  auto len = test.size();
  test += test1;
  EXPECT_EQ(test.size(), test1.size() + len);
  FOR_EACH_RANGE (i, 0, test1.size()) {
    EXPECT_EQ(test[len + i], test1[i]);
  }
  // aliasing modifiers
  String test2 = test;
  auto dt = test2.data();
  auto sz = test.c_str();
  len = test.size();
  EXPECT_EQ(memcmp(sz, dt, len), 0);
  String copy(test.data(), test.size());
  EXPECT_EQ(strlen(test.c_str()), len);
  test += test;
  //test.append(test);
  EXPECT_EQ(test.size(), 2 * len);
  EXPECT_EQ(strlen(test.c_str()), 2 * len);
  FOR_EACH_RANGE (i, 0, len) {
    EXPECT_EQ(test[i], copy[i]);
    EXPECT_EQ(test[i], test[len + i]);
  }
  len = test.size();
  EXPECT_EQ(strlen(test.c_str()), len);
  // more aliasing
  auto const pos = random(0, test.size());
  EXPECT_EQ(strlen(test.c_str() + pos), len - pos);
  if (avoidAliasing) {
    String addMe(test.c_str() + pos);
    EXPECT_EQ(addMe.size(), len - pos);
    test += addMe;
  } else {
    test += test.c_str() + pos;
  }
  EXPECT_EQ(test.size(), 2 * len - pos);
  // single char
  len = test.size();
  test += random('a', 'z');
  EXPECT_EQ(test.size(), len + 1);
}

template <class String> void clause_21_3_5_b(String & test) {
  // 21.3.5 modifiers (append, push_back)
  String s;

  // Test with a small string first
  char c = random('a', 'z');
  s.push_back(c);
  EXPECT_EQ(s[s.size() - 1], c);
  EXPECT_EQ(s.size(), 1);
  s.resize(s.size() - 1);

  randomString(&s, maxString);
  test.append(s);
  randomString(&s, maxString);
  test.append(s, random(0, s.size()), random(0, maxString));
  randomString(&s, maxString);
  test.append(s.c_str(), random(0, s.size()));
  randomString(&s, maxString);
  test.append(s.c_str());
  test.append(random(0, maxString), random('a', 'z'));
  std::list<char> lst(RandomList(maxString));
  test.append(lst.begin(), lst.end());
  c = random('a', 'z');
  test.push_back(c);
  EXPECT_EQ(test[test.size() - 1], c);
}

template <class String> void clause_21_3_5_c(String & test) {
  // assign
  String s;
  randomString(&s);
  test.assign(s);
}

template <class String> void clause_21_3_5_d(String & test) {
  // assign
  String s;
  randomString(&s, maxString);
  test.assign(s, random(0, s.size()), random(0, maxString));
}

template <class String> void clause_21_3_5_e(String & test) {
  // assign
  String s;
  randomString(&s, maxString);
  test.assign(s.c_str(), random(0, s.size()));
}

template <class String> void clause_21_3_5_f(String & test) {
  // assign
  String s;
  randomString(&s, maxString);
  test.assign(s.c_str());
}

template <class String> void clause_21_3_5_g(String & test) {
  // assign
  String s;
  randomString(&s, maxString);
  test.assign(random(0, maxString), random('a', 'z'));
}

template <class String> void clause_21_3_5_h(String & test) {
  // assign from bidirectional iterator
  std::list<char> lst(RandomList(maxString));
  test.assign(lst.begin(), lst.end());
}

template <class String> void clause_21_3_5_i(String & test) {
  // assign from aliased source
  test.assign(test);
}

template <class String> void clause_21_3_5_j(String & test) {
  // assign from aliased source
  test.assign(test, random(0, test.size()), random(0, maxString));
}

template <class String> void clause_21_3_5_k(String & test) {
  // assign from aliased source
  test.assign(test.c_str(), random(0, test.size()));
}

template <class String> void clause_21_3_5_l(String & test) {
  // assign from aliased source
  test.assign(test.c_str());
}

template <class String> void clause_21_3_5_m(String & test) {
  // insert
  String s;
  randomString(&s, maxString);
  test.insert(random(0, test.size()), s);
  randomString(&s, maxString);
  test.insert(random(0, test.size()),
              s, random(0, s.size()),
              random(0, maxString));
  randomString(&s, maxString);
  test.insert(random(0, test.size()),
              s.c_str(), random(0, s.size()));
  randomString(&s, maxString);
  test.insert(random(0, test.size()), s.c_str());
  test.insert(random(0, test.size()),
              random(0, maxString), random('a', 'z'));
  test.insert(test.begin() + random(0, test.size()),
              random('a', 'z'));
  std::list<char> lst(RandomList(maxString));
  test.insert(test.begin() + random(0, test.size()),
              lst.begin(), lst.end());
}

template <class String> void clause_21_3_5_n(String & test) {
  // erase
  if (!test.empty()) {
    test.erase(random(0, test.size()), random(0, maxString));
  }
  if (!test.empty()) {
    // TODO: is erase(end()) allowed?
    test.erase(test.begin() + random(0, test.size() - 1));
  }
  if (!test.empty()) {
    auto const i = test.begin() + random(0, test.size());
    if (i != test.end()) {
      test.erase(i, i + random(0, size_t(test.end() - i)));
    }
  }
}

template <class String> void clause_21_3_5_o(String & test) {
  auto pos = random(0, test.size());
  if (avoidAliasing) {
    test.replace(pos, random(0, test.size() - pos),
                 String(test));
  } else {
    test.replace(pos, random(0, test.size() - pos), test);
  }
  pos = random(0, test.size());
  String s;
  randomString(&s, maxString);
  test.replace(pos, pos + random(0, test.size() - pos), s);
  auto pos1 = random(0, test.size());
  auto pos2 = random(0, test.size());
  if (avoidAliasing) {
    test.replace(pos1, pos1 + random(0, test.size() - pos1),
                 String(test),
                 pos2, pos2 + random(0, test.size() - pos2));
  } else {
    test.replace(pos1, pos1 + random(0, test.size() - pos1),
                 test, pos2, pos2 + random(0, test.size() - pos2));
  }
  pos1 = random(0, test.size());
  String str;
  randomString(&str, maxString);
  pos2 = random(0, str.size());
  test.replace(pos1, pos1 + random(0, test.size() - pos1),
               str, pos2, pos2 + random(0, str.size() - pos2));
  pos = random(0, test.size());
  if (avoidAliasing) {
    test.replace(pos, random(0, test.size() - pos),
                 String(test).c_str(), test.size());
  } else {
    test.replace(pos, random(0, test.size() - pos),
                 test.c_str(), test.size());
  }
  pos = random(0, test.size());
  randomString(&str, maxString);
  test.replace(pos, pos + random(0, test.size() - pos),
               str.c_str(), str.size());
  pos = random(0, test.size());
  randomString(&str, maxString);
  test.replace(pos, pos + random(0, test.size() - pos),
               str.c_str());
  pos = random(0, test.size());
  test.replace(pos, random(0, test.size() - pos),
               random(0, maxString), random('a', 'z'));
  pos = random(0, test.size());
  if (avoidAliasing) {
    test.replace(
      test.begin() + pos,
      test.begin() + pos + random(0, test.size() - pos),
      String(test));
  } else {
    test.replace(
      test.begin() + pos,
      test.begin() + pos + random(0, test.size() - pos),
      test);
  }
  pos = random(0, test.size());
  if (avoidAliasing) {
    test.replace(
      test.begin() + pos,
      test.begin() + pos + random(0, test.size() - pos),
      String(test).c_str(),
      test.size() - random(0, test.size()));
  } else {
    test.replace(
      test.begin() + pos,
      test.begin() + pos + random(0, test.size() - pos),
      test.c_str(),
      test.size() - random(0, test.size()));
  }
  pos = random(0, test.size());
  auto const n = random(0, test.size() - pos);
  typename String::iterator b = test.begin();
  String str1;
  randomString(&str1, maxString);
  const String & str3 = str1;
  const typename String::value_type* ss = str3.c_str();
  test.replace(
    b + pos,
    b + pos + n,
    ss);
  pos = random(0, test.size());
  test.replace(
    test.begin() + pos,
    test.begin() + pos + random(0, test.size() - pos),
    random(0, maxString), random('a', 'z'));
}

template <class String> void clause_21_3_5_p(String & test) {
  std::vector<typename String::value_type>
    vec(random(0, maxString));
  test.copy(
    &vec[0],
    vec.size(),
    random(0, test.size()));
}

template <class String> void clause_21_3_5_q(String & test) {
  String s;
  randomString(&s, maxString);
  s.swap(test);
}

template <class String> void clause_21_3_6_a(String & test) {
  // 21.3.6 string operations
  // exercise c_str() and data()
  assert(test.c_str() == test.data());
  // exercise get_allocator()
  String s;
  randomString(&s, maxString);
  assert(test.get_allocator() == s.get_allocator());
}

template <class String> void clause_21_3_6_b(String & test) {
  String str = test.substr(
    random(0, test.size()),
    random(0, test.size()));
  Num2String(test, test.find(str, random(0, test.size())));
}

template <class String> void clause_21_3_6_c(String & test) {
  auto from = random(0, test.size());
  auto length = random(0, test.size() - from);
  String str = test.substr(from, length);
  Num2String(test, test.find(str.c_str(),
                             random(0, test.size()),
                             random(0, str.size())));
}

template <class String> void clause_21_3_6_d(String & test) {
  String str = test.substr(
    random(0, test.size()),
    random(0, test.size()));
  Num2String(test, test.find(str.c_str(),
                             random(0, test.size())));
}

template <class String> void clause_21_3_6_e(String & test) {
  Num2String(test, test.find(
               random('a', 'z'),
               random(0, test.size())));
}

template <class String> void clause_21_3_6_f(String & test) {
  String str = test.substr(
    random(0, test.size()),
    random(0, test.size()));
  Num2String(test, test.rfind(str, random(0, test.size())));
}

template <class String> void clause_21_3_6_g(String & test) {
  String str = test.substr(
    random(0, test.size()),
    random(0, test.size()));
  Num2String(test, test.rfind(str.c_str(),
                              random(0, test.size()),
                              random(0, str.size())));
}

template <class String> void clause_21_3_6_h(String & test) {
  String str = test.substr(
    random(0, test.size()),
    random(0, test.size()));
  Num2String(test, test.rfind(str.c_str(),
                              random(0, test.size())));
}

template <class String> void clause_21_3_6_i(String & test) {
  Num2String(test, test.rfind(
               random('a', 'z'),
               random(0, test.size())));
}

template <class String> void clause_21_3_6_j(String & test) {
  String str;
  randomString(&str, maxString);
  Num2String(test, test.find_first_of(str,
                                      random(0, test.size())));
}

template <class String> void clause_21_3_6_k(String & test) {
  String str;
  randomString(&str, maxString);
  Num2String(test, test.find_first_of(str.c_str(),
                                      random(0, test.size()),
                                      random(0, str.size())));
}

template <class String> void clause_21_3_6_l(String & test) {
  String str;
  randomString(&str, maxString);
  Num2String(test, test.find_first_of(str.c_str(),
                                      random(0, test.size())));
}

template <class String> void clause_21_3_6_m(String & test) {
  Num2String(test, test.find_first_of(
               random('a', 'z'),
               random(0, test.size())));
}

template <class String> void clause_21_3_6_n(String & test) {
  String str;
  randomString(&str, maxString);
  Num2String(test, test.find_last_of(str,
                                     random(0, test.size())));
}

template <class String> void clause_21_3_6_o(String & test) {
  String str;
  randomString(&str, maxString);
  Num2String(test, test.find_last_of(str.c_str(),
                                     random(0, test.size()),
                                     random(0, str.size())));
}

template <class String> void clause_21_3_6_p(String & test) {
  String str;
  randomString(&str, maxString);
  Num2String(test, test.find_last_of(str.c_str(),
                                     random(0, test.size())));
}

template <class String> void clause_21_3_6_q(String & test) {
  Num2String(test, test.find_last_of(
               random('a', 'z'),
               random(0, test.size())));
}

template <class String> void clause_21_3_6_r(String & test) {
  String str;
  randomString(&str, maxString);
  Num2String(test, test.find_first_not_of(str,
                                          random(0, test.size())));
}

template <class String> void clause_21_3_6_s(String & test) {
  String str;
  randomString(&str, maxString);
  Num2String(test, test.find_first_not_of(str.c_str(),
                                          random(0, test.size()),
                                          random(0, str.size())));
}

template <class String> void clause_21_3_6_t(String & test) {
  String str;
  randomString(&str, maxString);
  Num2String(test, test.find_first_not_of(str.c_str(),
                                          random(0, test.size())));
}

template <class String> void clause_21_3_6_u(String & test) {
  Num2String(test, test.find_first_not_of(
               random('a', 'z'),
               random(0, test.size())));
}

template <class String> void clause_21_3_6_v(String & test) {
  String str;
  randomString(&str, maxString);
  Num2String(test, test.find_last_not_of(str,
                                         random(0, test.size())));
}

template <class String> void clause_21_3_6_w(String & test) {
  String str;
  randomString(&str, maxString);
  Num2String(test, test.find_last_not_of(str.c_str(),
                                         random(0, test.size()),
                                         random(0, str.size())));
}

template <class String> void clause_21_3_6_x(String & test) {
  String str;
  randomString(&str, maxString);
  Num2String(test, test.find_last_not_of(str.c_str(),
                                         random(0, test.size())));
}

template <class String> void clause_21_3_6_y(String & test) {
  Num2String(test, test.find_last_not_of(
               random('a', 'z'),
               random(0, test.size())));
}

template <class String> void clause_21_3_6_z(String & test) {
  test = test.substr(random(0, test.size()), random(0, test.size()));
}

template <class String> void clause_21_3_7_a(String & test) {
  String s;
  randomString(&s, maxString);
  int tristate = test.compare(s);
  if (tristate > 0) tristate = 1;
  else if (tristate < 0) tristate = 2;
  Num2String(test, tristate);
}

template <class String> void clause_21_3_7_b(String & test) {
  String s;
  randomString(&s, maxString);
  int tristate = test.compare(
    random(0, test.size()),
    random(0, test.size()),
    s);
  if (tristate > 0) tristate = 1;
  else if (tristate < 0) tristate = 2;
  Num2String(test, tristate);
}

template <class String> void clause_21_3_7_c(String & test) {
  String str;
  randomString(&str, maxString);
  int tristate = test.compare(
    random(0, test.size()),
    random(0, test.size()),
    str,
    random(0, str.size()),
    random(0, str.size()));
  if (tristate > 0) tristate = 1;
  else if (tristate < 0) tristate = 2;
  Num2String(test, tristate);
}

template <class String> void clause_21_3_7_d(String & test) {
  String s;
  randomString(&s, maxString);
  int tristate = test.compare(s.c_str());
  if (tristate > 0) tristate = 1;
  else if (tristate < 0) tristate = 2;
                Num2String(test, tristate);
}

template <class String> void clause_21_3_7_e(String & test) {
  String str;
  randomString(&str, maxString);
  int tristate = test.compare(
    random(0, test.size()),
    random(0, test.size()),
    str.c_str(),
    random(0, str.size()));
  if (tristate > 0) tristate = 1;
  else if (tristate < 0) tristate = 2;
  Num2String(test, tristate);
}

template <class String> void clause_21_3_7_f(String & test) {
  String s1;
  randomString(&s1, maxString);
  String s2;
  randomString(&s2, maxString);
  test = s1 + s2;
}

template <class String> void clause_21_3_7_g(String & test) {
  String s;
  randomString(&s, maxString);
  String s1;
  randomString(&s1, maxString);
  test = s.c_str() + s1;
}

template <class String> void clause_21_3_7_h(String & test) {
  String s;
  randomString(&s, maxString);
  test = typename String::value_type(random('a', 'z')) + s;
}

template <class String> void clause_21_3_7_i(String & test) {
  String s;
  randomString(&s, maxString);
  String s1;
  randomString(&s1, maxString);
  test = s + s1.c_str();
}

template <class String> void clause_21_3_7_j(String & test) {
  String s;
  randomString(&s, maxString);
  String s1;
  randomString(&s1, maxString);
  test = s + s1.c_str();
}

template <class String> void clause_21_3_7_k(String & test) {
  String s;
  randomString(&s, maxString);
  test = s + typename String::value_type(random('a', 'z'));
}

// Numbering here is from C++11
template <class String> void clause_21_4_8_9_a(String & test) {
  stringstream s("asd asdfjhuhdf    asdfasdf\tasdsdf");
  String str;
  while (s) {
    s >> str;
    test += str + test;
  }
}

TEST(FBString, testAllClauses) {
  EXPECT_TRUE(1) << "Starting with seed: " << seed;
  std::string r;
  folly::fbstring c;
#define TEST_CLAUSE(x)                                              \
  do {                                                              \
      if (1) {} else EXPECT_TRUE(1) << "Testing clause " << #x;     \
      randomString(&r);                                             \
      c = r;                                                        \
      EXPECT_EQ(c, r);                                              \
      auto localSeed = seed + count;                                \
      rng = RandomT(localSeed);                                     \
      clause_##x(r);                                                \
      rng = RandomT(localSeed);                                     \
      clause_##x(c);                                                \
      EXPECT_EQ(r, c)                                               \
        << "Lengths: " << r.size() << " vs. " << c.size()           \
        << "\nReference: '" << r << "'"                             \
        << "\nActual:    '" << c.data()[0] << "'";                  \
    } while (++count % 100 != 0)

  int count = 0;
  TEST_CLAUSE(21_3_1_a);
  TEST_CLAUSE(21_3_1_b);
  TEST_CLAUSE(21_3_1_c);
  TEST_CLAUSE(21_3_1_d);
  TEST_CLAUSE(21_3_1_e);
  TEST_CLAUSE(21_3_1_f);
  TEST_CLAUSE(21_3_1_g);

  TEST_CLAUSE(21_3_2);
  TEST_CLAUSE(21_3_3);
  TEST_CLAUSE(21_3_4);
  TEST_CLAUSE(21_3_5_a);
  TEST_CLAUSE(21_3_5_b);
  TEST_CLAUSE(21_3_5_c);
  TEST_CLAUSE(21_3_5_d);
  TEST_CLAUSE(21_3_5_e);
  TEST_CLAUSE(21_3_5_f);
  TEST_CLAUSE(21_3_5_g);
  TEST_CLAUSE(21_3_5_h);
  TEST_CLAUSE(21_3_5_i);
  TEST_CLAUSE(21_3_5_j);
  TEST_CLAUSE(21_3_5_k);
  TEST_CLAUSE(21_3_5_l);
  TEST_CLAUSE(21_3_5_m);
  TEST_CLAUSE(21_3_5_n);
  TEST_CLAUSE(21_3_5_o);
  TEST_CLAUSE(21_3_5_p);

  TEST_CLAUSE(21_3_6_a);
  TEST_CLAUSE(21_3_6_b);
  TEST_CLAUSE(21_3_6_c);
  TEST_CLAUSE(21_3_6_d);
  TEST_CLAUSE(21_3_6_e);
  TEST_CLAUSE(21_3_6_f);
  TEST_CLAUSE(21_3_6_g);
  TEST_CLAUSE(21_3_6_h);
  TEST_CLAUSE(21_3_6_i);
  TEST_CLAUSE(21_3_6_j);
  TEST_CLAUSE(21_3_6_k);
  TEST_CLAUSE(21_3_6_l);
  TEST_CLAUSE(21_3_6_m);
  TEST_CLAUSE(21_3_6_n);
  TEST_CLAUSE(21_3_6_o);
  TEST_CLAUSE(21_3_6_p);
  TEST_CLAUSE(21_3_6_q);
  TEST_CLAUSE(21_3_6_r);
  TEST_CLAUSE(21_3_6_s);
  TEST_CLAUSE(21_3_6_t);
  TEST_CLAUSE(21_3_6_u);
  TEST_CLAUSE(21_3_6_v);
  TEST_CLAUSE(21_3_6_w);
  TEST_CLAUSE(21_3_6_x);
  TEST_CLAUSE(21_3_6_y);
  TEST_CLAUSE(21_3_6_z);

  TEST_CLAUSE(21_3_7_a);
  TEST_CLAUSE(21_3_7_b);
  TEST_CLAUSE(21_3_7_c);
  TEST_CLAUSE(21_3_7_d);
  TEST_CLAUSE(21_3_7_e);
  TEST_CLAUSE(21_3_7_f);
  TEST_CLAUSE(21_3_7_g);
  TEST_CLAUSE(21_3_7_h);
  TEST_CLAUSE(21_3_7_i);
  TEST_CLAUSE(21_3_7_j);
  TEST_CLAUSE(21_3_7_k);

  TEST_CLAUSE(21_4_8_9_a);
}

TEST(FBString, testGetline) {
  fbstring s1 = "\
Lorem ipsum dolor sit amet, consectetur adipiscing elit. Cras accumsan \n\
elit ut urna consectetur in sagittis mi auctor. Nulla facilisi. In nec \n\
dolor leo, vitae imperdiet neque. Donec ut erat mauris, a faucibus \n\
elit. Integer consectetur gravida augue, sit amet mattis mauris auctor \n\
sed. Morbi congue libero eu nunc sodales adipiscing. In lectus nunc, \n\
vulputate a fringilla at, venenatis quis justo. Proin eu velit \n\
nibh. Maecenas vitae tellus eros. Pellentesque habitant morbi \n\
tristique senectus et netus et malesuada fames ac turpis \n\
egestas. Vivamus faucibus feugiat consequat. Donec fermentum neque sit \n\
amet ligula suscipit porta. Phasellus facilisis felis in purus luctus \n\
quis posuere leo tempor. Nam nunc purus, luctus a pharetra ut, \n\
placerat at dui. Donec imperdiet, diam quis convallis pulvinar, dui \n\
est commodo lorem, ut tincidunt diam nibh et nibh. Maecenas nec velit \n\
massa, ut accumsan magna. Donec imperdiet tempor nisi et \n\
laoreet. Phasellus lectus quam, ultricies ut tincidunt in, dignissim \n\
id eros. Mauris vulputate tortor nec neque pellentesque sagittis quis \n\
sed nisl. In diam lacus, lobortis ut posuere nec, ornare id quam.";
  const char* f = "/tmp/fbstring_testing";
  {
    std::ofstream out(f);
    if (!(out << s1)) {
      EXPECT_TRUE(0) << "Couldn't write to temp file.";
      return;
    }
  }
  vector<fbstring> v;
  boost::split(v, s1, boost::is_any_of("\n"));
  ifstream input(f);
  fbstring line;
  FOR_EACH (i, v) {
    EXPECT_TRUE(getline(input, line));
    EXPECT_EQ(line, *i);
  }
}

TEST(FBString, testMoveCtor) {
  // Move constructor. Make sure we allocate a large string, so the
  // small string optimization doesn't kick in.
  auto size = random(100, 2000);
  fbstring s(size, 'a');
  fbstring test = std::move(s);
  EXPECT_TRUE(s.empty());
  EXPECT_EQ(size, test.size());
}

TEST(FBString, testMoveAssign) {
  // Move constructor. Make sure we allocate a large string, so the
  // small string optimization doesn't kick in.
  auto size = random(100, 2000);
  fbstring s(size, 'a');
  fbstring test;
  test = std::move(s);
  EXPECT_TRUE(s.empty());
  EXPECT_EQ(size, test.size());
}

TEST(FBString, testMoveOperatorPlusLhs) {
  // Make sure we allocate a large string, so the
  // small string optimization doesn't kick in.
  auto size1 = random(100, 2000);
  auto size2 = random(100, 2000);
  fbstring s1(size1, 'a');
  fbstring s2(size2, 'b');
  fbstring test;
  test = std::move(s1) + s2;
  EXPECT_TRUE(s1.empty());
  EXPECT_EQ(size1 + size2, test.size());
}

TEST(FBString, testMoveOperatorPlusRhs) {
  // Make sure we allocate a large string, so the
  // small string optimization doesn't kick in.
  auto size1 = random(100, 2000);
  auto size2 = random(100, 2000);
  fbstring s1(size1, 'a');
  fbstring s2(size2, 'b');
  fbstring test;
  test = s1 + std::move(s2);
  EXPECT_EQ(size1 + size2, test.size());
}

TEST(FBString, testConstructionFromLiteralZero) {
  try {
    std::string s(0);
    EXPECT_TRUE(false);
  } catch (const std::logic_error&) {
  } catch (...) {
    EXPECT_TRUE(false);
  }

  try {
    fbstring s(0);
    EXPECT_TRUE(false);
  } catch (const std::logic_error& e) {
  } catch (...) {
    EXPECT_TRUE(false);
  }
}

TEST(FBString, testFixedBugs) {
  { // D479397
    fbstring str(1337, 'f');
    fbstring cp = str;
    cp.clear();
    cp.c_str();
    EXPECT_EQ(str.front(), 'f');
  }
  { // D481173, --extra-cxxflags=-DFBSTRING_CONSERVATIVE
    fbstring str(1337, 'f');
    for (int i = 0; i < 2; ++i) {
      fbstring cp = str;
      cp[1] = 'b';
      EXPECT_EQ(cp.c_str()[cp.size()], '\0');
      cp.push_back('?');
    }
  }
}

#define CONCAT(A, B) CONCAT_HELPER(A, B)
#define CONCAT_HELPER(A, B) A##B
#define BENCHFUN(F) CONCAT(CONCAT(BM_, F), CONCAT(_, STRING))

#define STRING string
#include "folly/test/FBStringTestBenchmarks.cpp.h"
#undef STRING
#define STRING fbstring
#include "folly/test/FBStringTestBenchmarks.cpp.h"
#undef STRING

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);
  auto ret = RUN_ALL_TESTS();
  if (!ret && FLAGS_benchmark) {
    folly::runBenchmarks();
  }

  return ret;
}

/*
malloc

BENCHFUN(defaultCtor)                  100000  1.426 s   14.26 us  68.47 k
BM_copyCtor_string/32k                 100000  63.48 ms  634.8 ns  1.502 M
BM_ctorFromArray_string/32k            100000  303.3 ms  3.033 us  321.9 k
BM_ctorFromChar_string/1M              100000  9.915 ms  99.15 ns  9.619 M
BM_assignmentOp_string/256             100000  69.09 ms  690.9 ns   1.38 M
BENCHFUN(assignmentFill)               100000  1.775 ms  17.75 ns  53.73 M
BM_resize_string/512k                  100000  1.667 s   16.67 us  58.58 k
BM_findSuccessful_string/512k          100000  287.3 ms  2.873 us  339.9 k
BM_findUnsuccessful_string/512k        100000  320.3 ms  3.203 us  304.9 k
BM_replace_string/256                  100000  69.68 ms  696.8 ns  1.369 M
BM_push_back_string/1k                 100000  433.1 ms  4.331 us  225.5 k

BENCHFUN(defaultCtor)                  100000  1.086 s   10.86 us  89.91 k
BM_copyCtor_fbstring/32k               100000  4.218 ms  42.18 ns  22.61 M
BM_ctorFromArray_fbstring/32k          100000  145.2 ms  1.452 us  672.7 k
BM_ctorFromChar_fbstring/1M            100000   9.21 ms   92.1 ns  10.35 M
BM_assignmentOp_fbstring/256           100000  61.95 ms  619.5 ns   1.54 M
BENCHFUN(assignmentFill)               100000   1.41 ms   14.1 ns  67.64 M
BM_resize_fbstring/512k                100000  1.668 s   16.68 us  58.56 k
BM_findSuccessful_fbstring/512k        100000   20.6 ms    206 ns  4.629 M
BM_findUnsuccessful_fbstring/512k      100000  141.3 ms  1.413 us  691.1 k
BM_replace_fbstring/256                100000  77.12 ms  771.2 ns  1.237 M
BM_push_back_fbstring/1k               100000  1.745 s   17.45 us  55.95 k

jemalloc

BENCHFUN(defaultCtor)                  100000  1.426 s   14.26 us   68.5 k
BM_copyCtor_string/32k                 100000  275.7 ms  2.757 us  354.2 k
BM_ctorFromArray_string/32k            100000    270 ms    2.7 us  361.7 k
BM_ctorFromChar_string/1M              100000  10.36 ms  103.6 ns  9.206 M
BM_assignmentOp_string/256             100000  70.44 ms  704.3 ns  1.354 M
BENCHFUN(assignmentFill)               100000  1.766 ms  17.66 ns     54 M
BM_resize_string/512k                  100000  1.675 s   16.75 us  58.29 k
BM_findSuccessful_string/512k          100000  90.89 ms  908.9 ns  1.049 M
BM_findUnsuccessful_string/512k        100000  315.1 ms  3.151 us  309.9 k
BM_replace_string/256                  100000  71.14 ms  711.4 ns  1.341 M
BM_push_back_string/1k                 100000  425.1 ms  4.251 us  229.7 k

BENCHFUN(defaultCtor)                  100000  1.082 s   10.82 us  90.23 k
BM_copyCtor_fbstring/32k               100000  4.213 ms  42.13 ns  22.64 M
BM_ctorFromArray_fbstring/32k          100000  113.2 ms  1.132 us    863 k
BM_ctorFromChar_fbstring/1M            100000  9.162 ms  91.62 ns  10.41 M
BM_assignmentOp_fbstring/256           100000  61.34 ms  613.4 ns  1.555 M
BENCHFUN(assignmentFill)               100000  1.408 ms  14.08 ns  67.73 M
BM_resize_fbstring/512k                100000  1.671 s   16.71 us  58.43 k
BM_findSuccessful_fbstring/512k        100000  8.723 ms  87.23 ns  10.93 M
BM_findUnsuccessful_fbstring/512k      100000  141.3 ms  1.413 us  691.2 k
BM_replace_fbstring/256                100000  77.83 ms  778.3 ns  1.225 M
BM_push_back_fbstring/1k               100000  1.744 s   17.44 us  55.99 k
*/
