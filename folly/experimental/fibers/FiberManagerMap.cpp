/*
 * Copyright 2015 Facebook, Inc.
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
#include <folly/experimental/fibers/FiberManagerMap.h>

#include <memory>
#include <unordered_map>

namespace folly { namespace fibers {

namespace detail {

thread_local std::unordered_map<folly::EventBase*, FiberManager*>
    localFiberManagerMap;
std::unordered_map<folly::EventBase*, std::unique_ptr<FiberManager>>
    fiberManagerMap;
std::mutex fiberManagerMapMutex;

FiberManager* getFiberManagerThreadSafe(folly::EventBase& evb,
                                        const FiberManager::Options& opts) {
  std::lock_guard<std::mutex> lg(fiberManagerMapMutex);

  auto it = fiberManagerMap.find(&evb);
  if (LIKELY(it != fiberManagerMap.end())) {
    return it->second.get();
  }

  auto loopController = folly::make_unique<EventBaseLoopController>();
  loopController->attachEventBase(evb);
  auto fiberManager =
      folly::make_unique<FiberManager>(std::move(loopController), opts);
  auto result = fiberManagerMap.emplace(&evb, std::move(fiberManager));
  return result.first->second.get();
}

} // detail namespace

FiberManager& getFiberManager(folly::EventBase& evb,
                              const FiberManager::Options& opts) {
  auto it = detail::localFiberManagerMap.find(&evb);
  if (LIKELY(it != detail::localFiberManagerMap.end())) {
    return *(it->second);
  }

  return *(detail::localFiberManagerMap[&evb] =
               detail::getFiberManagerThreadSafe(evb, opts));
}

}}
