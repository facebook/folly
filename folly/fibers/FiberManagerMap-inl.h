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

#include <memory>
#include <unordered_map>
#include <unordered_set>

#include <folly/Function.h>
#include <folly/ScopeGuard.h>
#include <folly/SingletonThreadLocal.h>
#include <folly/Synchronized.h>
#include <folly/container/F14Map.h>
#include <folly/synchronization/RelaxedAtomic.h>

namespace folly {
namespace fibers {

namespace detail {

// ssize_t is a hash of FiberManager::Options
template <typename EventBaseT>
using Key = std::tuple<EventBaseT*, ssize_t, const std::type_info*>;

template <typename EventBaseT>
Function<void()> makeOnEventBaseDestructionCallback(const Key<EventBaseT>& key);

template <typename EventBaseT>
class GlobalCache {
 public:
  using TypeMap = std::unordered_map< //
      std::type_index,
      std::unordered_set<const std::type_info*>>;
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

  static TypeMap getTypeMap() { return instance().getTypeMapImpl(); }

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

    auto mkey = MapKey(std::get<0>(key), std::get<1>(key), *std::get<2>(key));

    std::lock_guard<std::mutex> lg(mutex_);

    types_[std::get<2>(mkey)].insert(std::get<2>(key));

    auto& fmPtrRef = map_[mkey];

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
    auto mkey = MapKey(std::get<0>(key), std::get<1>(key), *std::get<2>(key));

    std::lock_guard<std::mutex> lg(mutex_);

    DCHECK_EQ(map_.count(mkey), 1u);

    auto ret = std::move(map_[mkey]);
    map_.erase(mkey);
    return ret;
  }

  TypeMap getTypeMapImpl() {
    std::lock_guard<std::mutex> lg(mutex_);

    return types_;
  }

  using MapKey = std::tuple<EventBaseT*, ssize_t, std::type_index>;

  std::mutex mutex_;
  std::unordered_map<MapKey, std::unique_ptr<FiberManager>> map_;
  TypeMap types_; // can have multiple type_info obj's for one type_index
};

constexpr size_t kEraseListMaxSize = 64;

template <typename EventBaseT>
class ThreadLocalCache {
 public:
  template <typename LocalT>
  static FiberManager& get(
      uint64_t token, EventBaseT& evb, const FiberManager::Options& opts) {
    return STL::get().template getImpl<LocalT>(token, evb, opts);
  }

  static void erase(const Key<EventBaseT>& key) {
    for (auto& localInstance : STL::accessAllThreads()) {
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
  struct TLTag {};
  template <typename Base>
  struct Derived : Base {
    using Base::Base;
  };
  using STL = SingletonThreadLocal<Derived<ThreadLocalCache>, TLTag>;

  ThreadLocalCache() = default;

  template <typename LocalT>
  FiberManager& getImpl(
      uint64_t token, EventBaseT& evb, const FiberManager::Options& opts) {
    if (eraseRequested_) {
      eraseImpl();
    }

    auto key = Key<EventBaseT>(&evb, token, &typeid(LocalT));
    auto it = map_.find(key);
    if (it != map_.end()) {
      DCHECK(it->second != nullptr);
      return *it->second;
    }

    return getSlowImpl<LocalT>(key, evb, opts);
  }

  template <typename LocalT>
  FOLLY_NOINLINE FiberManager& getSlowImpl(
      Key<EventBaseT> key, EventBaseT& evb, const FiberManager::Options& opts) {
    auto& ref = GlobalCache<EventBaseT>::template get<LocalT>(key, evb, opts);
    map_.emplace(key, &ref);
    return ref;
  }

  FOLLY_NOINLINE void eraseImpl() {
    auto types = GlobalCache<EventBaseT>::getTypeMap(); // big copy!!!
    eraseInfo_.withWLock([&](auto& info) {
      if (info.eraseAll) {
        map_.clear();
      } else {
        for (auto& key : info.eraseList) {
          for (auto type : types[*std::get<2>(key)]) {
            map_.erase( // can have multiple type_info obj's for one type_index
                std::make_tuple(std::get<0>(key), std::get<1>(key), type));
          }
        }
      }

      info.eraseList.clear();
      info.eraseAll = false;
      eraseRequested_ = false;
    });
  }

  folly::F14FastMap<Key<EventBaseT>, FiberManager*> map_;
  relaxed_atomic<bool> eraseRequested_{false};

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
