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

#include <glog/logging.h>

#include <mutex>

#include <folly/CancellationToken.h>
#include <folly/Portability.h>
#include <folly/fibers/FiberManager.h>
#include <folly/functional/Invoke.h>
#include <folly/futures/Future.h>
#include <folly/observer/detail/Core.h>
#include <folly/observer/detail/GraphCycleDetector.h>
#include <folly/synchronization/SanitizeThread.h>

namespace folly {
namespace observer_detail {

/**
 * ObserverManager is a singleton which controls the re-computation of all
 * Observers. Such re-computation always happens on the thread pool owned by
 * ObserverManager.
 *
 * ObserverManager has global current version. All existing Observers
 * may have their version be less (yet to be updated) or equal (up to date)
 * to the global current version.
 *
 * ObserverManager::CurrentQueue contains all of the Observers which need to be
 * updated to the global current version. Those updates are peformed on the
 * ObserverManager's thread pool, until the queue is empty. If some Observer is
 * updated, all of its dependents are added to ObserverManager::CurrentQueue
 * to be updated.
 *
 * If some leaf Observer (i.e. created from Observable) is updated, then current
 * version of the ObserverManager should be bumped. All such updated leaf
 * Observers are added to the ObserverManager::NextQueue.
 *
 * *Only* when ObserverManager::CurrentQueue is empty, the global current
 * version is bumped and all updates from the ObserverManager::NextQueue are
 * performed. If leaf Observer gets updated more then once before being picked
 * from the ObserverManager::NextQueue, then only the last update is processed.
 */
class ObserverManager {
 public:
  static size_t getVersion() { return getInstance().version_; }

  static bool inManagerThread() { return inManagerThread_; }

  static void vivify() { getUpdatesManager(); }

  static void scheduleRefresh(Core::Ptr core, size_t minVersion) {
    if (core->getVersion() >= minVersion) {
      return;
    }

    auto& instance = getInstance();

    std::shared_lock rh(instance.versionMutex_);

    instance.scheduleCurrent(
        [coreWeak = folly::to_weak_ptr(std::move(core)),
         &instance,
         rh_2 = std::move(rh)]() {
          if (auto coreShared = coreWeak.lock()) {
            coreShared->refresh(instance.version_);
          }
        });
  }

  static void scheduleRefreshNewVersion(Function<Core::Ptr()> coreFunc) {
    getInstance().scheduleNext(std::move(coreFunc));
  }

  static void initCore(Core::Ptr core) {
    DCHECK(core->getVersion() == 0);

    auto& instance = getInstance();

    folly::fibers::runInMainContext([&] {
      auto inManagerThread = std::exchange(inManagerThread_, true);
      SCOPE_EXIT {
        inManagerThread_ = inManagerThread;
      };

      std::shared_lock rh(instance.versionMutex_);

      core->refresh(instance.version_);
    });
  }

  static void waitForAllUpdates() {
    tryWaitForAllUpdatesImpl([=](auto& m) { return std::unique_lock(m); });
  }
  static bool tryWaitForAllUpdates() {
    return tryWaitForAllUpdatesImpl([=](auto& m) {
      return std::unique_lock(m, std::try_to_lock);
    });
  }
  template <typename Rep, typename Period>
  static bool tryWaitForAllUpdatesFor(
      std::chrono::duration<Rep, Period> timeout) {
    return tryWaitForAllUpdatesImpl([=](auto& m) {
      return std::unique_lock(m, timeout);
    });
  }
  template <typename Clock, typename Duration>
  static bool tryWaitForAllUpdatesUntil(
      std::chrono::time_point<Clock, Duration> deadline) {
    return tryWaitForAllUpdatesImpl([=](auto& m) {
      return std::unique_lock(m, deadline);
    });
  }

  class DependencyRecorder {
   public:
    using DependencySet = std::unordered_set<Core::Ptr>;
    struct Dependencies {
      explicit Dependencies(const Core& core_) : core(core_) {}

      DependencySet dependencies;
      const Core& core;
    };

    explicit DependencyRecorder(const Core& core) : dependencies_(core) {
      DCHECK(inManagerThread());

      previousDepedencies_ = currentDependencies_;
      currentDependencies_ = &dependencies_;
    }

    static bool isActive() { return currentDependencies_; }

    template <typename F>
    static invoke_result_t<F> withDependencyRecordingDisabled(F f) {
      auto* const dependencies = std::exchange(currentDependencies_, nullptr);
      SCOPE_EXIT {
        currentDependencies_ = dependencies;
      };

      return f();
    }

    static void markDependency(Core::Ptr dependency) {
      DCHECK(inManagerThread());
      DCHECK(currentDependencies_);

      currentDependencies_->dependencies.insert(std::move(dependency));
    }

