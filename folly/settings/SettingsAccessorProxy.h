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

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <folly/settings/Settings.h>

namespace folly::settings {

constexpr const std::string_view kHelpFlag = "help";

class ISettingsAccessorProxy {
 public:
  virtual ~ISettingsAccessorProxy() = default;

  virtual SetResult setFromString(
      std::string_view settingName,
      std::string_view newValue,
      std::string_view reason) = 0;

  virtual SetResult resetToDefault(std::string_view flag) = 0;
};

/**
 * This class is a proxy for working with folly::settings represented as
 * strings. It can apply changes to these folly::settings via snapshot provided
 * by user. Changes are not going to be published by this class, it is callers
 * resposibility to commit/publish cahnges from snapshot into global state.
 *
 * This class is mainly useful when one needs to parse folly::settings from
 * command line or a config file.
 *
 * If default project and/or aliases map are provided SettingsAccessorProxy will
 * try to expand flag name into fully qualified folly::setting name.
 */
class SettingsAccessorProxy : public ISettingsAccessorProxy {
 public:
  using SettingAliases = std::unordered_map<std::string, std::string>;
  using SettingMetaMap = std::unordered_map<std::string, SettingMetadata>;

  /**
   * @param snapshot on which settings modifications will be applied
   * @param project name for not fully qualified setting name expantion
   * @param aliases reverse map of alternative names for folly::settings
   */
  explicit SettingsAccessorProxy(
      Snapshot& snapshot,
      std::string_view project = "",
      SettingAliases aliases = {});

  virtual ~SettingsAccessorProxy() override = default;

  /**
   * Return full name of folly::setting if flag is registered as folly::setting.
   * This method will resolve flag aliases and prepend flag with default
   * project, if any of this applies.
   *
   * @param flag command line flag name
   * @return fully qualified folly::setting name
   */
  std::string toFullyQualifiedName(std::string_view flag) const;

  /**
   * Returns true if flag is registered folly::setting. Flag alias and
   * name expantion works the same as toFullyQualifiedName().
   *
   * @param flag command line flag name
   * @return bool
   */
  bool hasFlag(std::string_view flag) const;

  /**
   * Returns true if flag is registered as bool folly::setting. Flag alias and
   * name expantion works the same as toFullyQualifiedName(). If flag is not
   * folly::settings, returns false.
   *
   * @param flag command line flag name
   * @return bool
   */
  bool isBooleanFlag(std::string_view flag) const;

  /**
   * Sets value of settingName into newValue with reason set to `reason`. The
   * change will be applied to the folly::settings::Snapshot associated with
   * SettingsAccessorProxy. This function will do setting name expantion via
   * toFullyQualifiedName() if needed.
   *
   * @param settingName
   * @param newValue
   * @param reason for update
   */
  virtual SetResult setFromString(
      std::string_view settingName,
      std::string_view newValue,
      std::string_view reason) override;

  /**
   * Return const ref to map setting_name => setting metadata. This is useful
   * for generating help messages.
   */

  const SettingMetaMap& getSettingsMetadata() const& noexcept {
    return settingsMeta_;
  }

  SettingMetaMap getSettingsMetadata() && { return std::move(settingsMeta_); }

  std::optional<std::reference_wrapper<const SettingMetadata>>
  getSettingMetadata(std::string_view flag) const;

  /**
   * Resets flag to default value.
   *
   * @param flag name of folly::settings.
   */
  virtual SetResult resetToDefault(std::string_view flag) override;

  Snapshot& snapshot() noexcept { return snapshot_; }

 protected:
  virtual SettingMetaMap::const_iterator findSettingMeta(
      std::string_view) const;

  std::string project_;
  SettingAliases aliases_;
  SettingMetaMap settingsMeta_;

 private:
  Snapshot& snapshot_;
};

} // namespace folly::settings
