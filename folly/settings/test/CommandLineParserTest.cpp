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

#include <string>
#include <string_view>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <folly/Conv.h>
#include <folly/String.h>
#include <folly/container/Array.h>
#include <folly/logging/xlog.h>
#include <folly/settings/CommandLineParser.h>
#include <folly/settings/Settings.h>
#include <folly/settings/SettingsAccessorProxy.h>

namespace folly::settings::test {

FOLLY_SETTING_DEFINE(
    test,
    bool_opt,
    bool,
    false,
    Mutability::Mutable,
    CommandLine::AcceptOverrides,
    "Bool option");
FOLLY_SETTING_DEFINE(
    test,
    int_opt,
    int64_t,
    1,
    Mutability::Mutable,
    CommandLine::AcceptOverrides,
    "Int option");
FOLLY_SETTING_DEFINE(
    test,
    string_opt,
    std::string,
    "blah",
    Mutability::Mutable,
    CommandLine::AcceptOverrides,
    "String option");
FOLLY_SETTING_DEFINE(
    test,
    float_opt,
    float,
    1,
    Mutability::Mutable,
    CommandLine::AcceptOverrides,
    "Float option");
FOLLY_SETTING_DEFINE(
    test,
    double_opt,
    double,
    1,
    Mutability::Mutable,
    CommandLine::AcceptOverrides,
    "Double option");
FOLLY_SETTING_DEFINE(
    test,
    int_opt_reject_overrides,
    int64_t,
    5,
    Mutability::Mutable,
    CommandLine::RejectOverrides,
    "Int option");

void check_args(int argc, char** argv, const std::vector<const char*>& expect) {
  ASSERT_EQ(argc, expect.size());

  for (int i = 0; i < argc; ++i) {
    EXPECT_STREQ(argv[i], expect[i]);
  }
}

template <typename Array>
void check_args_parsing(
    Array args,
    const std::vector<const char*>& expected,
    Function<void()> settingValueChecker = nullptr,
    ArgParsingResult parsingResult = ArgParsingResult::HELP,
    std::string_view project = "",
    const std::unordered_map<std::string, std::string>& aliases = {}) {
  XLOG(ERR) << "CHECKING " << join(" ", args);
  int argc = args.size();
  char** argv = const_cast<char**>(args.data());

  {
    Snapshot snap;
    SettingsAccessorProxy flags_info(snap, project, aliases);
    EXPECT_EQ(CommandLineParser(argc, argv, flags_info).parse(), parsingResult);
    if (parsingResult != ArgParsingResult::ERROR) {
      snap.publish();
    }
  }

  if (settingValueChecker) {
    settingValueChecker();
  }

  XLOG(ERR) << "AFTER " << join(" ", argv, argv + argc);
  check_args(argc, argv, expected);

  {
    Snapshot snap;
    snap.forEachSetting([&](const auto& setting) {
      snap.resetToDefault(setting.fullName());
    });
    snap.publish();
  }
}

TEST(CommandLineParserTest, Basic) {
  const char *int_val = "10", *double_val = "9.999999999", *float_val = "10.2",
             *string_val = "some_string";
  const char* program = "/bin/program";

  // With no args
  check_args_parsing(
      make_array<const char*>(program),
      {program},
      nullptr,
      ArgParsingResult::OK);

  // Just help
  check_args_parsing(
      make_array<const char*>(program, "--help"), {program, "--help"});

  // Help with value
  check_args_parsing(
      make_array<const char*>(program, "--help", "true"),
      {program, "--help", "true"});

  // Set int flag
  check_args_parsing(
      make_array<const char*>(program, "--help", "--test_int_opt", int_val),
      {program, "--help"},
      [&] {
        EXPECT_EQ(*FOLLY_SETTING(test, int_opt), folly::to<int>(int_val));
      });

  // Set int flag but it rejects the command line override
  check_args_parsing(
      make_array<const char*>(
          program, "--help", "--test_int_opt_reject_overrides", int_val),
      {program, "--help", "--test_int_opt_reject_overrides", int_val},
      [&] { EXPECT_EQ(*FOLLY_SETTING(test, int_opt_reject_overrides), 5); });

  // Set bool flag with no value
  check_args_parsing(
      make_array<const char*>(program, "--help", "--test_bool_opt"),
      {program, "--help"},
      [&] { EXPECT_TRUE(*FOLLY_SETTING(test, bool_opt)); });

  // Set bool flag with value
  check_args_parsing(
      make_array<const char*>(program, "--help", "--test_bool_opt", "true"),
      {program, "--help"},
      [&] { EXPECT_TRUE(*FOLLY_SETTING(test, bool_opt)); });

  // Unknown arg
  check_args_parsing(
      make_array<const char*>(
          program,
          "--help",
          "--unknown_opt",
          "test",
          "--test_int_opt",
          int_val),
      {program, "--help", "--unknown_opt", "test"},
      [&] {
        EXPECT_EQ(*FOLLY_SETTING(test, int_opt), folly::to<int>(int_val));
      });

  // Unknown arg
  check_args_parsing(
      make_array<const char*>(
          program,
          "--help",
          "--test_int_opt",
          int_val,
          "--unknown_opt",
          "test"),
      {program, "--help", "--unknown_opt", "test"},
      [&] {
        EXPECT_EQ(*FOLLY_SETTING(test, int_opt), folly::to<int>(int_val));
      });

  // All flags with unknown
  check_args_parsing(
      make_array<const char*>(
          program,
          "--help",
          "--test_bool_opt",
          "--test_int_opt",
          int_val,
          "--unknown_opt",
          "test",
          "--test_string_opt",
          string_val,
          "--test_float_opt",
          float_val,
          "--test_double_opt",
          double_val),
      {program, "--help", "--unknown_opt", "test"},
      [&] {
        EXPECT_EQ(*FOLLY_SETTING(test, int_opt), folly::to<int>(int_val));
        EXPECT_TRUE(*FOLLY_SETTING(test, bool_opt));
        EXPECT_EQ(
            *FOLLY_SETTING(test, string_opt),
            folly::to<std::string>(string_val));
        EXPECT_EQ(*FOLLY_SETTING(test, float_opt), folly::to<float>(float_val));
        EXPECT_EQ(
            *FOLLY_SETTING(test, double_opt), folly::to<double>(double_val));
      });

  // With positional argument
  check_args_parsing(
      make_array<const char*>(
          program,
          "--help",
          "--test_bool_opt",
          "--test_int_opt",
          int_val,
          "test"),
      {program, "--help", "test"},
      [&] {
        EXPECT_EQ(*FOLLY_SETTING(test, int_opt), folly::to<int>(int_val));
        EXPECT_TRUE(*FOLLY_SETTING(test, bool_opt));
      });

  // With '--' normal glags which we couldn't parse should be ordered before
  // '--', everything after '--' should remain the same
  check_args_parsing(
      make_array<const char*>(
          program,
          "--help",
          "--some_opt",
          "10",
          "--some_bool_opt",
          "--test_int_opt",
          int_val,
          "test",
          "--",
          "not_arg"),
      {
          program,
          "--help",
          "--some_opt",
          "10",
          "--some_bool_opt",
          "test",
          "--",
          "not_arg",
      },
      [&] {
        EXPECT_EQ(*FOLLY_SETTING(test, int_opt), folly::to<int>(int_val));
      });

  // Failed to parse value, non of the values should update
  check_args_parsing(
      make_array<const char*>(
          program, "--help", "--test_float_opt", float_val, "--test_int_opt"),
      {program, "--help", "--test_int_opt"},
      [float_exp = *FOLLY_SETTING(test, float_opt),
       int_exp = *FOLLY_SETTING(test, int_opt)] {
        EXPECT_EQ(*FOLLY_SETTING(test, float_opt), folly::to<int>(float_exp));
        EXPECT_EQ(*FOLLY_SETTING(test, int_opt), folly::to<int>(int_exp));
      },
      ArgParsingResult::ERROR);
}

TEST(CommandLineParserTest, DefaultProject) {
  const char *int_val = "10", *double_val = "9.999999999", *float_val = "10.2",
             *string_val = "some_string";
  const char* program = "/bin/program";
  check_args_parsing(
      make_array<const char*>(
          program,
          "--help",
          "--bool_opt",
          "--int_opt",
          int_val,
          "--unknown_opt",
          "test",
          "--string_opt",
          string_val,
          "--float_opt",
          float_val,
          "--double_opt",
          double_val),
      {program, "--help", "--unknown_opt", "test"},
      [&] {
        EXPECT_EQ(*FOLLY_SETTING(test, int_opt), folly::to<int>(int_val));
        EXPECT_TRUE(*FOLLY_SETTING(test, bool_opt));
        EXPECT_EQ(
            *FOLLY_SETTING(test, string_opt),
            folly::to<std::string>(string_val));
        EXPECT_EQ(*FOLLY_SETTING(test, float_opt), folly::to<float>(float_val));
        EXPECT_EQ(
            *FOLLY_SETTING(test, double_opt), folly::to<double>(double_val));
      },
      ArgParsingResult::HELP,
      "test");
}
TEST(CommandLineParserTest, ArgAliases) {
  const char *int_val = "101", *double_val = "9.9991111999",
             *float_val = "10.9999", *string_val = "some_string_short";
  const char* program = "/bin/program";
  check_args_parsing(
      make_array<const char*>(
          program,
          "--help",
          "-b",
          "-i",
          int_val,
          "--unknown_opt",
          "test",
          "-s",
          string_val,
          "-f",
          float_val,
          "-d",
          double_val),
      {program, "--help", "--unknown_opt", "test"},
      [&] {
        EXPECT_EQ(*FOLLY_SETTING(test, int_opt), folly::to<int>(int_val));
        EXPECT_TRUE(*FOLLY_SETTING(test, bool_opt));
        EXPECT_EQ(
            *FOLLY_SETTING(test, string_opt),
            folly::to<std::string>(string_val));
        EXPECT_EQ(*FOLLY_SETTING(test, float_opt), folly::to<float>(float_val));
        EXPECT_EQ(
            *FOLLY_SETTING(test, double_opt), folly::to<double>(double_val));
      },
      ArgParsingResult::HELP,
      "test",
      {{"i", "test_int_opt"},
       {"b", "bool_opt"},
       {"f", "float_opt"},
       {"d", "test_double_opt"},
       {"s", "string_opt"}});
}

struct LegacySettings {
  int lIntSetting{0};
  double lDoubleSetting{0};
  std::string lStringSetting;
};

LegacySettings gSettingsLegacy;

struct CustomFlagsContained : public SettingsAccessorProxy {
  explicit CustomFlagsContained(
      Snapshot& snapshot,
      std::string_view project = "",
      SettingsAccessorProxy::SettingAliases aliases = {})
      : SettingsAccessorProxy(snapshot, project, std::move(aliases)) {
    legacySetters_ = {
        {"test_legacy_int_setting",
         [](std::string_view val) {
           auto res = folly::tryTo<int>(val);
           if (res) {
             gSettingsLegacy.lIntSetting = res.value();
             return true;
           }
           return true;
         }},
        {"test_legacy_double_setting",
         [](std::string_view val) {
           auto res = folly::tryTo<double>(val);
           if (res) {
             gSettingsLegacy.lDoubleSetting = res.value();
             return true;
           }
           return false;
         }},
        {"test_legacy_string_setting", [](std::string_view val) {
           gSettingsLegacy.lStringSetting = std::string(val);
           return true;
         }}};

    settingsMeta_.emplace(
        "test_legacy_int_setting",
        SettingMetadata{
            "test",
            "legacy_int_setting",
            "int",
            typeid(int),
            "0",
            Mutability::Mutable,
            CommandLine::AcceptOverrides,
            "Legacy int option",
        });
    settingsMeta_.emplace(
        "test_legacy_double_setting",
        SettingMetadata{
            "test",
            "legacy_double_setting",
            "int",
            typeid(double),
            "0",
            Mutability::Mutable,
            CommandLine::AcceptOverrides,
            "Legacy double option",
        });
    settingsMeta_.emplace(
        "test_legacy_string_setting",
        SettingMetadata{
            "test",
            "legacy_string_setting",
            "int",
            typeid(std::string),
            "",
            Mutability::Mutable,
            CommandLine::AcceptOverrides,
            "Legacy string option",
        });
  }

