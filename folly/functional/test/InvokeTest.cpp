/*
 * Copyright 2017-present Facebook, Inc.
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

#include <folly/functional/Invoke.h>

#include <folly/portability/GTest.h>

class InvokeTest : public testing::Test {};

namespace {
struct Fn {
  char operator()(int, int) noexcept {
    return 'a';
  }
  int volatile&& operator()(int, char const*) {
    return std::move(x_);
  }
  float operator()(float, float) {
    return 3.14;
  }
  int volatile x_ = 17;
};

FOLLY_CREATE_MEMBER_INVOKE_TRAITS(test_invoke_traits, test);

struct Obj {
  char test(int, int) noexcept {
    return 'a';
  }
  int volatile&& test(int, char const*) {
    return std::move(x_);
  }
  float test(float, float) {
    return 3.14;
  }
  int volatile x_ = 17;
};

} // namespace

TEST_F(InvokeTest, invoke) {
  Fn fn;

  EXPECT_TRUE(noexcept(folly::invoke(fn, 1, 2)));
  EXPECT_FALSE(noexcept(folly::invoke(fn, 1, "2")));

  EXPECT_EQ('a', folly::invoke(fn, 1, 2));
  EXPECT_EQ(17, folly::invoke(fn, 1, "2"));

  using FnA = char (Fn::*)(int, int);
  using FnB = int volatile && (Fn::*)(int, char const*);
  EXPECT_EQ('a', folly::invoke(static_cast<FnA>(&Fn::operator()), fn, 1, 2));
  EXPECT_EQ(17, folly::invoke(static_cast<FnB>(&Fn::operator()), fn, 1, "2"));
}

TEST_F(InvokeTest, invoke_result) {
  EXPECT_TRUE(
      (std::is_same<char, folly::invoke_result_t<Fn, int, char>>::value));
  EXPECT_TRUE(
      (std::is_same<int volatile&&, folly::invoke_result_t<Fn, int, char*>>::
           value));
}

TEST_F(InvokeTest, is_invocable) {
  EXPECT_TRUE((folly::is_invocable<Fn, int, char>::value));
  EXPECT_TRUE((folly::is_invocable<Fn, int, char*>::value));
  EXPECT_FALSE((folly::is_invocable<Fn, int>::value));
}

TEST_F(InvokeTest, is_invocable_r) {
  EXPECT_TRUE((folly::is_invocable_r<int, Fn, int, char>::value));
  EXPECT_TRUE((folly::is_invocable_r<int, Fn, int, char*>::value));
  EXPECT_FALSE((folly::is_invocable_r<int, Fn, int>::value));
}

TEST_F(InvokeTest, is_nothrow_invocable) {
  EXPECT_TRUE((folly::is_nothrow_invocable<Fn, int, char>::value));
  EXPECT_FALSE((folly::is_nothrow_invocable<Fn, int, char*>::value));
  EXPECT_FALSE((folly::is_nothrow_invocable<Fn, int>::value));
}

TEST_F(InvokeTest, is_nothrow_invocable_r) {
  EXPECT_TRUE((folly::is_nothrow_invocable_r<int, Fn, int, char>::value));
  EXPECT_FALSE((folly::is_nothrow_invocable_r<int, Fn, int, char*>::value));
  EXPECT_FALSE((folly::is_nothrow_invocable_r<int, Fn, int>::value));
}

TEST_F(InvokeTest, member_invoke) {
  using traits = test_invoke_traits;

  Obj fn;

  EXPECT_TRUE(noexcept(traits::invoke(fn, 1, 2)));
  EXPECT_FALSE(noexcept(traits::invoke(fn, 1, "2")));

  EXPECT_EQ('a', traits::invoke(fn, 1, 2));
  EXPECT_EQ(17, traits::invoke(fn, 1, "2"));
}

TEST_F(InvokeTest, member_invoke_result) {
  using traits = test_invoke_traits;

  EXPECT_TRUE(
      (std::is_same<char, traits::invoke_result_t<Obj, int, char>>::value));
  EXPECT_TRUE(
      (std::is_same<int volatile&&, traits::invoke_result_t<Obj, int, char*>>::
           value));
}

TEST_F(InvokeTest, member_is_invocable) {
  using traits = test_invoke_traits;

  EXPECT_TRUE((traits::is_invocable<Obj, int, char>::value));
  EXPECT_TRUE((traits::is_invocable<Obj, int, char*>::value));
  EXPECT_FALSE((traits::is_invocable<Obj, int>::value));
}

TEST_F(InvokeTest, member_is_invocable_r) {
  using traits = test_invoke_traits;

  EXPECT_TRUE((traits::is_invocable_r<int, Obj, int, char>::value));
  EXPECT_TRUE((traits::is_invocable_r<int, Obj, int, char*>::value));
  EXPECT_FALSE((traits::is_invocable_r<int, Obj, int>::value));
}

TEST_F(InvokeTest, member_is_nothrow_invocable) {
  using traits = test_invoke_traits;

  EXPECT_TRUE((traits::is_nothrow_invocable<Obj, int, char>::value));
  EXPECT_FALSE((traits::is_nothrow_invocable<Obj, int, char*>::value));
  EXPECT_FALSE((traits::is_nothrow_invocable<Obj, int>::value));
}

TEST_F(InvokeTest, member_is_nothrow_invocable_r) {
  using traits = test_invoke_traits;

  EXPECT_TRUE((traits::is_nothrow_invocable_r<int, Obj, int, char>::value));
  EXPECT_FALSE((traits::is_nothrow_invocable_r<int, Obj, int, char*>::value));
  EXPECT_FALSE((traits::is_nothrow_invocable_r<int, Obj, int>::value));
}
