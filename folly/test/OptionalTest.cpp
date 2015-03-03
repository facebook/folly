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

#include <folly/Optional.h>

#include <memory>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <string>

#include <glog/logging.h>
#include <gtest/gtest.h>
#include <boost/optional.hpp>

using std::unique_ptr;
using std::shared_ptr;

namespace folly {

template<class V>
std::ostream& operator<<(std::ostream& os, const Optional<V>& v) {
  if (v) {
    os << "Optional(" << v.value() << ')';
  } else {
    os << "None";
  }
  return os;
}

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
  EXPECT_TRUE(bool(x));
  x.clear();
  EXPECT_FALSE(x);
}

TEST(Optional, String) {
  Optional<std::string> maybeString;
  EXPECT_FALSE(maybeString);
  maybeString = "hello";
  EXPECT_TRUE(bool(maybeString));
}

TEST(Optional, Const) {
  { // default construct
    Optional<const int> opt;
    EXPECT_FALSE(bool(opt));
    opt.emplace(4);
    EXPECT_EQ(*opt, 4);
    opt.emplace(5);
    EXPECT_EQ(*opt, 5);
    opt.clear();
    EXPECT_FALSE(bool(opt));
  }
  { // copy-constructed
    const int x = 6;
    Optional<const int> opt(x);
    EXPECT_EQ(*opt, 6);
  }
  { // move-constructed
    const int x = 7;
    Optional<const int> opt(std::move(x));
    EXPECT_EQ(*opt, 7);
  }
  // no assignment allowed
}

TEST(Optional, Simple) {
  Optional<int> opt;
  EXPECT_FALSE(bool(opt));
  EXPECT_EQ(42, opt.value_or(42));
  opt = 4;
  EXPECT_TRUE(bool(opt));
  EXPECT_EQ(4, *opt);
  EXPECT_EQ(4, opt.value_or(42));
  opt = 5;
  EXPECT_EQ(5, *opt);
  opt.clear();
  EXPECT_FALSE(bool(opt));
}

class MoveTester {
public:
  /* implicit */ MoveTester(const char* s) : s_(s) {}
  MoveTester(const MoveTester&) = default;
  MoveTester(MoveTester&& other) noexcept {
    s_ = std::move(other.s_);
    other.s_ = "";
  }
  MoveTester& operator=(const MoveTester&) = default;
  MoveTester& operator=(MoveTester&&) = default;
private:
  friend bool operator==(const MoveTester& o1, const MoveTester& o2);
  std::string s_;
};

bool operator==(const MoveTester& o1, const MoveTester& o2) {
  return o1.s_ == o2.s_;
}

TEST(Optional, value_or_rvalue_arg) {
  Optional<MoveTester> opt;
  MoveTester dflt = "hello";
  EXPECT_EQ("hello", opt.value_or(dflt));
  EXPECT_EQ("hello", dflt);
  EXPECT_EQ("hello", opt.value_or(std::move(dflt)));
  EXPECT_EQ("", dflt);
  EXPECT_EQ("world", opt.value_or("world"));

  dflt = "hello";
  // Make sure that the const overload works on const objects
  const auto& optc = opt;
  EXPECT_EQ("hello", optc.value_or(dflt));
  EXPECT_EQ("hello", dflt);
  EXPECT_EQ("hello", optc.value_or(std::move(dflt)));
  EXPECT_EQ("", dflt);
  EXPECT_EQ("world", optc.value_or("world"));

  dflt = "hello";
  opt = "meow";
  EXPECT_EQ("meow", opt.value_or(dflt));
  EXPECT_EQ("hello", dflt);
  EXPECT_EQ("meow", opt.value_or(std::move(dflt)));
  EXPECT_EQ("hello", dflt);  // only moved if used
}

TEST(Optional, value_or_noncopyable) {
  Optional<std::unique_ptr<int>> opt;
  std::unique_ptr<int> dflt(new int(42));
  EXPECT_EQ(42, *std::move(opt).value_or(std::move(dflt)));
}

