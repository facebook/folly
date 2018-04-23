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

#include <map>

#include <folly/Synchronized.h>

namespace folly {
namespace settings {
namespace detail {
namespace {
using SettingsMap = std::map<std::string, SettingCoreBase*>;
Synchronized<SettingsMap>& settingsMap() {
  static Indestructible<Synchronized<SettingsMap>> map;
  return *map;
}
} // namespace

void registerSetting(SettingCoreBase& core) {
  if (core.meta().project.empty() ||
      core.meta().project.find('_') != std::string::npos) {
    throw std::logic_error(
        "Setting project must be nonempty and cannot contain underscores: " +
        core.meta().project.str());
  }

  auto fullname = core.meta().project.str() + "_" + core.meta().name.str();

  auto mapPtr = settingsMap().wlock();
  auto it = mapPtr->find(fullname);
  if (it != mapPtr->end()) {
    throw std::logic_error("FOLLY_SETTING already exists: " + fullname);
  }
  mapPtr->emplace(std::move(fullname), &core);
}

} // namespace detail

bool setFromString(
    StringPiece settingName,
    StringPiece newValue,
    StringPiece reason) {
  auto mapPtr = detail::settingsMap().rlock();
  auto it = mapPtr->find(settingName.str());
  if (it == mapPtr->end()) {
    return false;
  }
  it->second->setFromString(newValue, reason);
  return true;
}

Optional<SettingsInfo> getAsString(StringPiece settingName) {
  auto mapPtr = detail::settingsMap().rlock();
  auto it = mapPtr->find(settingName.str());
  if (it == mapPtr->end()) {
    return folly::none;
  }
  return it->second->getAsString();
}

Optional<SettingMetadata> getSettingsMeta(StringPiece settingName) {
  auto mapPtr = detail::settingsMap().rlock();
  auto it = mapPtr->find(settingName.str());
  if (it == mapPtr->end()) {
    return folly::none;
  }
  return it->second->meta();
}

bool resetToDefault(StringPiece settingName) {
  auto mapPtr = detail::settingsMap().rlock();
  auto it = mapPtr->find(settingName.str());
  if (it == mapPtr->end()) {
    return false;
  }
  it->second->resetToDefault();
  return true;
}

void forEachSetting(
    const std::function<void(const SettingMetadata&, StringPiece, StringPiece)>&
        func) {
  detail::SettingsMap map;
  /* Note that this won't hold the lock over the callback, which is
     what we want since the user might call other settings:: APIs */
  map = *detail::settingsMap().rlock();
  for (const auto& kv : map) {
    auto value = kv.second->getAsString();
    func(kv.second->meta(), value.first, value.second);
  }
}

} // namespace settings
} // namespace folly
