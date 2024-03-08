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

#include <folly/experimental/settings/Immutables.h>

#include <folly/Indestructible.h>
#include <folly/Synchronized.h>

namespace folly {
namespace settings {
namespace {
Synchronized<F14FastSet<std::string>>& globalFrozenSettingProjects() {
  static Indestructible<Synchronized<F14FastSet<std::string>>> projects;
  return *projects;
}
} // namespace

void freezeImmutables(F14FastSet<std::string> projects) {
  globalFrozenSettingProjects().withWLock([&](auto& frozenProjects) {
    projects.eraseInto(projects.begin(), projects.end(), [&](auto&& project) {
      frozenProjects.insert(std::move(project));
    });
  });
}

bool immutablesFrozen(std::string_view project) {
  return globalFrozenSettingProjects().rlock()->contains(project);
}

FrozenSettingProjects::FrozenSettingProjects(F14FastSet<std::string> projects)
    : projects_(std::move(projects)) {}

bool FrozenSettingProjects::contains(std::string_view project) const {
  return projects_.contains(project);
}

FrozenSettingProjects frozenSettingProjects() {
  auto projects = globalFrozenSettingProjects().copy();
  return FrozenSettingProjects(std::move(projects));
}

} // namespace settings
} // namespace folly
