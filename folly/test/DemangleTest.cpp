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

#include <folly/Demangle.h>

#include <folly/lang/Pretty.h>
#include <folly/portability/GTest.h>

using folly::demangle;
using folly::demangle_build_has_cxxabi;
using folly::demangle_build_has_liberty;
using folly::demangle_max_symbol_size;
using folly::pretty_name;

namespace folly_test {
struct ThisIsAVeryLongStructureName {};
} // namespace folly_test

class DemangleTest : public testing::Test {};

TEST_F(DemangleTest, demangle_return_string) {
  using type = folly_test::ThisIsAVeryLongStructureName;
  auto const raw = typeid(type).name();
  auto const expected = demangle_build_has_cxxabi ? pretty_name<type>() : raw;
  EXPECT_EQ(expected, demangle(typeid(type)));
}

TEST_F(DemangleTest, demangle_to_buffer) {
  using type = folly_test::ThisIsAVeryLongStructureName;
  auto const raw = typeid(type).name();
  auto const expected = demangle_build_has_liberty ? pretty_name<type>() : raw;

  {
    std::vector<char> buf;
    buf.resize(1 + strlen(expected));
    EXPECT_EQ(strlen(expected), demangle(typeid(type), buf.data(), buf.size()));
    EXPECT_EQ(std::string(expected), buf.data());
  }

  {
    constexpr size_t size = 10;
    std::vector<char> buf;
    buf.resize(1 + size);
    EXPECT_EQ(strlen(expected), demangle(typeid(type), buf.data(), buf.size()));
    EXPECT_EQ(std::string(expected).substr(0, size), buf.data());
  }
}

TEST_F(DemangleTest, demangle_long_symbol) {
  //  pretty and demangled names are the same for int but not for size_t
  //  mangling strlen multiplier can be assumed to be at least 4
  using type = std::make_integer_sequence<int, demangle_max_symbol_size / 4>;
  auto raw = typeid(type).name();
  auto choice = demangle_max_symbol_size ? raw : pretty_name<type>();

  EXPECT_EQ(std::string(choice), demangle(raw).toStdString());

  auto const expected = demangle_build_has_liberty ? choice : raw;
  constexpr size_t size = 15;
  std::vector<char> buf;
  buf.resize(1 + size);
  EXPECT_EQ(strlen(expected), demangle(raw, buf.data(), buf.size()));
  EXPECT_EQ(std::string(expected).substr(0, size), buf.data());
}
