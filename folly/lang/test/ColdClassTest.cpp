/*
 * Copyright 2004-present Facebook, Inc.
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

#include <folly/lang/ColdClass.h>

#include <folly/portability/GTest.h>
#include <type_traits>

using folly::ColdClass;

TEST(ColdClass, inheritance) {
  // The only verifiable property of ColdClass is that it must not disrupt the
  // default constructor/destructor, default copy/move constructors and default
  // copy/move assignment operators when a class derives from it.
  struct TestStruct : ColdClass {};
  EXPECT_TRUE(std::is_nothrow_default_constructible<TestStruct>::value);
  EXPECT_TRUE(std::is_trivially_copy_constructible<TestStruct>::value);
  EXPECT_TRUE(std::is_trivially_move_constructible<TestStruct>::value);
  EXPECT_TRUE(std::is_trivially_copy_assignable<TestStruct>::value);
  EXPECT_TRUE(std::is_trivially_move_assignable<TestStruct>::value);
  EXPECT_TRUE(std::is_trivially_destructible<TestStruct>::value);
  // Same again, but private inheritance. Should make no difference.
  class TestClass : ColdClass {};
  EXPECT_TRUE(std::is_nothrow_default_constructible<TestClass>::value);
  EXPECT_TRUE(std::is_trivially_copy_constructible<TestClass>::value);
  EXPECT_TRUE(std::is_trivially_move_constructible<TestClass>::value);
  EXPECT_TRUE(std::is_trivially_copy_assignable<TestClass>::value);
  EXPECT_TRUE(std::is_trivially_move_assignable<TestClass>::value);
  EXPECT_TRUE(std::is_trivially_destructible<TestClass>::value);
}
