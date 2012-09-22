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

#include "folly/Optional.h"

#include <memory>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <string>

#include <glog/logging.h>
#include <gtest/gtest.h>
#include <boost/optional.hpp>

using namespace folly;
using std::unique_ptr;
using std::shared_ptr;

struct NoDefault {
  NoDefault(int, int) {}
  char a, b, c;
};

static_assert(sizeof(Optional<char>) == 2, "");
static_assert(sizeof(Optional<int>) == 8, "");
static_assert(sizeof(Optional<NoDefault>) == 4, "");
static_assert(sizeof(Optional<char>) == sizeof(boost::optional<char>), "");
static_assert(sizeof(Optional<short>) == sizeof(boost::optional<short>), "");
static_assert(sizeof(Optional<int>) == sizeof(boost::optional<int>), "");
static_assert(sizeof(Optional<double>) == sizeof(boost::optional<double>), "");

TEST(Optional, NoDefault) {
  Optional<NoDefault> x;
  EXPECT_FALSE(x);
  x.emplace(4, 5);
  EXPECT_TRUE(x);
  x.clear();
  EXPECT_FALSE(x);
}

TEST(Optional, String) {
  Optional<std::string> maybeString;
  EXPECT_FALSE(maybeString);
  maybeString = "hello";
  EXPECT_TRUE(maybeString);
}

TEST(Optional, Const) {
  { // default construct
    Optional<const int> opt;
    EXPECT_FALSE(opt);
    opt.emplace(4);
    EXPECT_EQ(opt, 4);
    opt.emplace(5);
    EXPECT_EQ(opt, 5);
    opt.clear();
    EXPECT_FALSE(opt);
  }
  { // copy-constructed
    const int x = 6;
    Optional<const int> opt(x);
    EXPECT_EQ(opt, 6);
  }
  { // move-constructed
    const int x = 7;
    Optional<const int> opt(std::move(x));
    EXPECT_EQ(opt, 7);
  }
  // no assignment allowed
}

TEST(Optional, Simple) {
  Optional<int> opt;
  EXPECT_FALSE(opt);
  opt = 4;
  EXPECT_TRUE(opt);
  EXPECT_EQ(4, *opt);
  opt = 5;
  EXPECT_EQ(5, *opt);
  opt.clear();
  EXPECT_FALSE(opt);
}

TEST(Optional, Unique) {
  Optional<unique_ptr<int>> opt;

  opt.clear();
  EXPECT_FALSE(opt);
  // empty->emplaced
  opt.emplace(new int(5));
  EXPECT_TRUE(opt);
  EXPECT_EQ(5, **opt);

  opt.clear();
  // empty->moved
  opt = unique_ptr<int>(new int(6));
  EXPECT_EQ(6, **opt);
  // full->moved
  opt = unique_ptr<int>(new int(7));
  EXPECT_EQ(7, **opt);

  // move it out by move construct
  Optional<unique_ptr<int>> moved(std::move(opt));
  EXPECT_TRUE(moved);
  EXPECT_FALSE(opt);
  EXPECT_EQ(7, **moved);

  EXPECT_TRUE(moved);
  opt = std::move(moved); // move it back by move assign
  EXPECT_FALSE(moved);
  EXPECT_TRUE(opt);
  EXPECT_EQ(7, **opt);
}

TEST(Optional, Shared) {
  shared_ptr<int> ptr;
  Optional<shared_ptr<int>> opt;
  EXPECT_FALSE(opt);
  // empty->emplaced
  opt.emplace(new int(5));
  EXPECT_TRUE(opt);
  ptr = opt.value();
  EXPECT_EQ(ptr.get(), opt->get());
  EXPECT_EQ(2, ptr.use_count());
  opt.clear();
  EXPECT_EQ(1, ptr.use_count());
  // full->copied
  opt = ptr;
  EXPECT_EQ(2, ptr.use_count());
  EXPECT_EQ(ptr.get(), opt->get());
  opt.clear();
  EXPECT_EQ(1, ptr.use_count());
  // full->moved
  opt = std::move(ptr);
  EXPECT_EQ(1, opt->use_count());
  EXPECT_EQ(nullptr, ptr.get());
  {
    Optional<shared_ptr<int>> copied(opt);
    EXPECT_EQ(2, opt->use_count());
    Optional<shared_ptr<int>> moved(std::move(opt));
    EXPECT_EQ(2, moved->use_count());
    moved.emplace(new int(6));
    EXPECT_EQ(1, moved->use_count());
    copied = moved;
    EXPECT_EQ(2, moved->use_count());
  }
}

