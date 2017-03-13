/*
 * Copyright 2017 Facebook, Inc.
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

#include <folly/MapUtil.h>

#include <map>
#include <unordered_map>

#include <folly/portability/GTest.h>

using namespace folly;

TEST(MapUtil, get_default) {
  std::map<int, int> m;
  m[1] = 2;
  EXPECT_EQ(2, get_default(m, 1, 42));
  EXPECT_EQ(42, get_default(m, 2, 42));
  EXPECT_EQ(0, get_default(m, 3));
}

TEST(MapUtil, get_default_function) {
  std::map<int, int> m;
  m[1] = 2;
  EXPECT_EQ(2, get_default(m, 1, [] { return 42; }));
  EXPECT_EQ(42, get_default(m, 2, [] { return 42; }));
  EXPECT_EQ(0, get_default(m, 3));
}

TEST(MapUtil, get_or_throw) {
  std::map<int, int> m;
  m[1] = 2;
  EXPECT_EQ(2, get_or_throw(m, 1));
  EXPECT_THROW(get_or_throw(m, 2), std::out_of_range);
  EXPECT_EQ(&m[1], &get_or_throw(m, 1));
  get_or_throw(m, 1) = 3;
  EXPECT_EQ(3, get_or_throw(m, 1));
  const auto& cm = m;
  EXPECT_EQ(&m[1], &get_or_throw(cm, 1));
  EXPECT_EQ(3, get_or_throw(cm, 1));
  EXPECT_THROW(get_or_throw(cm, 2), std::out_of_range);
}

TEST(MapUtil, get_or_throw_specified) {
  std::map<int, int> m;
  m[1] = 2;
  EXPECT_EQ(2, get_or_throw<std::runtime_error>(m, 1));
  EXPECT_THROW(get_or_throw<std::runtime_error>(m, 2), std::runtime_error);
}

TEST(MapUtil, get_optional) {
  std::map<int, int> m;
  m[1] = 2;
  EXPECT_TRUE(get_optional(m, 1).hasValue());
  EXPECT_EQ(2, get_optional(m, 1).value());
  EXPECT_FALSE(get_optional(m, 2).hasValue());
}

TEST(MapUtil, get_ref_default) {
  std::map<int, int> m;
  m[1] = 2;
  const int i = 42;
  EXPECT_EQ(2, get_ref_default(m, 1, i));
  EXPECT_EQ(42, get_ref_default(m, 2, i));
  EXPECT_EQ(std::addressof(i), std::addressof(get_ref_default(m, 2, i)));
}

TEST(MapUtil, get_ref_default_function) {
  std::map<int, int> m;
  m[1] = 2;
  const int i = 42;
  EXPECT_EQ(2, get_ref_default(m, 1, [&i]() -> const int& { return i; }));
  EXPECT_EQ(42, get_ref_default(m, 2, [&i]() -> const int& { return i; }));
  EXPECT_EQ(
      std::addressof(i),
      std::addressof(
          get_ref_default(m, 2, [&i]() -> const int& { return i; })));
  // statically disallowed:
  // get_ref_default(m, 2, [] { return 7; });
}

TEST(MapUtil, get_ptr) {
  std::map<int, int> m;
  m[1] = 2;
  EXPECT_EQ(2, *get_ptr(m, 1));
  EXPECT_TRUE(get_ptr(m, 2) == nullptr);
  *get_ptr(m, 1) = 4;
  EXPECT_EQ(4, m.at(1));
}

TEST(MapUtil, get_ptr_path_simple) {
  using std::map;
  map<int, map<int, map<int, map<int, int>>>> m{{1, {{2, {{3, {{4, 5}}}}}}}};
  EXPECT_EQ(5, *get_ptr(m, 1, 2, 3, 4));
  EXPECT_TRUE(get_ptr(m, 1, 2, 3, 4));
  EXPECT_FALSE(get_ptr(m, 1, 2, 3, 0));
  EXPECT_TRUE(get_ptr(m, 1, 2, 3));
  EXPECT_FALSE(get_ptr(m, 1, 2, 0));
  EXPECT_TRUE(get_ptr(m, 1, 2));
  EXPECT_FALSE(get_ptr(m, 1, 0));
  EXPECT_TRUE(get_ptr(m, 1));
  EXPECT_FALSE(get_ptr(m, 0));
  const auto& cm = m;
  ++*get_ptr(m, 1, 2, 3, 4);
  EXPECT_EQ(6, *get_ptr(cm, 1, 2, 3, 4));
  EXPECT_TRUE(get_ptr(cm, 1, 2, 3, 4));
  EXPECT_FALSE(get_ptr(cm, 1, 2, 3, 0));
}

TEST(MapUtil, get_ptr_path_mixed) {
  using std::map;
  using std::unordered_map;
  using std::string;
  unordered_map<string, map<int, map<string, int>>> m{{"a", {{1, {{"b", 7}}}}}};
  EXPECT_EQ(7, *get_ptr(m, "a", 1, "b"));
  EXPECT_TRUE(get_ptr(m, "a", 1, "b"));
  EXPECT_FALSE(get_ptr(m, "b", 1, "b"));
  EXPECT_FALSE(get_ptr(m, "a", 2, "b"));
  EXPECT_FALSE(get_ptr(m, "a", 1, "c"));
  EXPECT_TRUE(get_ptr(m, "a", 1, "b"));
  EXPECT_TRUE(get_ptr(m, "a", 1));
  EXPECT_TRUE(get_ptr(m, "a"));
  const auto& cm = m;
  ++*get_ptr(m, "a", 1, "b");
  EXPECT_EQ(8, *get_ptr(cm, "a", 1, "b"));
  EXPECT_TRUE(get_ptr(cm, "a", 1, "b"));
  EXPECT_FALSE(get_ptr(cm, "b", 1, "b"));
}
