/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <folly/fibers/Baton.h>
#include <folly/fibers/async/Async.h>
#include <glog/logging.h>
#include <utility>

namespace folly {
namespace fibers {
namespace async {

template <typename... Args>
Async<void> baton_wait(Baton& baton, Args&&... args) {
  // Call into blocking API
  baton.wait(std::forward<Args>(args)...);
  return {};
}

template <typename... Args>
Async<bool> baton_try_wait_for(Baton& baton, Args&&... args) {
  // Call into blocking API
  return baton.try_wait_for(std::forward<Args>(args)...);
}

template <typename... Args>
Async<bool> baton_try_wait_until(Baton& baton, Args&&... args) {
  // Call into blocking API
  return baton.try_wait_until(std::forward<Args>(args)...);
}

} // namespace async
} // namespace fibers
} // namespace folly
