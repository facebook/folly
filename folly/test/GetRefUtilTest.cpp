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

#include <folly/GetRefUtil.h>

#include <optional>
#include <string>
#include <vector>

#include <folly/portability/GTest.h>

using namespace folly;

// -- Pointer overload tests --

TEST(GetRefUtil, pointerNonNull) {
  const int val = 42;
  const int& ref = getRefOrDefault(&val);
  EXPECT_EQ(ref, 42);
  EXPECT_EQ(&ref, &val);
}

TEST(GetRefUtil, pointerNull) {
  const int* ptr = nullptr;
  const int& ref = getRefOrDefault(ptr);
  EXPECT_EQ(ref, 0);
}

TEST(GetRefUtil, pointerNullReturnsStableRef) {
  const int* ptr = nullptr;
  const int& ref1 = getRefOrDefault(ptr);
  const int& ref2 = getRefOrDefault(ptr);
  EXPECT_EQ(&ref1, &ref2);
}

TEST(GetRefUtil, pointerNullString) {
  const std::string* ptr = nullptr;
  const std::string& ref = getRefOrDefault(ptr);
  EXPECT_TRUE(ref.empty());
}

TEST(GetRefUtil, pointerNonNullString) {
  const std::string val = "hello";
  const std::string& ref = getRefOrDefault(&val);
  EXPECT_EQ(ref, "hello");
  EXPECT_EQ(&ref, &val);
}

TEST(GetRefUtil, pointerNullVector) {
  const std::vector<int>* ptr = nullptr;
  const std::vector<int>& ref = getRefOrDefault(ptr);
  EXPECT_TRUE(ref.empty());
}

TEST(GetRefUtil, pointerNonNullVector) {
  const std::vector<int> val = {1, 2, 3};
  const std::vector<int>& ref = getRefOrDefault(&val);
  const std::vector<int> expected = {1, 2, 3};
  EXPECT_EQ(ref, expected);
  EXPECT_EQ(&ref, &val);
}

// -- IsOptionalLike concept tests --

static_assert(IsOptionalLike<std::optional<int>>);
static_assert(IsOptionalLike<std::optional<std::string>>);
static_assert(!IsOptionalLike<int>);
static_assert(!IsOptionalLike<std::string>);

// -- Optional overload tests --

TEST(GetRefUtil, optionalWithValue) {
  const std::optional<int> opt = 42;
  const int& ref = getRefOrDefault(opt);
  EXPECT_EQ(ref, 42);
  EXPECT_EQ(&ref, &opt.value());
}

TEST(GetRefUtil, optionalEmpty) {
  const std::optional<int> opt;
  const int& ref = getRefOrDefault(opt);
  EXPECT_EQ(ref, 0);
}

TEST(GetRefUtil, optionalEmptyReturnsStableRef) {
  const std::optional<int> opt;
  const int& ref1 = getRefOrDefault(opt);
  const int& ref2 = getRefOrDefault(opt);
  EXPECT_EQ(&ref1, &ref2);
}

TEST(GetRefUtil, optionalWithString) {
  const std::optional<std::string> opt = "hello";
  const std::string& ref = getRefOrDefault(opt);
  EXPECT_EQ(ref, "hello");
}

TEST(GetRefUtil, optionalEmptyString) {
  const std::optional<std::string> opt;
  const std::string& ref = getRefOrDefault(opt);
  EXPECT_TRUE(ref.empty());
}

TEST(GetRefUtil, optionalWithVector) {
  const std::optional<std::vector<int>> opt = std::vector<int>{1, 2, 3};
  const std::vector<int>& ref = getRefOrDefault(opt);
  const std::vector<int> expected = {1, 2, 3};
  EXPECT_EQ(ref, expected);
}

TEST(GetRefUtil, optionalEmptyVector) {
  const std::optional<std::vector<int>> opt;
  const std::vector<int>& ref = getRefOrDefault(opt);
  EXPECT_TRUE(ref.empty());
}

// -- Custom optional-like type tests --

struct CustomOptional {
  using value_type = int;
  bool present;
  int val;

  bool has_value() const { return present; }
  const int& value() const { return val; }
};

static_assert(IsOptionalLike<CustomOptional>);

TEST(GetRefUtil, customOptionalLikeWithValue) {
  const CustomOptional opt{true, 99};
  const int& ref = getRefOrDefault(opt);
  EXPECT_EQ(ref, 99);
}

TEST(GetRefUtil, customOptionalLikeEmpty) {
  const CustomOptional opt{false, 99};
  const int& ref = getRefOrDefault(opt);
  EXPECT_EQ(ref, 0);
}
