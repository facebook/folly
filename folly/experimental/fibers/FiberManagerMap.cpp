/*
 * Copyright 2016 Facebook, Inc.
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

#include <folly/Synchronized.h>
#include <folly/ThreadLocal.h>

namespace folly {
namespace fibers {

namespace {

class EventBaseOnDestructionCallback : public EventBase::LoopCallback {
 public:
  explicit EventBaseOnDestructionCallback(EventBase& evb) : evb_(evb) {}
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
      evb.runOnDestruction(new EventBaseOnDestructionCallback(evb));

      fmPtrRef = make_unique<FiberManager>(std::move(loopController), opts);
    }

    return *fmPtrRef;
  }

  std::unique_ptr<FiberManager> eraseImpl(EventBase& evb) {
    std::lock_guard<std::mutex> lg(mutex_);

    DCHECK_EQ(1, map_.count(&evb));

    auto ret = std::move(map_[&evb]);
    map_.erase(&evb);
    return ret;
  }

  std::mutex mutex_;
  std::unordered_map<EventBase*, std::unique_ptr<FiberManager>> map_;
};

constexpr size_t kEraseListMaxSize = 64;

class ThreadLocalCache {
 public:
  static FiberManager& get(EventBase& evb, const FiberManager::Options& opts) {
    return instance()->getImpl(evb, opts);
  }

  static void erase(EventBase& evb) {
    for (auto& localInstance : instance().accessAllThreads()) {
      SYNCHRONIZED(info, localInstance.eraseInfo_) {
        if (info.eraseList.size() >= kEraseListMaxSize) {
          info.eraseAll = true;
        } else {
          info.eraseList.push_back(&evb);
        }
        localInstance.eraseRequested_ = true;
      }
    }
  }

 private:
  ThreadLocalCache() {}

  struct ThreadLocalCacheTag {};
  using ThreadThreadLocalCache =
      ThreadLocal<ThreadLocalCache, ThreadLocalCacheTag>;

  // Leak this intentionally. During shutdown, we may call getFiberManager,
  // and want access to the fiber managers during that time.
  static ThreadThreadLocalCache& instance() {
    static auto ret =
        new ThreadThreadLocalCache([]() { return new ThreadLocalCache(); });
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
    if (!eraseRequested_.load()) {
      return;
    }

    SYNCHRONIZED(info, eraseInfo_) {
      if (info.eraseAll) {
        map_.clear();
      } else {
        for (auto evbPtr : info.eraseList) {
          map_.erase(evbPtr);
        }
      }

      info.eraseList.clear();
      info.eraseAll = false;
      eraseRequested_ = false;
    }
  }

  std::unordered_map<EventBase*, FiberManager*> map_;
  std::atomic<bool> eraseRequested_{false};

  struct EraseInfo {
    bool eraseAll{false};
    std::vector<EventBase*> eraseList;
  };

  folly::Synchronized<EraseInfo> eraseInfo_;
};

void EventBaseOnDestructionCallback::runLoopCallback() noexcept {
  auto fm = GlobalCache::erase(evb_);
  DCHECK(fm.get() != nullptr);
  ThreadLocalCache::erase(evb_);

  while (fm->hasTasks()) {
    fm->loopUntilNoReady();
    evb_.loopOnce();
  }

  delete this;
}

} // namespace

FiberManager& getFiberManager(
    EventBase& evb,
    const FiberManager::Options& opts) {
  return ThreadLocalCache::get(evb, opts);
}
}
}
