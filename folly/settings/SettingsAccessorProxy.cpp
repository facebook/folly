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

#include <folly/settings/SettingsAccessorProxy.h>

namespace folly::settings {

SettingsAccessorProxy::SettingsAccessorProxy(
    Snapshot& snapshot,
    std::string_view project,
    SettingsAccessorProxy::SettingAliases aliases)
    : project_(project), aliases_(std::move(aliases)), snapshot_(snapshot) {
  snapshot_.forEachSetting([&](const auto& setting) {
    settingsMeta_.emplace(setting.fullName(), setting.meta());
  });

  SettingMetadata help_meta{
      "",
      kHelpFlag,
      "bool",
      typeid(bool),
      "false",
      Mutability::Mutable,
      CommandLine::AcceptOverrides,
      "Show this message"};

  settingsMeta_.emplace(std::string(kHelpFlag), help_meta);
}

SettingsAccessorProxy::SettingMetaMap::const_iterator
SettingsAccessorProxy::findSettingMeta(std::string_view flag) const {
  if (!aliases_.empty()) {
    auto it = aliases_.find(std::string(flag));
    if (it != end(aliases_)) {
      flag = it->second;
    }
  }

  auto it = settingsMeta_.find(std::string(flag));
  if (it != end(settingsMeta_)) {
    return it;
  }

  if (!project_.empty()) {
    return settingsMeta_.find(fmt::format("{}_{}", project_, flag));
  }
  return end(settingsMeta_);
}

std::optional<std::reference_wrapper<const SettingMetadata>>
SettingsAccessorProxy::getSettingMetadata(std::string_view flag) const {
  auto it = findSettingMeta(flag);
  if (it != end(settingsMeta_)) {
    return std::cref(it->second);
  }
  return std::nullopt;
}

bool SettingsAccessorProxy::hasFlag(std::string_view flag) const {
  return findSettingMeta(flag) != end(settingsMeta_);
}

bool SettingsAccessorProxy::isBooleanFlag(std::string_view flag) const {
  auto it = findSettingMeta(flag);
  if (it != end(settingsMeta_)) {
    return it->second.typeId == typeid(bool);
  }
  return false;
}

std::string SettingsAccessorProxy::toFullyQualifiedName(
    std::string_view flag) const {
  auto it = findSettingMeta(flag);
  if (it != end(settingsMeta_)) {
    return it->first;
  }

  return std::string(flag);
}

SetResult SettingsAccessorProxy::resetToDefault(std::string_view settingName) {
  return snapshot_.resetToDefault(toFullyQualifiedName(settingName));
}

SetResult SettingsAccessorProxy::setFromString(
    std::string_view settingName,
    std::string_view newValue,
    std::string_view reason) {
  return snapshot_.setFromString(
      toFullyQualifiedName(settingName), newValue, reason);
}
} // namespace folly::settings
