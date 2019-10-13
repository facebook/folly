/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

namespace std {
static void PrintTo(type_info const& ti, std::ostream* out) {
  *out << ti.name();
}
} // namespace std

namespace {
struct Foo {};
struct Bar : Foo {
  virtual ~Bar() {}
};
struct Toc : Bar {};
} // namespace

TEST_F(TypeInfoTest, exanples) {
  EXPECT_EQ(typeid(Foo), *folly::type_info_of<Foo>());
  EXPECT_EQ(typeid(Foo), *folly::type_info_of(Foo()));
  EXPECT_EQ(typeid(Foo), *folly::type_info_of(static_cast<Foo const&>(Bar())));
  EXPECT_EQ(typeid(Bar), *folly::type_info_of(static_cast<Bar const&>(Bar())));
  EXPECT_EQ(typeid(Toc), *folly::type_info_of(static_cast<Bar const&>(Toc())));
}
