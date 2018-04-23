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

#include <folly/experimental/settings/test/a.h>
#include <folly/experimental/settings/test/b.h>

namespace some_ns {
FOLLY_SETTING_DEFINE(
    follytest,
    some_flag,
    std::string,
    "default",
    "Description");
FOLLY_SETTING_DEFINE(
    follytest,
    unused,
    std::string,
    "unused_default",
    "Not used, but should still be in the list");
// Enable to test runtime collision checking logic
#if 0
FOLLY_SETTING_DEFINE(follytest, internal_flag_to_a, std::string,
                     "collision_with_a",
                     "Collision_with_a");
#endif
} // namespace some_ns

TEST(Settings, basic) {
  EXPECT_EQ(a_ns::a_func(), 1245);
  EXPECT_EQ(b_ns::b_func(), "testbasdf");
  EXPECT_EQ(*some_ns::FOLLY_SETTING(follytest, some_flag), "default");
  // Test -> API
  EXPECT_EQ(some_ns::FOLLY_SETTING(follytest, some_flag)->size(), 7);
  a_ns::FOLLY_SETTING(follytest, public_flag_to_a).set(100);
  EXPECT_EQ(*a_ns::FOLLY_SETTING(follytest, public_flag_to_a), 100);
  EXPECT_EQ(a_ns::getRemote(), 100);
  a_ns::setRemote(200);
  EXPECT_EQ(*a_ns::FOLLY_SETTING(follytest, public_flag_to_a), 200);
  EXPECT_EQ(a_ns::getRemote(), 200);

  auto res = folly::settings::getAsString("follytest_public_flag_to_a");
  EXPECT_TRUE(res.hasValue());
  EXPECT_EQ(res->first, "200");
  EXPECT_EQ(res->second, "remote_set");

  {
    auto meta = folly::settings::getSettingsMeta("follytest_public_flag_to_a");
    EXPECT_TRUE(meta.hasValue());
    const auto& md = meta.value();
    EXPECT_EQ(md.project, "follytest");
    EXPECT_EQ(md.name, "public_flag_to_a");
    EXPECT_EQ(md.typeStr, "int");
    EXPECT_EQ(md.typeId, typeid(int));
  }

  {
    auto meta = folly::settings::getSettingsMeta("follytest_some_flag");
    EXPECT_TRUE(meta.hasValue());
    const auto& md = meta.value();
    EXPECT_EQ(md.project, "follytest");
    EXPECT_EQ(md.name, "some_flag");
    EXPECT_EQ(md.typeStr, "std::string");
    EXPECT_EQ(md.typeId, typeid(std::string));
  }

  res = folly::settings::getAsString("follytest_nonexisting");
  EXPECT_FALSE(res.hasValue());

  EXPECT_TRUE(folly::settings::setFromString(
      "follytest_public_flag_to_a", "300", "from_string"));
  EXPECT_EQ(*a_ns::FOLLY_SETTING(follytest, public_flag_to_a), 300);
  EXPECT_EQ(a_ns::getRemote(), 300);
  res = folly::settings::getAsString("follytest_public_flag_to_a");
  EXPECT_TRUE(res.hasValue());
  EXPECT_EQ(res->first, "300");
  EXPECT_EQ(res->second, "from_string");

  EXPECT_FALSE(folly::settings::setFromString(
      "follytest_nonexisting", "300", "from_string"));

  std::string allFlags;
  folly::settings::forEachSetting(
      [&allFlags](
          const folly::settings::SettingMetadata& meta,
          folly::StringPiece value,
          folly::StringPiece reason) {
        if (meta.typeId == typeid(int)) {
          EXPECT_EQ(meta.typeStr, "int");
        } else if (meta.typeId == typeid(std::string)) {
          EXPECT_EQ(meta.typeStr, "std::string");
        } else {
          ASSERT_FALSE(true);
        }
        allFlags += folly::sformat(
            "{}/{}/{}/{}/{}/{}/{}\n",
            meta.project,
            meta.name,
            meta.typeStr,
            meta.defaultStr,
            meta.description,
            value,
            reason);
      });
  EXPECT_EQ(
      allFlags,
      "follytest/internal_flag_to_a/int/789/Desc of int/789/default\n"
      "follytest/internal_flag_to_b/std::string/\"test\"/Desc of str/test/default\n"
      "follytest/public_flag_to_a/int/456/Public flag to a/300/from_string\n"
      "follytest/public_flag_to_b/std::string/\"basdf\"/Public flag to b/basdf/default\n"
      "follytest/some_flag/std::string/\"default\"/Description/default/default\n"
      "follytest/unused/std::string/\"unused_default\"/Not used, but should still be in the list/unused_default/default\n");

  EXPECT_TRUE(folly::settings::resetToDefault("follytest_public_flag_to_a"));
  EXPECT_EQ(*a_ns::FOLLY_SETTING(follytest, public_flag_to_a), 456);
  EXPECT_EQ(a_ns::getRemote(), 456);

  EXPECT_FALSE(folly::settings::resetToDefault("follytest_nonexisting"));
}
