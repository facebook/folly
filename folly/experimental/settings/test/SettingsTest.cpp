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

#include <folly/experimental/settings/Settings.h>

#include <folly/Format.h>
#include <folly/String.h>
#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

#include <folly/experimental/settings/test/a.h>
#include <folly/experimental/settings/test/b.h>

namespace some_ns {
FOLLY_SETTING_DEFINE(
    follytest,
    some_flag,
    std::string,
    "default",
    folly::settings::Mutability::Mutable,
    "Description");
FOLLY_SETTING_DEFINE(
    follytest,
    unused,
    std::string,
    "unused_default",
    folly::settings::Mutability::Mutable,
    "Not used, but should still be in the list");
FOLLY_SETTING_DEFINE(
    follytest,
    multi_token_type,
    unsigned int,
    123,
    folly::settings::Mutability::Mutable,
    "Test that multi-token type names can be used");
// Enable to test runtime collision checking logic
#if 0
FOLLY_SETTING_DEFINE(
    follytest,
    internal_flag_to_a,
    std::string,
    "collision_with_a",
    folly::settings::Mutability::Mutable,
    "Collision_with_a");
#endif

/* Test user defined type support */
struct UserDefinedType {
  explicit UserDefinedType(folly::StringPiece value) {
    if (value == "a") {
      value_ = 0;
    } else if (value == "b") {
      value_ = 100;
    } else {
      throw std::runtime_error("Invalid value passed to UserDefinedType ctor");
    }
  }

  bool operator==(const UserDefinedType& other) const {
    return value_ == other.value_;
  }

  int value_;
};
/* Note: conversion intentionally to different strings to test that this
   function is called */
template <class String>
void toAppend(const UserDefinedType& t, String* out) {
  if (t.value_ == 0) {
    out->append("a_out");
  } else if (t.value_ == 100) {
    out->append("b_out");
  } else {
    throw std::runtime_error("Can't convert UserDefinedType to string");
  }
}

FOLLY_SETTING_DEFINE(
    follytest,
    user_defined,
    UserDefinedType,
    "b",
    folly::settings::Mutability::Mutable,
    "User defined type constructed from string");

FOLLY_SETTING_DEFINE(
    follytest,
    immutable_setting,
    UserDefinedType,
    "b",
    folly::settings::Mutability::Immutable,
    "User defined type constructed from string");
FOLLY_SETTING_DEFINE(
    otherproj,
    some_flag,
    std::string,
    "default",
    folly::settings::Mutability::Mutable,
    "Description");

} // namespace some_ns
namespace {
MATCHER(IsOk, "success but got error") {
  if (arg.hasError()) {
    *result_listener << "which is: '"
                     << ::folly::settings::toString(arg.error()) << "'";
    return false;
  }
  return true;
}
MATCHER_P(
    HasErrorCode,
    error_code,
    "error '" + std::string(::folly::settings::toString(error_code)) + "'") {
  if (!arg.hasError()) {
    *result_listener << "which is not an error";
    return false;
  }
  if (arg.error() != error_code) {
    *result_listener << "which is: '"
                     << ::folly::settings::toString(arg.error()) << "'";
  }
  return true;
}
} // namespace

