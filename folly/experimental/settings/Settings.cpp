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
#include "Settings.h"

#include <map>

#include <folly/Synchronized.h>

namespace folly {
namespace settings {
namespace detail {
namespace {
class Signature {
 public:
  Signature(
      const std::type_info& type,
      std::string defaultString,
      std::string desc,
      bool unique)
      : type_(type),
        defaultString_(std::move(defaultString)),
        desc_(std::move(desc)),
        unique_(unique) {}

  bool operator==(const Signature& other) const {
    /* unique_ field is ignored on purpose */
    return type_ == other.type_ && defaultString_ == other.defaultString_ &&
        desc_ == other.desc_;
  }

  bool unique() const {
    return unique_;
  }

 private:
  std::type_index type_;
  std::string defaultString_;
  std::string desc_;
  bool unique_;
};
using SettingsMap = std::
    map<std::string, std::pair<Signature, std::shared_ptr<SettingCoreBase>>>;
Synchronized<SettingsMap>& settingsMap() {
  static Indestructible<Synchronized<SettingsMap>> map;
  return *map;
}
} // namespace

std::shared_ptr<SettingCoreBase> registerImpl(
    StringPiece project,
    StringPiece name,
    const std::type_info& type,
    StringPiece defaultString,
    StringPiece desc,
    bool unique,
    std::shared_ptr<SettingCoreBase> base) {
  if (project.empty() || project.find('_') != std::string::npos) {
    throw std::logic_error(
        "Setting project must be nonempty and cannot contain underscores: " +
        project.str());
  }

  auto fullname = project.str() + "_" + name.str();
  Signature sig(type, defaultString.str(), desc.str(), unique);

  auto mapPtr = settingsMap().wlock();
  auto it = mapPtr->find(fullname);
  if (it != mapPtr->end()) {
    if (it->second.first == sig) {
      if (unique || it->second.first.unique()) {
        throw std::logic_error("FOLLY_SETTING not unique: " + fullname);
      }
      /* Identical SHARED setting in a different translation unit,
         reuse it */
      return it->second.second;
    }
    throw std::logic_error("Setting collision detected: " + fullname);
  }
  mapPtr->emplace(std::move(fullname), std::make_pair(std::move(sig), base));
  return base;
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
  it->second.second->setFromString(newValue, reason);
  return true;
}

Optional<std::pair<std::string, std::string>> getAsString(
    StringPiece settingName) {
  auto mapPtr = detail::settingsMap().rlock();
  auto it = mapPtr->find(settingName.str());
  if (it == mapPtr->end()) {
    return folly::none;
  }
  return it->second.second->getAsString();
}

bool resetToDefault(StringPiece settingName) {
  auto mapPtr = detail::settingsMap().rlock();
  auto it = mapPtr->find(settingName.str());
  if (it == mapPtr->end()) {
    return false;
  }
  it->second.second->resetToDefault();
  return true;
}

void forEachSetting(
    const std::function<
        void(StringPiece, StringPiece, StringPiece, const std::type_info&)>&
        func) {
  detail::SettingsMap map;
  /* Note that this won't hold the lock over the callback, which is
     what we want since the user might call other settings:: APIs */
  map = *detail::settingsMap().rlock();
  for (const auto& kv : map) {
    auto value = kv.second.second->getAsString();
    func(kv.first, value.first, value.second, kv.second.second->typeId());
  }
}

} // namespace settings
} // namespace folly