TEST(Optional, EmptyConstruct) {
  Optional<int> opt;
  EXPECT_FALSE(bool(opt));
  Optional<int> test1(opt);
  EXPECT_FALSE(bool(test1));
  Optional<int> test2(std::move(opt));
  EXPECT_FALSE(bool(test2));
}

TEST(Optional, Unique) {
  Optional<unique_ptr<int>> opt;

  opt.clear();
  EXPECT_FALSE(bool(opt));
  // empty->emplaced
  opt.emplace(new int(5));
  EXPECT_TRUE(bool(opt));
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
  EXPECT_TRUE(bool(moved));
  EXPECT_FALSE(bool(opt));
  EXPECT_EQ(7, **moved);

  EXPECT_TRUE(bool(moved));
  opt = std::move(moved); // move it back by move assign
  EXPECT_FALSE(bool(moved));
  EXPECT_TRUE(bool(opt));
  EXPECT_EQ(7, **opt);
}

TEST(Optional, Shared) {
  shared_ptr<int> ptr;
  Optional<shared_ptr<int>> opt;
  EXPECT_FALSE(bool(opt));
  // empty->emplaced
  opt.emplace(new int(5));
  EXPECT_TRUE(bool(opt));
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
  EXPECT_EQ(vect, expected);
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

  EXPECT_TRUE(o_ <= (o_));
  EXPECT_TRUE(o_ == (o_));
  EXPECT_TRUE(o_ >= (o_));

  EXPECT_TRUE(o1 < o2);
  EXPECT_TRUE(o1 <= o2);
  EXPECT_TRUE(o1 <= (o1));
  EXPECT_TRUE(o1 == (o1));
  EXPECT_TRUE(o1 != o2);
  EXPECT_TRUE(o1 >= (o1));
  EXPECT_TRUE(o2 >= o1);
  EXPECT_TRUE(o2 > o1);

  EXPECT_FALSE(o2 < o1);
  EXPECT_FALSE(o2 <= o1);
  EXPECT_FALSE(o2 <= o1);
  EXPECT_FALSE(o2 == o1);
  EXPECT_FALSE(o1 != (o1));
  EXPECT_FALSE(o1 >= o2);
  EXPECT_FALSE(o1 >= o2);
  EXPECT_FALSE(o1 > o2);

  /* folly::Optional explicitly doesn't support comparisons with contained value
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
  */

  // boost::optional does support comparison with contained value, which can
  // lead to confusion when a bool is contained
  boost::optional<int> boi(3);
  EXPECT_TRUE(boi < 5);
  EXPECT_TRUE(boi <= 4);
  EXPECT_TRUE(boi == 3);
  EXPECT_TRUE(boi != 2);
  EXPECT_TRUE(boi >= 1);
  EXPECT_TRUE(boi > 0);
  EXPECT_TRUE(1 <  boi);
  EXPECT_TRUE(2 <= boi);
  EXPECT_TRUE(3 == boi);
  EXPECT_TRUE(4 != boi);
  EXPECT_TRUE(5 >= boi);
  EXPECT_TRUE(6 >  boi);

  boost::optional<bool> bob(false);
  EXPECT_TRUE((bool)bob);
  EXPECT_TRUE(bob == false); // well that was confusing
  EXPECT_FALSE(bob != false);
}

TEST(Optional, Conversions) {
  Optional<bool> mbool;
  Optional<short> mshort;
  Optional<char*> mstr;
  Optional<int> mint;

  //These don't compile
  //bool b = mbool;
  //short s = mshort;
  //char* c = mstr;
  //int x = mint;
  //char* c(mstr);
  //short s(mshort);
  //int x(mint);

  // intended explicit operator bool, for if (opt).
  bool b(mbool);

  // Truthy tests work and are not ambiguous
  if (mbool && mshort && mstr && mint) { // only checks not-empty
    if (*mbool && *mshort && *mstr && *mint) { // only checks value
      ;
    }
  }

  mbool = false;
  EXPECT_TRUE(bool(mbool));
  EXPECT_FALSE(*mbool);

  mbool = true;
  EXPECT_TRUE(bool(mbool));
  EXPECT_TRUE(*mbool);

  mbool = none;
  EXPECT_FALSE(bool(mbool));

  // No conversion allowed; does not compile
  // EXPECT_TRUE(mbool == false);
}