    static void markRefreshDependency(const Core& core) {
      if (!kIsDebug) {
        return;
      }
      if (!currentDependencies_) {
        return;
      }

      getInstance().cycleDetector_.withLock([&](CycleDetector& cycleDetector) {
        bool hasCycle =
            !cycleDetector.addEdge(&currentDependencies_->core, &core);
        if (hasCycle) {
          LOG(FATAL) << "Observer cycle detected.";
        }
      });
    }

    static void unmarkRefreshDependency(const Core& core) {
      if (!kIsDebug) {
        return;
      }
      if (!currentDependencies_) {
        return;
      }

      getInstance().cycleDetector_.withLock([&](CycleDetector& cycleDetector) {
        cycleDetector.removeEdge(&currentDependencies_->core, &core);
      });
    }

    DependencySet release() {
      DCHECK(currentDependencies_ == &dependencies_);
      std::swap(currentDependencies_, previousDepedencies_);
      previousDepedencies_ = nullptr;

      return std::move(dependencies_.dependencies);
    }

    ~DependencyRecorder() {
      if (currentDependencies_ == &dependencies_) {
        release();
      }
    }

   private:
    Dependencies dependencies_;
    Dependencies* previousDepedencies_;

    static thread_local Dependencies* currentDependencies_;
  };

 private:
  using TryWaitForAllUpdatesImplOp = FunctionRef<
      std::unique_lock<SharedMutexReadPriority>(SharedMutexReadPriority&)>;
  ObserverManager() {}

  static bool tryWaitForAllUpdatesImpl(TryWaitForAllUpdatesImplOp op);

  void scheduleCurrent(Function<void()>);
  void scheduleNext(Function<Core::Ptr()>);

  class UpdatesManager {
   public:
    UpdatesManager();
    bool tryWaitForAllUpdatesImpl(TryWaitForAllUpdatesImplOp op);

   private:
    class CurrentQueueProcessor;
    class NextQueueProcessor;

    // Stops both processor threads early, called from shutdownCallback_ (see
    // below). Declared separately from the destructor so it can run *before*
    // SingletonVault destroys any other singleton, not just at this object's
    // own (LIFO-ordered) destruction turn -- see ObserverManager.cpp for why.
    void stopProcessorsForShutdown();

    // Guards currentQueueProcessor_/nextQueueProcessor_ against a real race:
    // getUpdatesManager() hands out a shared_ptr<UpdatesManager>, so a caller
    // already inside tryWaitForAllUpdatesImpl() can be dereferencing
    // nextQueueProcessor_ while this SAME (still-alive, refcounted) object
    // is concurrently torn down early by stopProcessorsForShutdown() on
    // another thread -- the shared_ptr keeps the UpdatesManager itself
    // alive, but does nothing to protect its internal members from a
    // same-instance concurrent mutation. Without this lock that's a
    // null-deref/UAF race that does not exist today (today these members
    // are only ever mutated from ~UpdatesManager(), which the Singleton
    // machinery only runs once all shared_ptr holders have released).
    std::mutex processorsMutex_;

    // Declaration order matters here and must NOT change: members are
    // constructed in declaration order, so both processors are guaranteed to
    // exist before shutdownCallback_ registers (and can safely be reset from
    // it, even if the callback fires synchronously during construction
    // because the SingletonVault destruction token is already cancelled).
    // On (normal) destruction, members are torn down in reverse:
    // shutdownCallback_ first (safe/idempotent to destroy whether or not it
    // already fired), then the two processors (safe no-op if already reset).
    std::unique_ptr<CurrentQueueProcessor> currentQueueProcessor_;
    std::unique_ptr<NextQueueProcessor> nextQueueProcessor_;
    folly::CancellationCallback shutdownCallback_;
  };
  struct Singleton;

  static ObserverManager& getInstance();
  static std::shared_ptr<UpdatesManager> getUpdatesManager();
  static thread_local bool inManagerThread_;

  /**
   * Version mutex is used to make sure all updates are processed from the
   * CurrentQueue, before bumping the version and moving to the NextQueue.
   *
   * To achieve this every task added to CurrentQueue holds a reader lock.
   * NextQueue grabs a writer lock before bumping the version, so it can only
   * happen if CurrentQueue is empty (notice that we use read-priority shared
   * mutex).
   */
  mutable SharedMutexReadPriority versionMutex_;
  std::atomic<size_t> version_{1};

  using CycleDetector = GraphCycleDetector<const Core*>;
  folly::Synchronized<CycleDetector, std::mutex> cycleDetector_;
};
} // namespace observer_detail
} // namespace folly
