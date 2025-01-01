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

#include <folly/FmtUtility.h>

#include <map>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

using namespace std::literals;

struct FmtUtilityTest : testing::Test {};

TEST_F(FmtUtilityTest, fmt_make_format_args_from_map_fn) {
  EXPECT_EQ(
      "hello bob you silly goose",
      fmt::vformat(
          "hello {name} you {adj} goose",
          folly::fmt_make_format_args_from_map(
              std::map<std::string, std::string_view>{
                  {"name"s, "bob"sv},
                  {"adj"s, "silly"sv},
              })));
}

TEST_F(FmtUtilityTest, fmt_vformat_mangle) {
  EXPECT_EQ(
      "hello bob you silly goose",
      fmt::vformat(
          folly::fmt_vformat_mangle_format_string(
              "hello {@pre-key|name} you {@pre-key|adj} goose"),
          folly::fmt_make_format_args_from_map(
              std::map<std::string, std::string_view>{
                  {folly::fmt_vformat_mangle_name("@pre-key|name"), "bob"sv},
                  {folly::fmt_vformat_mangle_name("@pre-key|adj"), "silly"sv},
              })));
}
