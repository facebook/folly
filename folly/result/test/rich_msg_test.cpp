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

#include <folly/result/rich_msg.h>

#include <folly/portability/GTest.h>

#if FOLLY_HAS_RESULT

namespace folly {

static_assert(!std::is_default_constructible_v<rich_msg>);
static_assert(std::is_copy_constructible_v<rich_msg>);
static_assert(std::is_move_constructible_v<rich_msg>);
static_assert(std::is_copy_assignable_v<rich_msg>);
static_assert(std::is_move_assignable_v<rich_msg>);

TEST(RichMsgTest, basics) {
  rich_msg msg1{"msg"};
  auto check = [&](const rich_msg& msg2) {
    EXPECT_STREQ(msg2.message(), "msg");
    EXPECT_EQ(msg2.location().line(), msg1.location().line());
  };
  check(rich_msg{msg1}); // copy ctor
  { // move ctor
    auto msg2{msg1};
    check(rich_msg{std::move(msg2)});
  }
  { // copy assignment
    rich_msg msg2{"overwrite me"};
    msg2 = msg1;
    check(msg2);
  }
  { // move assignment
    rich_msg msg2{"overwrite me"};
    msg2 = rich_msg{msg1};
    check(msg2);
  }
}

TEST(RichMsgTest, constructFromLiteralString) {
  auto line = source_location::current().line() + 1;
  rich_msg msg{"simple message"};
  EXPECT_STREQ(msg.message(), "simple message");
  EXPECT_EQ(line, msg.location().line());
}

TEST(RichMsgTest, constructFromFormatString) {
#if 0 // Manual test -- format string mismatch with argument type
  rich_msg msg{"{:d}", "not an int"};
#endif
  auto line = source_location::current().line() + 1;
  rich_msg msg{"{} bottles of kheer", 42};
  EXPECT_STREQ(msg.message(), "42 bottles of kheer");
  EXPECT_EQ(line, msg.location().line());
}

TEST(RichMsgTest, constructFromComponents) {
  auto loc = source_location::current();
  rich_msg msg{exception_shared_string{"msg"}, loc};
  EXPECT_STREQ(msg.message(), "msg");
  EXPECT_EQ(msg.location().line(), loc.line());
}

} // namespace folly

#endif // FOLLY_HAS_RESULT
