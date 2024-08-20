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

#include <folly/settings/Settings.h>

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

Optional<SettingMetadata> getSettingsMeta(StringPiece settingName) {
  auto mapPtr = detail::settingsMap().rlock();
  auto it = mapPtr->find(settingName.str());
  if (it == mapPtr->end()) {
    return none;
  }
  return it->second->meta();
}

std::vector<SettingMetadata> getAllSettingsMeta() {
  std::vector<SettingMetadata> rv;
  detail::settingsMap().withRLock([&rv](const auto& settingsMap) {
    rv.reserve(settingsMap.size());
    for (const auto& [_, corePtr] : settingsMap) {
      rv.push_back(corePtr->meta());
    }
  });
  return rv;
}

SetResult Snapshot::setFromString(
    StringPiece settingName, StringPiece newValue, StringPiece reason) {
  auto mapPtr = detail::settingsMap().rlock();
  auto it = mapPtr->find(settingName.str());
  if (it == mapPtr->end()) {
    return makeUnexpected(SetErrorCode::NotFound);
  }
  return it->second->setFromString(newValue, reason, this);
}

SetResult Snapshot::forceSetFromString(
    StringPiece settingName, StringPiece newValue, StringPiece reason) {
  auto mapPtr = detail::settingsMap().rlock();
  auto it = mapPtr->find(settingName.str());
  if (it == mapPtr->end()) {
    return makeUnexpected(SetErrorCode::NotFound);
  }
  it->second->forceSetFromString(newValue, reason, this);
  return folly::unit;
}

Optional<Snapshot::SettingsInfo> Snapshot::getAsString(
    StringPiece settingName) const {
  auto mapPtr = detail::settingsMap().rlock();
  auto it = mapPtr->find(settingName.str());
  if (it == mapPtr->end()) {
    return none;
  }
  return it->second->getAsString(this);
}

SetResult Snapshot::resetToDefault(StringPiece settingName) {
  auto mapPtr = detail::settingsMap().rlock();
  auto it = mapPtr->find(settingName.str());
  if (it == mapPtr->end()) {
    return makeUnexpected(SetErrorCode::NotFound);
  }
  return it->second->resetToDefault(this);
}

SetResult Snapshot::forceResetToDefault(StringPiece settingName) {
  auto mapPtr = detail::settingsMap().rlock();
  auto it = mapPtr->find(settingName.str());
  if (it == mapPtr->end()) {
    return makeUnexpected(SetErrorCode::NotFound);
  }
  it->second->forceResetToDefault(this);
  return folly::unit;
}

void Snapshot::forEachSetting(
    const std::function<void(const SettingMetadata&, StringPiece, StringPiece)>&
        func) const {
  detail::SettingsMap map;
  /* Note that this won't hold the lock over the callback, which is
     what we want since the user might call other settings:: APIs */
  map = *detail::settingsMap().rlock();
  for (const auto& kv : map) {
    auto value = kv.second->getAsString(this);
    func(kv.second->meta(), value.first, value.second);
  }
}

namespace detail {
std::atomic<SettingCoreBase::Version> gGlobalVersion_;

auto& getSavedValuesMutex() {
  static Indestructible<SharedMutex> gSavedValuesMutex;
  return *gSavedValuesMutex;
}

/* Version -> (count of outstanding snapshots, saved setting values) */
auto& getSavedValues() {
  static Indestructible<std::unordered_map<
      SettingCoreBase::Version,
      std::pair<size_t, std::unordered_map<SettingCoreBase::Key, BoxedValue>>>>
      gSavedValues;
  return *gSavedValues;
}

SettingCoreBase::Version nextGlobalVersion() {
  return gGlobalVersion_.fetch_add(1) + 1;
}

void saveValueForOutstandingSnapshots(
    SettingCoreBase::Key settingKey,
    SettingCoreBase::Version version,
    const BoxedValue& value) {
  std::unique_lock lg(getSavedValuesMutex());
  for (auto& it : getSavedValues()) {
    if (version <= it.first) {
      it.second.second[settingKey] = value;
    }
  }
}

const BoxedValue* FOLLY_NULLABLE
getSavedValue(SettingCoreBase::Key settingKey, SettingCoreBase::Version at) {
  std::shared_lock lg(getSavedValuesMutex());
  auto it = getSavedValues().find(at);
  if (it != getSavedValues().end()) {
    auto jt = it->second.second.find(settingKey);
    if (jt != it->second.second.end()) {
      return &jt->second;
    }
  }
  return nullptr;
}

SnapshotBase::SnapshotBase() {
  std::unique_lock lg(detail::getSavedValuesMutex());
  at_ = detail::gGlobalVersion_.load();
  auto it = detail::getSavedValues().emplace(
      std::piecewise_construct,
      std::forward_as_tuple(at_),
      std::forward_as_tuple());
  ++it.first->second.first;
}

SnapshotBase::~SnapshotBase() {
  std::unique_lock lg(detail::getSavedValuesMutex());
  auto& savedValues = detail::getSavedValues();
  auto it = savedValues.find(at_);
  assert(it != savedValues.end());
  if (it != savedValues.end()) {
    --it->second.first;
    if (!it->second.first) {
      savedValues.erase(at_);
    }
  }
}

} // namespace detail

void Snapshot::publish() {
  // Double check frozen immutables since they could have been frozen after the
  // values were set.
  auto frozenProjects = frozenSettingProjects();
  for (auto& it : snapshotValues_) {
    it.second.publish(frozenProjects);
  }
}

} // namespace settings
} // namespace folly
