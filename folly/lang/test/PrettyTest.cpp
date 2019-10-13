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

#include <folly/lang/Pretty.h>

#include <folly/portability/GTest.h>

namespace folly {

class PrettyTest : public testing::Test {};

TEST_F(PrettyTest, example) {
  constexpr auto const name = pretty_name<int>();
  EXPECT_STREQ("int", name);
}

TEST_F(PrettyTest, example_msc) {
  constexpr auto const tag = detail::pretty_tag_msc{};
  constexpr auto const& name = "void __cdecl foo<int>(void)";
  constexpr auto const info = detail::pretty_parse(tag, name);
  EXPECT_EQ("int", detail::pretty_info_to<std::string>(info, name));
}

TEST_F(PrettyTest, example_gcc) {
  constexpr auto const tag = detail::pretty_tag_gcc{};
  constexpr auto const& name = "void foo() [T = int]";
  constexpr auto const info = detail::pretty_parse(tag, name);
  EXPECT_EQ("int", detail::pretty_info_to<std::string>(info, name));
}

} // namespace folly
