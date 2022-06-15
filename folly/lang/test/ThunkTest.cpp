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

#include <folly/lang/Thunk.h>

#include <string>
#include <string_view>

#include <folly/Traits.h>
#include <folly/portability/GTest.h>

namespace folly::detail {

class ThunkTest : public testing::Test {};

TEST_F(ThunkTest, make_ruin) {
  auto make0 = thunk::make<std::string>;
  auto make1 = thunk::make<std::string, std::string_view>;
  auto ruin = thunk::ruin<std::string>;

  {
    auto str = make0();
    EXPECT_EQ("", *(std::string*)str);
    ruin(str);
  }
  {
    auto str = make1("blargh");
    EXPECT_EQ("blargh", *(std::string*)str);
    ruin(str);
  }
}

TEST_F(ThunkTest, ctor_dtor) {
  auto ctor0 = thunk::ctor<std::string>;
  auto ctor1 = thunk::ctor<std::string, std::string_view>;
  auto dtor = thunk::dtor<std::string>;
  aligned_storage_for_t<std::string> buf;

  {
    void* str = &buf;
    ctor0(str);
    EXPECT_EQ("", *(std::string*)str);
    dtor(str);
  }
  {
    void* str = &buf;
    ctor1(str, "blargh");
    EXPECT_EQ("blargh", *(std::string*)str);
    dtor(str);
  }
}

} // namespace folly::detail