TEST(Optional, Order) {
  std::vector<Optional<int>> vect{
    { none },
    { 3 },
    { 1 },
    { none },
    { 2 },
  };
  std::vector<Optional<int>> expected {
    { none },
    { none },
    { 1 },
    { 2 },
    { 3 },
  };
  std::sort(vect.begin(), vect.end());
  EXPECT_TRUE(vect == expected);
}

TEST(Optional, Swap) {
  Optional<std::string> a;
  Optional<std::string> b;

  swap(a, b);
  EXPECT_FALSE(a.hasValue());
  EXPECT_FALSE(b.hasValue());

  a = "hello";
  EXPECT_TRUE(a.hasValue());
  EXPECT_FALSE(b.hasValue());
  EXPECT_EQ("hello", a.value());

  swap(a, b);
  EXPECT_FALSE(a.hasValue());
  EXPECT_TRUE(b.hasValue());
  EXPECT_EQ("hello", b.value());

  a = "bye";
  EXPECT_TRUE(a.hasValue());
  EXPECT_EQ("bye", a.value());

  swap(a, b);
}

TEST(Optional, Comparisons) {
  Optional<int> o_;
  Optional<int> o1(1);
  Optional<int> o2(2);

  EXPECT_TRUE(o_ < 1);
  EXPECT_TRUE(o_ <= 1);
  EXPECT_TRUE(o_ <= o_);
  EXPECT_TRUE(o_ == o_);
  EXPECT_TRUE(o_ != 1);
  EXPECT_TRUE(o_ >= o_);
  EXPECT_TRUE(1 >= o_);
  EXPECT_TRUE(1 > o_);

  EXPECT_TRUE(o1 < o2);
  EXPECT_TRUE(o1 <= o2);
  EXPECT_TRUE(o1 <= o1);
  EXPECT_TRUE(o1 == o1);
  EXPECT_TRUE(o1 != o2);
  EXPECT_TRUE(o1 >= o1);
  EXPECT_TRUE(o2 >= o1);
  EXPECT_TRUE(o2 > o1);

  EXPECT_FALSE(o2 < o1);
  EXPECT_FALSE(o2 <= o1);
  EXPECT_FALSE(o2 <= o1);
  EXPECT_FALSE(o2 == o1);
  EXPECT_FALSE(o1 != o1);
  EXPECT_FALSE(o1 >= o2);
  EXPECT_FALSE(o1 >= o2);
  EXPECT_FALSE(o1 > o2);

  EXPECT_TRUE(1 < o2);
  EXPECT_TRUE(1 <= o2);
  EXPECT_TRUE(1 <= o1);
  EXPECT_TRUE(1 == o1);
  EXPECT_TRUE(2 != o1);
  EXPECT_TRUE(1 >= o1);
  EXPECT_TRUE(2 >= o1);
  EXPECT_TRUE(2 > o1);

  EXPECT_FALSE(o2 < 1);
  EXPECT_FALSE(o2 <= 1);
  EXPECT_FALSE(o2 <= 1);
  EXPECT_FALSE(o2 == 1);
  EXPECT_FALSE(o2 != 2);
  EXPECT_FALSE(o1 >= 2);
  EXPECT_FALSE(o1 >= 2);
  EXPECT_FALSE(o1 > 2);
}

TEST(Optional, Pointee) {
  Optional<int> x;
  EXPECT_FALSE(get_pointer(x));
  x = 1;
  EXPECT_TRUE(get_pointer(x));
  *get_pointer(x) = 2;
  EXPECT_TRUE(x == 2);
  x = none;
  EXPECT_FALSE(get_pointer(x));
}
