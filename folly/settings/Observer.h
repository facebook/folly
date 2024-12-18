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

#include <atomic>
#include <memory>

#include <folly/observer/Observer.h>
#include <folly/observer/SimpleObservable.h>
#include <folly/settings/Settings.h>

namespace folly::settings {
/*
 * Get a folly::observer::Observer<T> for a given setting. For example:
 *   folly::settings::getObserver(FOLLY_SETTING(project, retention))
 *
 * Useful for cases which may want to trigger callbacks when settings are
 * updated, or for cases that are already built on top of Observers.
 *
 * The returned Observer is updated whenever the setting is updated.
 */
template <typename T, std::atomic<uint64_t>* Ptr, typename Tag>
observer::Observer<T> getObserver(
    settings::detail::SettingWrapper<T, Ptr, Tag> setting) {
  // Make observable a unique_ptr so it can be moved and captured in the setting
  // update callback
  auto observable = std::make_unique<observer::SimpleObservable<T>>(*setting);
  auto observer = observable->getObserver();

  auto callbackHandle = setting.addCallback(
      [observable = std::move(observable)](const auto& newContents) {
        observable->setValue(newContents.value);
      });
  // Create a wrapped observer to capture the callback handle and keep it alive
  // as long as the observer is alive
  return observer::makeObserver(
      [callbackHandle = std::move(callbackHandle),
       observer = std::move(observer)]() { return **observer; });
}
} // namespace folly::settings
