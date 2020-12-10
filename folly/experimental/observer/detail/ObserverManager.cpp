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

#include <folly/experimental/observer/detail/ObserverManager.h>

#include <future>

#include <folly/ExceptionString.h>
#include <folly/Format.h>
#include <folly/Range.h>
#include <folly/Singleton.h>
#include <folly/concurrency/UnboundedQueue.h>
#include <folly/portability/GFlags.h>
#include <folly/system/ThreadName.h>

namespace folly {
namespace observer_detail {

thread_local bool ObserverManager::inManagerThread_{false};
thread_local ObserverManager::DependencyRecorder::Dependencies*
    ObserverManager::DependencyRecorder::currentDependencies_{nullptr};

DEFINE_int32(
    observer_manager_pool_size,
    4,
    "How many internal threads ObserverManager should use");

namespace {
constexpr StringPiece kObserverManagerThreadNamePrefix{"ObserverMngr"};
constexpr size_t kNextBatchSize{1024};
} // namespace

class ObserverManager::UpdatesManager::CurrentQueue {
 public:
  CurrentQueue() {
    if (FLAGS_observer_manager_pool_size < 1) {
      LOG(ERROR) << "--observer_manager_pool_size should be >= 1";
      FLAGS_observer_manager_pool_size = 1;
    }
    for (int32_t i = 0; i < FLAGS_observer_manager_pool_size; ++i) {
      threads_.emplace_back([this, i]() {
        folly::setThreadName(
            folly::sformat("{}{}", kObserverManagerThreadNamePrefix, i));
        ObserverManager::inManagerThread_ = true;

        while (true) {
          Function<void()> task;
          queue_.dequeue(task);

          if (!task) {
            return;
          }

          try {
            task();
          } catch (...) {
            LOG(ERROR) << "Exception while running CurrentQueue task: "
                       << exceptionStr(std::current_exception());
          }
        }
      });
    }
  }

  ~CurrentQueue() {
    for (size_t i = 0; i < threads_.size(); ++i) {
      queue_.enqueue(nullptr);
    }

    for (auto& thread : threads_) {
      thread.join();
    }

    CHECK(queue_.empty());
  }

  void add(Function<void()> task) { queue_.enqueue(std::move(task)); }

 private:
  UMPMCQueue<Function<void()>, true> queue_;
  std::vector<std::thread> threads_;
};

class ObserverManager::UpdatesManager::NextQueue {
 public:
  NextQueue() {
    thread_ = std::thread([&]() {
      auto& manager = getInstance();

      folly::setThreadName(
          folly::sformat("{}NQ", kObserverManagerThreadNamePrefix));

      Core::WeakPtr queueCoreWeak;

      while (true) {
        queue_.dequeue(queueCoreWeak);
        if (stop_) {
          return;
        }

        std::vector<Core::Ptr> cores;
        {
          if (auto queueCore = queueCoreWeak.lock()) {
            cores.emplace_back(std::move(queueCore));
          }
        }

        {
          SharedMutexReadPriority::WriteHolder wh(manager.versionMutex_);

          // We can't pick more tasks from the queue after we bumped the
          // version, so we have to do this while holding the lock.
          while (cores.size() < kNextBatchSize &&
                 queue_.try_dequeue(queueCoreWeak)) {
            if (stop_) {
              return;
            }
            if (auto queueCore = queueCoreWeak.lock()) {
              cores.emplace_back(std::move(queueCore));
            }
          }

          for (auto& corePtr : cores) {
            corePtr->setForceRefresh();
          }

          ++manager.version_;
        }

        for (auto& core : cores) {
          manager.scheduleRefresh(std::move(core), manager.version_);
        }

        {
          auto wEmptyWaiters = emptyWaiters_.wlock();
          // We don't want any new waiters to be added while we are checking the
          // queue.
          if (queue_.empty()) {
            for (auto& promise : *wEmptyWaiters) {
              promise.set_value();
            }
            wEmptyWaiters->clear();
          }
        }
      }
    });
  }

  void add(Core::WeakPtr core) { queue_.enqueue(std::move(core)); }

  ~NextQueue() {
    stop_ = true;
    // Write to the queue to notify the thread.
    queue_.enqueue(Core::WeakPtr());
    thread_.join();
  }

  void waitForEmpty() {
    std::promise<void> promise;
    auto future = promise.get_future();
    emptyWaiters_.wlock()->push_back(std::move(promise));

    // Write to the queue to notify the thread.
    queue_.enqueue(Core::WeakPtr());

    future.get();
  }

 private:
  UMPSCQueue<Core::WeakPtr, true> queue_;
  std::thread thread_;
  std::atomic<bool> stop_{false};
  folly::Synchronized<std::vector<std::promise<void>>> emptyWaiters_;
};

ObserverManager::UpdatesManager::UpdatesManager() {
  currentQueue_ = std::make_unique<CurrentQueue>();
  nextQueue_ = std::make_unique<NextQueue>();
}

ObserverManager::UpdatesManager::~UpdatesManager() {
  // Destroy NextQueue, before the rest of this object, since it expects
  // ObserverManager to be alive.
  nextQueue_.reset();
  currentQueue_.reset();
}

void ObserverManager::UpdatesManager::scheduleCurrent(Function<void()> task) {
  currentQueue_->add(std::move(task));
}

void ObserverManager::UpdatesManager::scheduleNext(Core::WeakPtr core) {
  nextQueue_->add(std::move(core));
}

void ObserverManager::waitForAllUpdates() {
  if (auto updatesManager = getUpdatesManager()) {
    return updatesManager->waitForAllUpdates();
  }
}

void ObserverManager::UpdatesManager::waitForAllUpdates() {
  auto& instance = ObserverManager::getInstance();
  nextQueue_->waitForEmpty();
  // Wait for all readers to release the lock.
  SharedMutexReadPriority::WriteHolder wh(instance.versionMutex_);
}

struct ObserverManager::Singleton {
  static folly::Singleton<UpdatesManager> instance;
  // MSVC 2015 doesn't let us access ObserverManager's constructor if we
  // try to use a lambda to initialize instance, so we have to create
  // an actual function instead.
  static UpdatesManager* createManager() { return new UpdatesManager(); }
};

folly::Singleton<ObserverManager::UpdatesManager>
    ObserverManager::Singleton::instance(createManager);

std::shared_ptr<ObserverManager::UpdatesManager>
ObserverManager::getUpdatesManager() {
  return Singleton::instance.try_get();
}

ObserverManager& ObserverManager::getInstance() {
  static auto instance = new ObserverManager();
  return *instance;
}
} // namespace observer_detail
} // namespace folly
