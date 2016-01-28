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

#include <memory>
#include <unordered_map>

#include <folly/AtomicLinkedList.h>
#include <folly/ThreadLocal.h>

namespace folly { namespace fibers {

namespace {

class OnEventBaseDestructionCallback : public EventBase::LoopCallback {
 public:
  explicit OnEventBaseDestructionCallback(EventBase& evb) : evb_(evb) {}
  void runLoopCallback() noexcept override;

 private:
  EventBase& evb_;
};

class GlobalCache {
 public:
  static FiberManager& get(EventBase& evb, const FiberManager::Options& opts) {
    return instance().getImpl(evb, opts);
  }

  static std::unique_ptr<FiberManager> erase(EventBase& evb) {
    return instance().eraseImpl(evb);
  }

 private:
  GlobalCache() {}

  // Leak this intentionally. During shutdown, we may call getFiberManager,
  // and want access to the fiber managers during that time.
  static GlobalCache& instance() {
    static auto ret = new GlobalCache();
    return *ret;
  }

  FiberManager& getImpl(EventBase& evb, const FiberManager::Options& opts) {
    std::lock_guard<std::mutex> lg(mutex_);

    auto& fmPtrRef = map_[&evb];

    if (!fmPtrRef) {
      auto loopController = make_unique<EventBaseLoopController>();
      loopController->attachEventBase(evb);
      evb.runOnDestruction(new OnEventBaseDestructionCallback(evb));

      fmPtrRef = make_unique<FiberManager>(std::move(loopController), opts);
    }

    return *fmPtrRef;
  }

  std::unique_ptr<FiberManager> eraseImpl(EventBase& evb) {
    std::lock_guard<std::mutex> lg(mutex_);

    DCHECK(map_.find(&evb) != map_.end());

    auto ret = std::move(map_[&evb]);
    map_.erase(&evb);
    return ret;
  }

  std::mutex mutex_;
  std::unordered_map<EventBase*, std::unique_ptr<FiberManager>> map_;
};

class LocalCache {
 public:
  static FiberManager& get(EventBase& evb, const FiberManager::Options& opts) {
    return instance()->getImpl(evb, opts);
  }

  static void erase(EventBase& evb) {
    for (auto& localInstance : instance().accessAllThreads()) {
      localInstance.removedEvbs_.insertHead(&evb);
    }
  }

 private:
  LocalCache() {}

  struct LocalCacheTag {};
  using ThreadLocalCache = ThreadLocal<LocalCache, LocalCacheTag>;

  // Leak this intentionally. During shutdown, we may call getFiberManager,
  // and want access to the fiber managers during that time.
  static ThreadLocalCache& instance() {
    static auto ret = new ThreadLocalCache([]() { return new LocalCache(); });
    return *ret;
  }

  FiberManager& getImpl(EventBase& evb, const FiberManager::Options& opts) {
    eraseImpl();

    auto& fmPtrRef = map_[&evb];
    if (!fmPtrRef) {
      fmPtrRef = &GlobalCache::get(evb, opts);
    }

    DCHECK(fmPtrRef != nullptr);

    return *fmPtrRef;
  }

  void eraseImpl() {
    if (removedEvbs_.empty()) {
      return;
    }

    removedEvbs_.sweep([&](EventBase* evb) { map_.erase(evb); });
  }

  std::unordered_map<EventBase*, FiberManager*> map_;
  AtomicLinkedList<EventBase*> removedEvbs_;
};

void OnEventBaseDestructionCallback::runLoopCallback() noexcept {
  auto fm = GlobalCache::erase(evb_);
  DCHECK(fm.get() != nullptr);
  LocalCache::erase(evb_);

  fm->loopUntilNoReady();

  delete this;
}

} // namespace

FiberManager& getFiberManager(EventBase& evb,
                              const FiberManager::Options& opts) {
  return LocalCache::get(evb, opts);
}

}}