TEST(Optional, Pointee) {
  Optional<int> x;
  EXPECT_FALSE(get_pointer(x));
  x = 1;
  EXPECT_TRUE(get_pointer(x));
  *get_pointer(x) = 2;
  EXPECT_TRUE(*x == 2);
  x = none;
  EXPECT_FALSE(get_pointer(x));
}

TEST(Optional, MakeOptional) {
  // const L-value version
  const std::string s("abc");
  auto optStr = make_optional(s);
  ASSERT_TRUE(optStr.hasValue());
  EXPECT_EQ(*optStr, "abc");
  *optStr = "cde";
  EXPECT_EQ(s, "abc");
  EXPECT_EQ(*optStr, "cde");

  // L-value version
  std::string s2("abc");
  auto optStr2 = make_optional(s2);
  ASSERT_TRUE(optStr2.hasValue());
  EXPECT_EQ(*optStr2, "abc");
  *optStr2 = "cde";
  // it's vital to check that s2 wasn't clobbered
  EXPECT_EQ(s2, "abc");

  // L-value reference version
  std::string& s3(s2);
  auto optStr3 = make_optional(s3);
  ASSERT_TRUE(optStr3.hasValue());
  EXPECT_EQ(*optStr3, "abc");
  *optStr3 = "cde";
  EXPECT_EQ(s3, "abc");

  // R-value ref version
  unique_ptr<int> pInt(new int(3));
  auto optIntPtr = make_optional(std::move(pInt));
  EXPECT_TRUE(pInt.get() == nullptr);
  ASSERT_TRUE(optIntPtr.hasValue());
  EXPECT_EQ(**optIntPtr, 3);
}

#ifdef __clang__
# pragma clang diagnostic push
# if __clang_major__ > 3 || __clang_minor__ >= 6
#  pragma clang diagnostic ignored "-Wself-move"
# endif
#endif

TEST(Optional, SelfAssignment) {
  Optional<int> a = 42;
  a = a;
  ASSERT_TRUE(a.hasValue() && a.value() == 42);

  Optional<int> b = 23333333;
  b = std::move(b);
  ASSERT_TRUE(b.hasValue() && b.value() == 23333333);
}

#ifdef __clang__
#pragma clang diagnostic pop
#endif

class ContainsOptional {
 public:
  ContainsOptional() { }
  explicit ContainsOptional(int x) : opt_(x) { }
  bool hasValue() const { return opt_.hasValue(); }
  int value() const { return opt_.value(); }

  ContainsOptional(const ContainsOptional &other) = default;
  ContainsOptional& operator=(const ContainsOptional &other) = default;
  ContainsOptional(ContainsOptional &&other) = default;
  ContainsOptional& operator=(ContainsOptional &&other) = default;

 private:
  Optional<int> opt_;
};

/**
 * Test that a class containing an Optional can be copy and move assigned.
 * This was broken under gcc 4.7 until assignment operators were explicitly
 * defined.
 */
TEST(Optional, AssignmentContained) {
  {
    ContainsOptional source(5), target;
    target = source;
    EXPECT_TRUE(target.hasValue());
    EXPECT_EQ(5, target.value());
  }

  {
    ContainsOptional source(5), target;
    target = std::move(source);
    EXPECT_TRUE(target.hasValue());
    EXPECT_EQ(5, target.value());
    EXPECT_FALSE(source.hasValue());
  }

  {
    ContainsOptional opt_uninit, target(10);
    target = opt_uninit;
    EXPECT_FALSE(target.hasValue());
  }
}

}
