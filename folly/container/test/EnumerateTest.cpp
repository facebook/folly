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

#include <array>
#include <string>
#include <vector>

#include <folly/Range.h>
#include <folly/container/Enumerate.h>
#include <folly/portability/GTest.h>

namespace {

template <class T>
struct IsConstReference {
  constexpr static bool value = false;
};
template <class T>
struct IsConstReference<const T&> {
  constexpr static bool value = true;
};

constexpr int basicSum(const std::array<int, 3>& test) {
  int sum = 0;
  for (auto it : folly::enumerate(test)) {
    sum += *it;
  }
  return sum;
}

constexpr int cpp17StructuredBindingSum(const std::array<int, 3>& test) {
  int sum = 0;
  for (auto&& [_, integer] : folly::enumerate(test)) {
    sum += integer;
  }
  return sum;
}

} // namespace

TEST(Enumerate, Basic) {
  std::vector<std::string> v = {"abc", "a", "ab"};
  size_t i = 0;
  for (auto it : folly::enumerate(v)) {
    EXPECT_EQ(it.index, i);
    EXPECT_EQ(*it, v[i]);
    EXPECT_EQ(it->size(), v[i].size());

    /* Test mutability. */
    std::string newValue = "x";
    *it = newValue;
    EXPECT_EQ(newValue, v[i]);

    ++i;
  }

  EXPECT_EQ(i, v.size());
}

TEST(Enumerate, BasicRRef) {
  std::vector<std::string> v = {"abc", "a", "ab"};
  size_t i = 0;
  for (auto&& it : folly::enumerate(v)) {
    EXPECT_EQ(it.index, i);
    EXPECT_EQ(*it, v[i]);
    EXPECT_EQ(it->size(), v[i].size());

    /* Test mutability. */
    std::string newValue = "x";
    *it = newValue;
    EXPECT_EQ(newValue, v[i]);

    ++i;
  }

  EXPECT_EQ(i, v.size());
}

TEST(Enumerate, BasicConst) {
  std::vector<std::string> v = {"abc", "a", "ab"};
  size_t i = 0;
  for (const auto it : folly::enumerate(v)) {
    static_assert(IsConstReference<decltype(*it)>::value, "Const enumeration");
    EXPECT_EQ(it.index, i);
    EXPECT_EQ(*it, v[i]);
    EXPECT_EQ(it->size(), v[i].size());
    ++i;
  }

  EXPECT_EQ(i, v.size());
}

TEST(Enumerate, BasicConstRef) {
  std::vector<std::string> v = {"abc", "a", "ab"};
  size_t i = 0;
  for (const auto& it : folly::enumerate(v)) {
    static_assert(IsConstReference<decltype(*it)>::value, "Const enumeration");
    EXPECT_EQ(it.index, i);
    EXPECT_EQ(*it, v[i]);
    EXPECT_EQ(it->size(), v[i].size());
    ++i;
  }

  EXPECT_EQ(i, v.size());
}

TEST(Enumerate, BasicConstRRef) {
  std::vector<std::string> v = {"abc", "a", "ab"};
  size_t i = 0;
  for (const auto&& it : folly::enumerate(v)) {
    static_assert(IsConstReference<decltype(*it)>::value, "Const enumeration");
    EXPECT_EQ(it.index, i);
    EXPECT_EQ(*it, v[i]);
    EXPECT_EQ(it->size(), v[i].size());
    ++i;
  }

  EXPECT_EQ(i, v.size());
}

TEST(Enumerate, BasicVecBool) {
  std::vector<bool> v = {true, false, false, true};
  size_t i = 0;
  for (auto it : folly::enumerate(v)) {
    EXPECT_EQ(it.index, i);
    EXPECT_EQ(*it, v[i]);
    ++i;
  }

  EXPECT_EQ(i, v.size());
}

TEST(Enumerate, BasicVecBoolRRef) {
  std::vector<bool> v = {true, false, false, true};
  size_t i = 0;
  for (auto it : folly::enumerate(v)) {
    EXPECT_EQ(it.index, i);
    EXPECT_EQ(*it, v[i]);
    ++i;
  }

  EXPECT_EQ(i, v.size());
}

