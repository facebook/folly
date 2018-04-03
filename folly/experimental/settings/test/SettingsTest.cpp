/*
 * Copyright 2018-present Facebook, Inc.
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
#include <folly/experimental/settings/Settings.h>
#include <folly/Format.h>
#include <folly/portability/GTest.h>

#include "a.h"
#include "b.h"

namespace some_ns {
FOLLY_SETTING(follytest, std::string, some_flag, "default", "Description");
FOLLY_SETTING(
    follytest,
    std::string,
    unused,
    "unused_default",
    "Not used, but should still be in the list");
// Enable to test runtime collision checking logic
#if 0
FOLLY_SETTING(follytest, std::string, internal_flag_to_a, "collision_with_a",
              "Collision_with_a");
#endif
} // namespace some_ns

TEST(Settings, basic) {
  std::string allFlags;
  folly::settings::forEachSetting([&allFlags](
                                      folly::StringPiece name,
                                      folly::StringPiece value,
                                      folly::StringPiece reason,
                                      const std::type_info& type) {
    std::string typeName;
    if (type == typeid(int)) {
      typeName = "int";
    } else if (type == typeid(std::string)) {
      typeName = "std::string";
    } else {
      ASSERT_FALSE(true);
    }
    allFlags += folly::sformat("{} {} {} {}\n", name, value, reason, typeName);
  });
  EXPECT_EQ(
      allFlags,
      "follytest_internal_flag_to_a 789 default int\n"
      "follytest_internal_flag_to_b test default std::string\n"
      "follytest_public_flag_to_a 456 default int\n"
      "follytest_public_flag_to_b basdf default std::string\n"
      "follytest_some_flag default default std::string\n"
      "follytest_unused unused_default default std::string\n");
  EXPECT_EQ(a_ns::a_func(), 1245);
  EXPECT_EQ(b_ns::b_func(), "testbasdf");
  EXPECT_EQ(*some_ns::SETTING_follytest_some_flag, "default");
  a_ns::SETTING_follytest_public_flag_to_a.set(100);
  EXPECT_EQ(*a_ns::SETTING_follytest_public_flag_to_a, 100);
  EXPECT_EQ(a_ns::getRemote(), 100);
  a_ns::setRemote(200);
  EXPECT_EQ(*a_ns::SETTING_follytest_public_flag_to_a, 200);
  EXPECT_EQ(a_ns::getRemote(), 200);

  auto res = folly::settings::getAsString("follytest_public_flag_to_a");
  EXPECT_TRUE(res.hasValue());
  EXPECT_EQ(res->first, "200");
  EXPECT_EQ(res->second, "remote_set");

  res = folly::settings::getAsString("follytest_nonexisting");
  EXPECT_FALSE(res.hasValue());

  EXPECT_TRUE(folly::settings::setFromString(
      "follytest_public_flag_to_a", "300", "from_string"));
  EXPECT_EQ(*a_ns::SETTING_follytest_public_flag_to_a, 300);
  EXPECT_EQ(a_ns::getRemote(), 300);
  res = folly::settings::getAsString("follytest_public_flag_to_a");
  EXPECT_TRUE(res.hasValue());
  EXPECT_EQ(res->first, "300");
  EXPECT_EQ(res->second, "from_string");

  EXPECT_FALSE(folly::settings::setFromString(
      "follytest_nonexisting", "300", "from_string"));

  EXPECT_TRUE(folly::settings::resetToDefault("follytest_public_flag_to_a"));
  EXPECT_EQ(*a_ns::SETTING_follytest_public_flag_to_a, 456);
  EXPECT_EQ(a_ns::getRemote(), 456);

  EXPECT_FALSE(folly::settings::resetToDefault("follytest_nonexisting"));
}
