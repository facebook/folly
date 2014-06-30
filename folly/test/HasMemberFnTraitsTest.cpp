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

/*
 * @author: Marcelo Juchem <marcelo@fb.com>
 */

#include <folly/Traits.h>

#include <gtest/gtest.h>
#include <glog/logging.h>

#include <string>

using namespace std;
using namespace folly;

FOLLY_CREATE_HAS_MEMBER_FN_TRAITS(has_test, test);

struct Foo {
  int test();
  int test() const;
  string test(const string&) const;
};

struct Bar {
  int test();
  double test(int,long);
  long test(int) const;
};

struct Gaz {
  void test();
  void test() const;
  void test() volatile;
  void test() const volatile;
};

struct NoCV {
  void test();
};

struct Const {
  void test() const;
};

struct Volatile {
  void test() volatile;
};

struct CV {
  void test() const volatile;
};

bool log_value(const char* what, bool result) {
  LOG(INFO) << what << ": " << boolalpha << result;
  return result;
}

#define LOG_VALUE(x) log_value(#x, x)

TEST(HasMemberFnTraits, DirectMembers) {
  EXPECT_TRUE(LOG_VALUE((has_test<Foo, int()>::value)));
  EXPECT_TRUE(LOG_VALUE((has_test<Foo, int() const>::value)));
  EXPECT_FALSE(LOG_VALUE((has_test<Foo, double(int, long)>::value)));
  EXPECT_TRUE(LOG_VALUE((has_test<Foo, string(const string&) const>::value)));
  EXPECT_FALSE(LOG_VALUE((has_test<Foo, long(int) const>::value)));
  EXPECT_FALSE(LOG_VALUE((has_test<Foo, string(string) const>::value)));

  EXPECT_TRUE(LOG_VALUE((has_test<Bar, int()>::value)));
  EXPECT_FALSE(LOG_VALUE((has_test<Bar, int() const>::value)));
  EXPECT_TRUE(LOG_VALUE((has_test<Bar, double(int, long)>::value)));
  EXPECT_FALSE(LOG_VALUE((has_test<Bar, string(const string&) const>::value)));
  EXPECT_TRUE(LOG_VALUE((has_test<Bar, long(int) const>::value)));
  EXPECT_FALSE(LOG_VALUE((has_test<Bar, string(string) const>::value)));

  EXPECT_TRUE(LOG_VALUE((has_test<Gaz, void()>::value)));
  EXPECT_TRUE(LOG_VALUE((has_test<Gaz, void() const>::value)));
  EXPECT_TRUE(LOG_VALUE((has_test<Gaz, void() volatile>::value)));
  EXPECT_TRUE(LOG_VALUE((has_test<Gaz, void() const volatile>::value)));
  EXPECT_TRUE(LOG_VALUE((has_test<Gaz, void() volatile const>::value)));

  EXPECT_TRUE(LOG_VALUE((has_test<NoCV, void()>::value)));
  EXPECT_FALSE(LOG_VALUE((has_test<NoCV, void() const>::value)));
  EXPECT_FALSE(LOG_VALUE((has_test<NoCV, void() volatile>::value)));
  EXPECT_FALSE(LOG_VALUE((has_test<NoCV, void() const volatile>::value)));
  EXPECT_FALSE(LOG_VALUE((has_test<NoCV, void() volatile const>::value)));

  EXPECT_FALSE(LOG_VALUE((has_test<Const, void()>::value)));
  EXPECT_TRUE(LOG_VALUE((has_test<Const, void() const>::value)));
  EXPECT_FALSE(LOG_VALUE((has_test<Const, void() volatile>::value)));
  EXPECT_FALSE(LOG_VALUE((has_test<Const, void() const volatile>::value)));
  EXPECT_FALSE(LOG_VALUE((has_test<Const, void() volatile const>::value)));

  EXPECT_FALSE(LOG_VALUE((has_test<Volatile, void()>::value)));
  EXPECT_FALSE(LOG_VALUE((has_test<Volatile, void() const>::value)));
  EXPECT_TRUE(LOG_VALUE((has_test<Volatile, void() volatile>::value)));
  EXPECT_FALSE(LOG_VALUE((has_test<Volatile, void() const volatile>::value)));
  EXPECT_FALSE(LOG_VALUE((has_test<Volatile, void() volatile const>::value)));

  EXPECT_FALSE(LOG_VALUE((has_test<CV, void()>::value)));
  EXPECT_FALSE(LOG_VALUE((has_test<CV, void() const>::value)));
  EXPECT_FALSE(LOG_VALUE((has_test<CV, void() volatile>::value)));
  EXPECT_TRUE(LOG_VALUE((has_test<CV, void() const volatile>::value)));
  EXPECT_TRUE(LOG_VALUE((has_test<CV, void() volatile const>::value)));
}

int main(int argc, char *argv[]) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