TEST(Enumerate, Temporary) {
  std::vector<std::string> v = {"abc", "a", "ab"};
  size_t i = 0;
  for (auto&& it : folly::enumerate(decltype(v)(v))) { // Copy v.
    EXPECT_EQ(it.index, i);
    EXPECT_EQ(*it, v[i]);
    EXPECT_EQ(it->size(), v[i].size());
    ++i;
  }

  EXPECT_EQ(i, v.size());
}

TEST(Enumerate, BasicConstArg) {
  const std::vector<std::string> v = {"abc", "a", "ab"};
  size_t i = 0;
  for (auto&& it : folly::enumerate(v)) {
    static_assert(
        IsConstReference<decltype(*it)>::value, "Enumerating a const vector");
    EXPECT_EQ(it.index, i);
    EXPECT_EQ(*it, v[i]);
    EXPECT_EQ(it->size(), v[i].size());
    ++i;
  }

  EXPECT_EQ(i, v.size());
}

TEST(Enumerate, TemporaryConstEnumerate) {
  std::vector<std::string> v = {"abc", "a", "ab"};
  size_t i = 0;
  for (const auto&& it : folly::enumerate(decltype(v)(v))) { // Copy v.
    static_assert(IsConstReference<decltype(*it)>::value, "Const enumeration");
    EXPECT_EQ(it.index, i);
    EXPECT_EQ(*it, v[i]);
    EXPECT_EQ(it->size(), v[i].size());
    ++i;
  }

  EXPECT_EQ(i, v.size());
}

TEST(Enumerate, RangeSupport) {
  std::vector<std::string> v = {"abc", "a", "ab"};
  size_t i = 0;
  for (const auto&& it : folly::enumerate(folly::range(v))) {
    EXPECT_EQ(it.index, i);
    EXPECT_EQ(*it, v[i]);
    EXPECT_EQ(it->size(), v[i].size());
    ++i;
  }

  EXPECT_EQ(i, v.size());
}

TEST(Enumerate, EmptyRange) {
  std::vector<std::string> v;
  for (auto&& it : folly::enumerate(v)) {
    (void)it; // Silence warnings.
    ADD_FAILURE();
  }
}

class CStringRange {
  const char* cstr;

 public:
  struct Sentinel {};

  explicit CStringRange(const char* cstr_) : cstr(cstr_) {}

  const char* begin() const { return cstr; }
  Sentinel end() const { return Sentinel{}; }
};

bool operator==(const char* c, CStringRange::Sentinel) {
  return *c == 0;
}

TEST(Enumerate, Cpp17Support) {
  std::array<char, 5> test = {"test"};
  for (const auto&& it : folly::enumerate(CStringRange{test.data()})) {
    ASSERT_LT(it.index, test.size());
    EXPECT_EQ(*it, test[it.index]);
  }
}

TEST(Enumerate, Cpp17StructuredBinding) {
  std::vector<std::string> test = {"abc", "a", "ab"};
  for (const auto& [index, str] : folly::enumerate(test)) {
    ASSERT_LT(index, test.size());
    EXPECT_EQ(str, test[index]);
  }
}

TEST(Enumerate, Cpp17StructuredBindingConstVector) {
  const std::vector<std::string> test = {"abc", "a", "ab"};
  for (auto&& [index, str] : folly::enumerate(test)) {
    static_assert(
        IsConstReference<decltype(str)>::value, "Enumerating const vector");
    ASSERT_LT(index, test.size());
    EXPECT_EQ(str, test[index]);
  }
}

TEST(Enumerate, Cpp17StructuredBindingModify) {
  std::vector<int> test = {1, 2, 3, 4, 5};
  for (auto&& [index, integer] : folly::enumerate(test)) {
    integer = 0;
  }

  for (const auto& integer : test) {
    EXPECT_EQ(integer, 0);
  }
}

TEST(Enumerate, BasicConstexpr) {
  constexpr std::array<int, 3> test = {1, 2, 3};
  static_assert(basicSum(test) == 6, "Basic enumerating is not constexpr");
  EXPECT_EQ(basicSum(test), 6);
}

TEST(Enumerate, Cpp17StructuredBindingConstexpr) {
  constexpr std::array<int, 3> test = {1, 2, 3};
  static_assert(
      cpp17StructuredBindingSum(test) == 6,
      "C++17 structured binding enumerating is not constexpr");
  EXPECT_EQ(cpp17StructuredBindingSum(test), 6);
}
