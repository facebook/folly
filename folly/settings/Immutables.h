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

#include <string>
#include <string_view>

#include <folly/container/F14Set.h>

namespace folly {
namespace settings {

/**
 * Freezes immutable settings - preventing subsequent mutation attempts. It's up
 * to the caller to call this function after settings have been initialized.
 */
void freezeImmutables(F14FastSet<std::string> projects);

/**
 * @returns true if immutable settings can not be modified, false otherwise.
 */
bool immutablesFrozen(std::string_view project);

struct FrozenSettingProjects {
  explicit FrozenSettingProjects(F14FastSet<std::string> projects);

  bool contains(std::string_view project) const;

 private:
  F14FastSet<std::string> projects_;
};

/**
 * @return a snapshot of the current set of frozen setting projects.
 */
FrozenSettingProjects frozenSettingProjects();

} // namespace settings
} // namespace folly
