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

#include <memory>
#include <unordered_map>

#include <folly/Function.h>
#include <folly/ScopeGuard.h>
#include <folly/Synchronized.h>
#include <folly/ThreadLocal.h>

namespace folly {
namespace fibers {

namespace detail {

// ssize_t is a hash of FiberManager::Options
template <typename EventBaseT>
using Key = std::tuple<EventBaseT*, ssize_t, std::type_index>;

template <typename EventBaseT>
Function<void()> makeOnEventBaseDestructionCallback(const Key<EventBaseT>& key);

template <typename EventBaseT>
class GlobalCache {
 public:
  template <typename LocalT>
  static FiberManager& get(
      const Key<EventBaseT>& key,
      EventBaseT& evb,
      const FiberManager::Options& opts) {
    return instance().template getImpl<LocalT>(key, evb, opts);
  }

  static std::unique_ptr<FiberManager> erase(const Key<EventBaseT>& key) {
    return instance().eraseImpl(key);
  }

 private:
  GlobalCache() = default;

  // Leak this intentionally. During shutdown, we may call getFiberManager,
  // and want access to the fiber managers during that time.
  static GlobalCache& instance() {
    static auto ret = new GlobalCache();
    return *ret;
  }

  template <typename LocalT>
  FiberManager& getImpl(
      const Key<EventBaseT>& key,
      EventBaseT& evb,
      const FiberManager::Options& opts) {
    bool constructed = false;
    SCOPE_EXIT {
      if (constructed) {
        evb.runOnDestruction(makeOnEventBaseDestructionCallback(key));
      }
    };

    std::lock_guard<std::mutex> lg(mutex_);

    auto& fmPtrRef = map_[key];

    if (!fmPtrRef) {
      constructed = true;
      auto loopController = std::make_unique<EventBaseLoopController>();
      loopController->attachEventBase(evb);
      fmPtrRef = std::make_unique<FiberManager>(
          LocalType<LocalT>(), std::move(loopController), opts);
    }

    return *fmPtrRef;
  }

  std::unique_ptr<FiberManager> eraseImpl(const Key<EventBaseT>& key) {
    std::lock_guard<std::mutex> lg(mutex_);

    DCHECK_EQ(map_.count(key), 1u);

    auto ret = std::move(map_[key]);
    map_.erase(key);
    return ret;
  }

  std::mutex mutex_;
  std::unordered_map<Key<EventBaseT>, std::unique_ptr<FiberManager>> map_;
};

constexpr size_t kEraseListMaxSize = 64;

template <typename EventBaseT>
class ThreadLocalCache {
 public:
  template <typename LocalT>
  static FiberManager& get(
      uint64_t token, EventBaseT& evb, const FiberManager::Options& opts) {
    return instance()->template getImpl<LocalT>(token, evb, opts);
  }

  static void erase(const Key<EventBaseT>& key) {
    for (auto& localInstance : instance().accessAllThreads()) {
      localInstance.eraseInfo_.withWLock([&](auto& info) {
        if (info.eraseList.size() >= kEraseListMaxSize) {
          info.eraseAll = true;
        } else {
          info.eraseList.push_back(key);
        }
        localInstance.eraseRequested_ = true;
      });
    }
  }

 private:
  ThreadLocalCache() = default;

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

  template <typename LocalT>
  FiberManager& getImpl(
      uint64_t token, EventBaseT& evb, const FiberManager::Options& opts) {
    eraseImpl();

    auto key = make_tuple(&evb, token, std::type_index(typeid(LocalT)));
    auto& fmPtrRef = map_[key];
    if (!fmPtrRef) {
      fmPtrRef = &GlobalCache<EventBaseT>::template get<LocalT>(key, evb, opts);
    }

    DCHECK(fmPtrRef != nullptr);

    return *fmPtrRef;
  }

  void eraseImpl() {
    if (!eraseRequested_.load()) {
      return;
    }

    eraseInfo_.withWLock([&](auto& info) {
      if (info.eraseAll) {
        map_.clear();
      } else {
        for (auto& key : info.eraseList) {
          map_.erase(key);
        }
      }

      info.eraseList.clear();
      info.eraseAll = false;
      eraseRequested_ = false;
    });
  }

  std::unordered_map<Key<EventBaseT>, FiberManager*> map_;
  std::atomic<bool> eraseRequested_{false};

  struct EraseInfo {
    bool eraseAll{false};
    std::vector<Key<EventBaseT>> eraseList;
  };

  folly::Synchronized<EraseInfo> eraseInfo_;
};

template <typename EventBaseT>
Function<void()> makeOnEventBaseDestructionCallback(
    const Key<EventBaseT>& key) {
  return [key] {
    auto fm = GlobalCache<EventBaseT>::erase(key);
    DCHECK(fm.get() != nullptr);
    ThreadLocalCache<EventBaseT>::erase(key);
  };
}

} // namespace detail

template <typename LocalT>
FiberManager& getFiberManagerT(
    EventBase& evb, const FiberManager::Options& opts) {
  return detail::ThreadLocalCache<EventBase>::get<LocalT>(0, evb, opts);
}

FiberManager& getFiberManager(
    folly::EventBase& evb, const FiberManager::Options& opts) {
  return detail::ThreadLocalCache<EventBase>::get<void>(0, evb, opts);
}

FiberManager& getFiberManager(
    VirtualEventBase& evb, const FiberManager::Options& opts) {
  return detail::ThreadLocalCache<VirtualEventBase>::get<void>(0, evb, opts);
}

FiberManager& getFiberManager(
    folly::EventBase& evb, const FiberManager::FrozenOptions& opts) {
  return detail::ThreadLocalCache<EventBase>::get<void>(
      opts.token, evb, opts.options);
}

FiberManager& getFiberManager(
    VirtualEventBase& evb, const FiberManager::FrozenOptions& opts) {
  return detail::ThreadLocalCache<VirtualEventBase>::get<void>(
      opts.token, evb, opts.options);
}

} // namespace fibers
} // namespace folly
