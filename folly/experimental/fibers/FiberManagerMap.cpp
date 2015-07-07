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
#include "FiberManagerMap.h"

#include <cassert>
#include <memory>
#include <unordered_map>

#include <folly/ThreadLocal.h>

namespace folly { namespace fibers {

namespace {

// Leak these intentionally.  During shutdown, we may call getFiberManager, and
// want access to the fiber managers during that time.
class LocalFiberManagerMapTag;
typedef folly::ThreadLocal<
    std::unordered_map<folly::EventBase*, FiberManager*>,
    LocalFiberManagerMapTag>
  LocalMapType;
LocalMapType* localFiberManagerMap() {
  static auto ret = new LocalMapType();
  return ret;
}

typedef
  std::unordered_map<folly::EventBase*, std::unique_ptr<FiberManager>>
  MapType;
MapType* fiberManagerMap() {
  static auto ret = new MapType();
  return ret;
}

std::mutex* fiberManagerMapMutex() {
  static auto ret = new std::mutex();
  return ret;
}


class OnEventBaseDestructionCallback : public folly::EventBase::LoopCallback {
 public:
  explicit OnEventBaseDestructionCallback(folly::EventBase& evb)
           : evb_(&evb) {}
  void runLoopCallback() noexcept override {
    for (auto& localMap : localFiberManagerMap()->accessAllThreads()) {
      localMap.erase(evb_);
    }
    std::unique_ptr<FiberManager> fm;
    {
      std::lock_guard<std::mutex> lg(*fiberManagerMapMutex());
      auto it = fiberManagerMap()->find(evb_);
      assert(it != fiberManagerMap()->end());
      fm = std::move(it->second);
      fiberManagerMap()->erase(it);
    }
    assert(fm.get() != nullptr);
    fm->loopUntilNoReady();
    delete this;
  }
 private:
  folly::EventBase* evb_;
};

FiberManager* getFiberManagerThreadSafe(folly::EventBase& evb,
                                        const FiberManager::Options& opts) {
  std::lock_guard<std::mutex> lg(*fiberManagerMapMutex());

  auto it = fiberManagerMap()->find(&evb);
  if (LIKELY(it != fiberManagerMap()->end())) {
    return it->second.get();
  }

  auto loopController = folly::make_unique<EventBaseLoopController>();
  loopController->attachEventBase(evb);
  auto fiberManager =
      folly::make_unique<FiberManager>(std::move(loopController), opts);
  auto result = fiberManagerMap()->emplace(&evb, std::move(fiberManager));
  evb.runOnDestruction(new OnEventBaseDestructionCallback(evb));
  return result.first->second.get();
}

} // namespace

FiberManager& getFiberManager(folly::EventBase& evb,
                              const FiberManager::Options& opts) {
  auto it = (*localFiberManagerMap())->find(&evb);
  if (LIKELY(it != (*localFiberManagerMap())->end())) {
    return *(it->second);
  }

  auto fm = getFiberManagerThreadSafe(evb, opts);
  (*localFiberManagerMap())->emplace(&evb, fm);
  return *fm;
}

}}
