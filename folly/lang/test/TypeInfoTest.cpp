/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <folly/lang/TypeInfo.h>

#include <folly/portability/GTest.h>

class TypeInfoTest : public testing::Test {};

namespace {
struct Foo {};
struct Bar : Foo {
  virtual ~Bar() {}
};
struct Toc : Bar {};

// gtest-v1.12 adds an explicit overload of PrintTo taking std::type_info const&
// with which our own overload would be ambiguous, so work around with a wrapper
//
// remove once gtest-v1.12 is the minimum elegantly-supported version
struct ti : private std::reference_wrapper<std::type_info const> {
  using base = std::reference_wrapper<std::type_info const>;
  using base::base;
  friend bool operator==(ti a, ti b) noexcept { return a.get() == b.get(); }
  friend std::ostream& operator<<(std::ostream& o, ti v) {
    return o << v.get().name();
  }
};
template <typename A, typename B>
A const& as(B const& _) {
  return _;
}
} // namespace

TEST_F(TypeInfoTest, exanples) {
  EXPECT_EQ(ti{typeid(Foo)}, ti{*folly::type_info_of<Foo>()});
  EXPECT_EQ(ti{typeid(Foo)}, ti{*folly::type_info_of(Foo())});
  EXPECT_EQ(ti{typeid(Foo)}, ti{*folly::type_info_of(as<Foo>(Bar()))});
  EXPECT_EQ(ti{typeid(Bar)}, ti{*folly::type_info_of(as<Bar>(Bar()))});
  EXPECT_EQ(ti{typeid(Toc)}, ti{*folly::type_info_of(as<Bar>(Toc()))});
}