TEST(Settings, userDefined) {
  EXPECT_EQ(some_ns::FOLLY_SETTING(follytest, user_defined)->value_, 100);
  {
    folly::settings::Snapshot sn;
    EXPECT_THAT(
        sn.setFromString("follytest_user_defined", "a", "test"), IsOk());
    sn.publish();
    EXPECT_EQ(some_ns::FOLLY_SETTING(follytest, user_defined)->value_, 0);
  }
  {
    folly::settings::Snapshot sn;
    auto info = sn.getAsString("follytest_user_defined");
    EXPECT_TRUE(info.has_value());
    EXPECT_EQ(info->first, "a_out");
    EXPECT_EQ(info->second, "test");
  }
  {
    folly::settings::Snapshot sn;
    EXPECT_THROW(
        sn.setFromString("follytest_user_defined", "c", "test2"),
        std::runtime_error);
    sn.publish();
    EXPECT_EQ(some_ns::FOLLY_SETTING(follytest, user_defined)->value_, 0);
  }
  {
    folly::settings::Snapshot sn;
    auto info = sn.getAsString("follytest_user_defined");
    EXPECT_TRUE(info.has_value());
    EXPECT_EQ(info->first, "a_out");
    EXPECT_EQ(info->second, "test");
  }
  {
    folly::settings::Snapshot sn;
    EXPECT_THAT(sn.resetToDefault("follytest_user_defined"), IsOk());
    sn.publish();
    EXPECT_EQ(some_ns::FOLLY_SETTING(follytest, user_defined)->value_, 100);
  }
  {
    folly::settings::Snapshot sn;
    auto info = sn.getAsString("follytest_user_defined");
    EXPECT_TRUE(info.has_value());
    EXPECT_EQ(info->first, "b_out");
    EXPECT_EQ(info->second, "default");
  }
  /* Test that intentionally setting to something non-converteable fails */
  some_ns::UserDefinedType bad("a");
  bad.value_ = 50;
  EXPECT_THROW(
      some_ns::FOLLY_SETTING(follytest, user_defined).set(bad),
      std::runtime_error);
  EXPECT_EQ(some_ns::FOLLY_SETTING(follytest, user_defined)->value_, 100);
  {
    folly::settings::Snapshot sn;
    auto info = sn.getAsString("follytest_user_defined");
    EXPECT_TRUE(info.has_value());
    EXPECT_EQ(info->first, "b_out");
    EXPECT_EQ(info->second, "default");
  }
}

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
  {
    folly::settings::Snapshot sn;
    auto res = sn.getAsString("follytest_public_flag_to_a");
    EXPECT_TRUE(res.has_value());
    EXPECT_EQ(res->first, "200");
    EXPECT_EQ(res->second, "remote_set");
  }
  {
    auto meta = folly::settings::getSettingsMeta("follytest_public_flag_to_a");
    EXPECT_TRUE(meta.has_value());
    const auto& md = meta.value();
    EXPECT_EQ(md.project, "follytest");
    EXPECT_EQ(md.name, "public_flag_to_a");
    EXPECT_EQ(md.typeStr, "int");
    EXPECT_EQ(md.typeId, typeid(int));
  }
  {
    auto meta = folly::settings::getSettingsMeta("follytest_some_flag");
    EXPECT_TRUE(meta.has_value());
    const auto& md = meta.value();
    EXPECT_EQ(md.project, "follytest");
    EXPECT_EQ(md.name, "some_flag");
    EXPECT_EQ(md.typeStr, "std::string");
    EXPECT_EQ(md.typeId, typeid(std::string));
  }
  {
    folly::settings::Snapshot sn;
    auto res = sn.getAsString("follytest_nonexisting");
    EXPECT_FALSE(res.has_value());
  }
  {
    folly::settings::Snapshot sn;
    EXPECT_THAT(
        sn.setFromString("follytest_public_flag_to_a", "300", "from_string"),
        IsOk());
    sn.publish();
    EXPECT_EQ(*a_ns::FOLLY_SETTING(follytest, public_flag_to_a), 300);
  }
  EXPECT_EQ(a_ns::getRemote(), 300);
  {
    folly::settings::Snapshot sn;
    auto res = sn.getAsString("follytest_public_flag_to_a");
    EXPECT_TRUE(res.has_value());
    EXPECT_EQ(res->first, "300");
    EXPECT_EQ(res->second, "from_string");
  }
  {
    folly::settings::Snapshot sn;
    EXPECT_THAT(
        sn.setFromString("follytest_nonexisting", "300", "from_string"),
        HasErrorCode(folly::settings::SetErrorCode::NotFound));
  }
  EXPECT_EQ(
      some_ns::FOLLY_SETTING(follytest, multi_token_type).defaultValue(), 123);
  EXPECT_EQ(
      a_ns::FOLLY_SETTING(follytest, public_flag_to_a).defaultValue(), 456);
  EXPECT_EQ(
      b_ns::FOLLY_SETTING(follytest, public_flag_to_b).defaultValue(), "basdf");
  EXPECT_EQ(
      some_ns::FOLLY_SETTING(follytest, some_flag).defaultValue(), "default");
  EXPECT_EQ(
      some_ns::FOLLY_SETTING(follytest, user_defined).defaultValue(),
      some_ns::UserDefinedType("b"));
  {
    std::string allFlags;
    folly::settings::Snapshot sn;
    sn.forEachSetting([&allFlags](
                          const folly::settings::SettingMetadata& meta,
                          folly::StringPiece value,
                          folly::StringPiece reason) {
      if (meta.typeId == typeid(int)) {
        EXPECT_EQ(meta.typeStr, "int");
      } else if (meta.typeId == typeid(std::string)) {
        EXPECT_EQ(meta.typeStr, "std::string");
      } else if (meta.typeId == typeid(unsigned int)) {
        EXPECT_EQ(meta.typeStr, "unsigned int");
      } else if (meta.typeId == typeid(some_ns::UserDefinedType)) {
        EXPECT_EQ(meta.typeStr, "UserDefinedType");
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
    auto allFlagsString = folly::stripLeftMargin(R"MESSAGE(
      follytest/immutable_setting/UserDefinedType/"b"/User defined type constructed from string/b_out/default
      follytest/internal_flag_to_a/int/789/Desc of int/789/default
      follytest/internal_flag_to_b/std::string/"test"/Desc of str/test/default
      follytest/multi_token_type/unsigned int/123/Test that multi-token type names can be used/123/default
      follytest/public_flag_to_a/int/456/Public flag to a/300/from_string
      follytest/public_flag_to_b/std::string/"basdf"/Public flag to b/basdf/default
      follytest/some_flag/std::string/"default"/Description/default/default
      follytest/unused/std::string/"unused_default"/Not used, but should still be in the list/unused_default/default
      follytest/user_defined/UserDefinedType/"b"/User defined type constructed from string/b_out/default
      otherproj/some_flag/std::string/"default"/Description/default/default
  )MESSAGE");
    EXPECT_EQ(allFlags, allFlagsString);
  }
  {
    folly::settings::Snapshot sn;
    EXPECT_THAT(sn.resetToDefault("follytest_public_flag_to_a"), IsOk());
    sn.publish();
    EXPECT_EQ(*a_ns::FOLLY_SETTING(follytest, public_flag_to_a), 456);
    EXPECT_EQ(a_ns::getRemote(), 456);
  }
  {
    folly::settings::Snapshot sn;
    EXPECT_THAT(
        sn.resetToDefault("follytest_nonexisting"),
        HasErrorCode(folly::settings::SetErrorCode::NotFound));
  }
}

TEST(Settings, snapshot) {
  // Test discarding a snapshot
  {
    folly::settings::Snapshot snapshot;

    EXPECT_EQ(*some_ns::FOLLY_SETTING(follytest, some_flag), "default");
    EXPECT_EQ(
        *snapshot(some_ns::FOLLY_SETTING(follytest, some_flag)), "default");

    // Set the global value, snapshot doesn't see it
    some_ns::FOLLY_SETTING(follytest, some_flag).set("global_value");
    EXPECT_EQ(*some_ns::FOLLY_SETTING(follytest, some_flag), "global_value");
    EXPECT_EQ(
        *snapshot(some_ns::FOLLY_SETTING(follytest, some_flag)), "default");
    EXPECT_EQ(
        some_ns::FOLLY_SETTING(follytest, some_flag).value(snapshot),
        "default");

    // Set the value in the snapshot only
    snapshot(some_ns::FOLLY_SETTING(follytest, some_flag))
        .set("snapshot_value");
    EXPECT_EQ(*some_ns::FOLLY_SETTING(follytest, some_flag), "global_value");
    EXPECT_EQ(
        some_ns::FOLLY_SETTING(follytest, some_flag).value(), "global_value");
    EXPECT_EQ(
        *snapshot(some_ns::FOLLY_SETTING(follytest, some_flag)),
        "snapshot_value");
    EXPECT_EQ(
        some_ns::FOLLY_SETTING(follytest, some_flag).value(snapshot),
        "snapshot_value");
  }
  // Discard the snapshot
  EXPECT_EQ(*some_ns::FOLLY_SETTING(follytest, some_flag), "global_value");

  // Test publishing a snapshot
  {
    folly::settings::Snapshot snapshot;

    // Set the value in the snapshot only
    EXPECT_EQ(*some_ns::FOLLY_SETTING(follytest, some_flag), "global_value");
    EXPECT_EQ(
        *snapshot(some_ns::FOLLY_SETTING(follytest, some_flag)),
        "global_value");
    EXPECT_EQ(
        some_ns::FOLLY_SETTING(follytest, some_flag).value(snapshot),
        "global_value");
    snapshot(some_ns::FOLLY_SETTING(follytest, some_flag))
        .set("snapshot_value2");
    EXPECT_EQ(*some_ns::FOLLY_SETTING(follytest, some_flag), "global_value");
    EXPECT_EQ(
        *snapshot(some_ns::FOLLY_SETTING(follytest, some_flag)),
        "snapshot_value2");
    EXPECT_EQ(
        some_ns::FOLLY_SETTING(follytest, some_flag).value(snapshot),
        "snapshot_value2");

    // Set the global value, snapshot doesn't see it
    some_ns::FOLLY_SETTING(follytest, some_flag).set("global_value2");
    EXPECT_EQ(*some_ns::FOLLY_SETTING(follytest, some_flag), "global_value2");
    EXPECT_EQ(
        *snapshot(some_ns::FOLLY_SETTING(follytest, some_flag)),
        "snapshot_value2");
    EXPECT_EQ(
        some_ns::FOLLY_SETTING(follytest, some_flag).value(snapshot),
        "snapshot_value2");
    snapshot.publish();
  }

  EXPECT_EQ(*some_ns::FOLLY_SETTING(follytest, some_flag), "snapshot_value2");

  // Snapshots at different points in time
  {
    some_ns::FOLLY_SETTING(follytest, some_flag).set("a");
    a_ns::FOLLY_SETTING(follytest, public_flag_to_a).set(123);

    folly::settings::Snapshot snapshot_1;

    EXPECT_EQ(*some_ns::FOLLY_SETTING(follytest, some_flag), "a");
    EXPECT_EQ(some_ns::FOLLY_SETTING(follytest, some_flag).value(), "a");
    EXPECT_EQ(*snapshot_1(some_ns::FOLLY_SETTING(follytest, some_flag)), "a");
    EXPECT_EQ(
        some_ns::FOLLY_SETTING(follytest, some_flag).value(snapshot_1), "a");

    EXPECT_EQ(*a_ns::FOLLY_SETTING(follytest, public_flag_to_a), 123);
    EXPECT_EQ(a_ns::FOLLY_SETTING(follytest, public_flag_to_a).value(), 123);
    EXPECT_EQ(
        *snapshot_1(a_ns::FOLLY_SETTING(follytest, public_flag_to_a)), 123);
    EXPECT_EQ(
        a_ns::FOLLY_SETTING(follytest, public_flag_to_a).value(snapshot_1),
        123);

    some_ns::FOLLY_SETTING(follytest, some_flag).set("b");
    EXPECT_EQ(*some_ns::FOLLY_SETTING(follytest, some_flag), "b");
    EXPECT_EQ(some_ns::FOLLY_SETTING(follytest, some_flag).value(), "b");
    EXPECT_EQ(*snapshot_1(some_ns::FOLLY_SETTING(follytest, some_flag)), "a");
    EXPECT_EQ(
        some_ns::FOLLY_SETTING(follytest, some_flag).value(snapshot_1), "a");

    EXPECT_EQ(*a_ns::FOLLY_SETTING(follytest, public_flag_to_a), 123);
    EXPECT_EQ(a_ns::FOLLY_SETTING(follytest, public_flag_to_a).value(), 123);
    EXPECT_EQ(
        *snapshot_1(a_ns::FOLLY_SETTING(follytest, public_flag_to_a)), 123);
    EXPECT_EQ(
        a_ns::FOLLY_SETTING(follytest, public_flag_to_a).value(snapshot_1),
        123);

    folly::settings::Snapshot snapshot_2;
    EXPECT_EQ(*some_ns::FOLLY_SETTING(follytest, some_flag), "b");
    EXPECT_EQ(some_ns::FOLLY_SETTING(follytest, some_flag).value(), "b");
    EXPECT_EQ(*snapshot_1(some_ns::FOLLY_SETTING(follytest, some_flag)), "a");
    EXPECT_EQ(*snapshot_2(some_ns::FOLLY_SETTING(follytest, some_flag)), "b");
    EXPECT_EQ(
        some_ns::FOLLY_SETTING(follytest, some_flag).value(snapshot_1), "a");
    EXPECT_EQ(
        some_ns::FOLLY_SETTING(follytest, some_flag).value(snapshot_2), "b");

    EXPECT_EQ(*a_ns::FOLLY_SETTING(follytest, public_flag_to_a), 123);
    EXPECT_EQ(a_ns::FOLLY_SETTING(follytest, public_flag_to_a).value(), 123);
    EXPECT_EQ(
        *snapshot_1(a_ns::FOLLY_SETTING(follytest, public_flag_to_a)), 123);
    EXPECT_EQ(
        *snapshot_2(a_ns::FOLLY_SETTING(follytest, public_flag_to_a)), 123);
    EXPECT_EQ(
        a_ns::FOLLY_SETTING(follytest, public_flag_to_a).value(snapshot_1),
        123);
    EXPECT_EQ(
        a_ns::FOLLY_SETTING(follytest, public_flag_to_a).value(snapshot_2),
        123);

    some_ns::FOLLY_SETTING(follytest, some_flag).set("c");
    EXPECT_EQ(*some_ns::FOLLY_SETTING(follytest, some_flag), "c");
    EXPECT_EQ(some_ns::FOLLY_SETTING(follytest, some_flag).value(), "c");
    EXPECT_EQ(*snapshot_1(some_ns::FOLLY_SETTING(follytest, some_flag)), "a");
    EXPECT_EQ(*snapshot_2(some_ns::FOLLY_SETTING(follytest, some_flag)), "b");
    EXPECT_EQ(
        some_ns::FOLLY_SETTING(follytest, some_flag).value(snapshot_1), "a");
    EXPECT_EQ(
        some_ns::FOLLY_SETTING(follytest, some_flag).value(snapshot_2), "b");

    EXPECT_EQ(*a_ns::FOLLY_SETTING(follytest, public_flag_to_a), 123);
    EXPECT_EQ(a_ns::FOLLY_SETTING(follytest, public_flag_to_a).value(), 123);
    EXPECT_EQ(
        *snapshot_1(a_ns::FOLLY_SETTING(follytest, public_flag_to_a)), 123);
    EXPECT_EQ(
        *snapshot_2(a_ns::FOLLY_SETTING(follytest, public_flag_to_a)), 123);
    EXPECT_EQ(
        a_ns::FOLLY_SETTING(follytest, public_flag_to_a).value(snapshot_1),
        123);
    EXPECT_EQ(
        a_ns::FOLLY_SETTING(follytest, public_flag_to_a).value(snapshot_2),
        123);

    a_ns::FOLLY_SETTING(follytest, public_flag_to_a).set(456);
    EXPECT_EQ(*some_ns::FOLLY_SETTING(follytest, some_flag), "c");
    EXPECT_EQ(some_ns::FOLLY_SETTING(follytest, some_flag).value(), "c");
    EXPECT_EQ(*snapshot_1(some_ns::FOLLY_SETTING(follytest, some_flag)), "a");
    EXPECT_EQ(*snapshot_2(some_ns::FOLLY_SETTING(follytest, some_flag)), "b");
    EXPECT_EQ(
        some_ns::FOLLY_SETTING(follytest, some_flag).value(snapshot_1), "a");
    EXPECT_EQ(
        some_ns::FOLLY_SETTING(follytest, some_flag).value(snapshot_2), "b");

    EXPECT_EQ(*a_ns::FOLLY_SETTING(follytest, public_flag_to_a), 456);
    EXPECT_EQ(a_ns::FOLLY_SETTING(follytest, public_flag_to_a).value(), 456);
    EXPECT_EQ(
        *snapshot_1(a_ns::FOLLY_SETTING(follytest, public_flag_to_a)), 123);
    EXPECT_EQ(
        *snapshot_2(a_ns::FOLLY_SETTING(follytest, public_flag_to_a)), 123);
    EXPECT_EQ(
        a_ns::FOLLY_SETTING(follytest, public_flag_to_a).value(snapshot_1),
        123);
    EXPECT_EQ(
        a_ns::FOLLY_SETTING(follytest, public_flag_to_a).value(snapshot_2),
        123);
  }
}

TEST(SettingsTest, callback) {
  size_t callbackInvocations = 0;
  std::string lastCallbackValue;

  EXPECT_EQ(*some_ns::FOLLY_SETTING(follytest, some_flag), "default");
  {
    auto handle = some_ns::FOLLY_SETTING(follytest, some_flag)
                      .addCallback([&](const auto& contents) {
                        ++callbackInvocations;
                        lastCallbackValue = contents.value;
                      });

    some_ns::FOLLY_SETTING(follytest, some_flag).set("a");
    EXPECT_EQ(callbackInvocations, 1);
    EXPECT_EQ(lastCallbackValue, "a");

    size_t secondCallbackInvocations = 0;
    // Test adding multiple callbacks and letting the handle go out of scope
    {
      auto secondHandle = some_ns::FOLLY_SETTING(follytest, some_flag)
                              .addCallback([&](const auto& /* contents */) {
                                ++secondCallbackInvocations;
                              });
      some_ns::FOLLY_SETTING(follytest, some_flag).set("b");
      EXPECT_EQ(callbackInvocations, 2);
      EXPECT_EQ(lastCallbackValue, "b");
      EXPECT_EQ(secondCallbackInvocations, 1);
    }

    some_ns::FOLLY_SETTING(follytest, some_flag).set("c");
    EXPECT_EQ(callbackInvocations, 3);
    EXPECT_EQ(lastCallbackValue, "c");
    // Second callback no longer invoked
    EXPECT_EQ(secondCallbackInvocations, 1);

    auto movedHandle = std::move(handle);
    some_ns::FOLLY_SETTING(follytest, some_flag).set("d");
    EXPECT_EQ(callbackInvocations, 4);
    EXPECT_EQ(lastCallbackValue, "d");
  }
  // Main callback no longer invoked
  some_ns::FOLLY_SETTING(follytest, some_flag).set("e");
  EXPECT_EQ(callbackInvocations, 4);
  EXPECT_EQ(lastCallbackValue, "d");
}

TEST(Settings, immutables) {
  EXPECT_EQ(some_ns::FOLLY_SETTING(follytest, immutable_setting)->value_, 100);
  EXPECT_EQ(*some_ns::FOLLY_SETTING(otherproj, some_flag), "default");

  // Check these can be changed before immutables are frozen
  folly::settings::Snapshot sn;
  EXPECT_THAT(
      sn.setFromString("follytest_immutable_setting", "a", "test"), IsOk());
  EXPECT_THAT(
      sn.setFromString("otherproj_some_flag", "value_1", "test"), IsOk());
  sn.publish();
  EXPECT_EQ(some_ns::FOLLY_SETTING(follytest, immutable_setting)->value_, 0);
  EXPECT_EQ(*some_ns::FOLLY_SETTING(otherproj, some_flag), "value_1");

  // Create a snapshot before freezing immutables
  folly::settings::Snapshot beforeFreezeSn;
  EXPECT_THAT(
      beforeFreezeSn.resetToDefault("follytest_immutable_setting"), IsOk());
  EXPECT_THAT(
      beforeFreezeSn.setFromString("follytest_immutable_setting", "b", "test"),
      IsOk());
  EXPECT_THAT(
      beforeFreezeSn.setFromString("otherproj_some_flag", "value_2", "test"),
      IsOk());

  ASSERT_FALSE(folly::settings::immutablesFrozen("follytest"));
  ASSERT_FALSE(folly::settings::frozenSettingProjects().contains("follytest"));

  // Freeze "follytest" immutable settings
  folly::settings::freezeImmutables({"follytest"});

  ASSERT_TRUE(folly::settings::immutablesFrozen("follytest"));
  ASSERT_TRUE(folly::settings::frozenSettingProjects().contains("follytest"));
  ASSERT_FALSE(folly::settings::immutablesFrozen("otherproj"));
  ASSERT_FALSE(folly::settings::frozenSettingProjects().contains("otherproj"));

  // Check that even though 'immutable_setting' was successfully set before
  // settings were frozen, it still can't publish the new value now that
  // immutables are frozen. However, it still can change otherproj_some_flag
  // since that project has not been frozen yet.
  beforeFreezeSn.publish();
  EXPECT_EQ(some_ns::FOLLY_SETTING(follytest, immutable_setting)->value_, 0);
  EXPECT_EQ(*some_ns::FOLLY_SETTING(otherproj, some_flag), "value_2");

  // Check newly created snapshots fail mutation attempts.
  folly::settings::Snapshot afterFreezeSn;
  EXPECT_THAT(
      afterFreezeSn.setFromString("follytest_immutable_setting", "b", "test"),
      HasErrorCode(folly::settings::SetErrorCode::FrozenImmutable));
  EXPECT_THAT(
      afterFreezeSn.resetToDefault("follytest_immutable_setting"),
      HasErrorCode(folly::settings::SetErrorCode::FrozenImmutable));
  EXPECT_THAT(
      some_ns::FOLLY_SETTING(follytest, immutable_setting)
          .set(some_ns::UserDefinedType("b")),
      HasErrorCode(folly::settings::SetErrorCode::FrozenImmutable));

  // Check we don't attempt to contruct immutable settings from strings if
  // immutables have already been frozen. If we did, the below code would throw
  // an exception.
  some_ns::UserDefinedType bad("a");
  bad.value_ = 50;
  EXPECT_THAT(
      some_ns::FOLLY_SETTING(follytest, immutable_setting).set(bad),
      HasErrorCode(folly::settings::SetErrorCode::FrozenImmutable));
  EXPECT_THAT(
      afterFreezeSn.setFromString("follytest_immutable_setting", "c", "test"),
      HasErrorCode(folly::settings::SetErrorCode::FrozenImmutable));

  // Check forceSetFromString does construct immutable settings even if
  // immutables have already been frozen.
  try {
    afterFreezeSn.forceSetFromString("follytest_immutable_setting", "c", "ds");
    FAIL();
  } catch (const std::runtime_error& ex) {
    EXPECT_STREQ(ex.what(), "Invalid value passed to UserDefinedType ctor");
  }

  {
    // Check forceSetFromString does construct immutable settings even if
    // immutables have already been frozen.
    folly::settings::Snapshot newSnapshot;
    try {
      newSnapshot.forceSetFromString("follytest_immutable_setting", "c", "dsc");
      FAIL();
    } catch (const std::runtime_error& ex) {
      EXPECT_STREQ(ex.what(), "Invalid value passed to UserDefinedType ctor");
    }

    // Check forceSetFromString can set immutable settings in the snapshot but
    // will still not publish them
    EXPECT_THAT(
        newSnapshot.forceSetFromString(
            "follytest_immutable_setting", "b", "new desc"),
        IsOk());
    auto found = newSnapshot.getAsString("follytest_immutable_setting");
    ASSERT_TRUE(found.hasValue());
    EXPECT_EQ(found->first, "b_out");
    EXPECT_EQ(found->second, "new desc");
    newSnapshot.publish();
    EXPECT_EQ(some_ns::FOLLY_SETTING(follytest, immutable_setting)->value_, 0);
  }

  {
    // Check forceResetToDefault can reset immutable settings in the snapshot
    // but will still not publish them
    folly::settings::Snapshot newSnapshot;
    auto found = newSnapshot.getAsString("follytest_immutable_setting");
    ASSERT_TRUE(found.hasValue());
    EXPECT_EQ(found->first, "a_out");
    EXPECT_EQ(found->second, "test");
    EXPECT_THAT(
        newSnapshot.forceResetToDefault("follytest_immutable_setting"), IsOk());
    found = newSnapshot.getAsString("follytest_immutable_setting");
    ASSERT_TRUE(found.hasValue());
    EXPECT_EQ(found->first, "b_out");
    EXPECT_EQ(found->second, "default");
    newSnapshot.publish();
    EXPECT_EQ(some_ns::FOLLY_SETTING(follytest, immutable_setting)->value_, 0);
  }
}