  SetResult setFromString(
      std::string_view settingName,
      std::string_view newValue,
      std::string_view reason) override {
    auto it = legacySetters_.find(toFullyQualifiedName(settingName));
    if (it != end(legacySetters_)) {
      if (it->second(newValue)) {
        return unit;
      }
      return makeUnexpected(SetErrorCode::Rejected);
    }
    return SettingsAccessorProxy::setFromString(settingName, newValue, reason);
  }

  std::unordered_map<std::string, std::function<bool(std::string_view)>>
      legacySetters_;
};

TEST(CommandLineParserTest, CustomSnapshot) {
  const char *int_val = "102", *double_val = "9.9990000999",
             *float_val = "10.99911", *string_val = "some_string_legacy";
  const char* program = "/bin/program";

  auto args = make_array<const char*>(
      program,
      "--help",
      "-b",
      "-i",
      int_val,
      "--unknown_opt",
      "test",
      "-s",
      string_val,
      "-f",
      float_val,
      "-d",
      double_val,
      "--legacy_double_setting",
      double_val,
      "--test_legacy_int_setting",
      int_val,
      "--legacy_string_setting",
      string_val);

  std::vector<const char*> expect = {
      program, "--help", "--unknown_opt", "test"};

  auto check_values = [&] {
    EXPECT_EQ(*FOLLY_SETTING(test, int_opt), folly::to<int>(int_val));
    EXPECT_TRUE(*FOLLY_SETTING(test, bool_opt));
    EXPECT_EQ(
        *FOLLY_SETTING(test, string_opt), folly::to<std::string>(string_val));
    EXPECT_EQ(*FOLLY_SETTING(test, float_opt), folly::to<float>(float_val));
    EXPECT_EQ(*FOLLY_SETTING(test, double_opt), folly::to<double>(double_val));

    EXPECT_EQ(gSettingsLegacy.lIntSetting, folly::to<int>(int_val));
    EXPECT_EQ(gSettingsLegacy.lDoubleSetting, folly::to<double>(double_val));
    EXPECT_EQ(
        gSettingsLegacy.lStringSetting, folly::to<std::string>(string_val));
  };

  std::unordered_map<std::string, std::string> aliases = {
      {"i", "test_int_opt"},
      {"b", "bool_opt"},
      {"f", "float_opt"},
      {"d", "test_double_opt"},
      {"s", "string_opt"}};

  XLOG(ERR) << "CHECKING " << join(" ", args);
  int argc = args.size();
  char** argv = const_cast<char**>(args.data());

  {
    Snapshot snap;
    CustomFlagsContained flags_info(snap, "test", aliases);
    EXPECT_EQ(
        CommandLineParser(argc, argv, flags_info).parse(),
        ArgParsingResult::HELP);
    snap.publish();
  }

  check_values();

  XLOG(ERR) << "AFTER " << join(" ", argv, argv + argc);
  check_args(argc, argv, expect);

  {
    Snapshot snap;
    snap.forEachSetting([&](const auto& setting) {
      snap.resetToDefault(setting.fullName());
    });
    snap.publish();
  }
}

TEST(CommandLineParserTest, HelpMessage) {
  std::stringstream oss;
  std::streambuf* old = std::cerr.rdbuf(oss.rdbuf());
  SCOPE_EXIT {
    std::cerr.rdbuf(old);
  };
  printHelpIfNeeded("test", false);
  const std::string msg = oss.str();

  const std::string prefix = "test:\n";
  const std::unordered_set<std::string> substrings{
      "\t--help Show this message\n\t  type: bool\n",
      "\t--test_string_opt String option\n\t  type: std::string default: \"blah\"\n",
      "\t--test_bool_opt Bool option\n\t  type: bool\n",
      "\t--test_float_opt Float option\n\t  type: float default: 1\n",
      "\t--test_double_opt Double option\n\t  type: double default: 1\n",
      "\t--test_int_opt Int option\n\t  type: int64_t default: 1\n",
      "\t--test_int_opt_reject_overrides Int option\n\t  type: int64_t default: 5\n"};

  EXPECT_THAT(msg, ::testing::StartsWith(prefix));
  auto expectedSize = prefix.size();
  for (const auto& substring : substrings) {
    EXPECT_THAT(msg, ::testing::HasSubstr(substring));
    expectedSize += substring.size();
  }
  EXPECT_EQ(expectedSize, msg.size());
}

} // namespace folly::settings::test
