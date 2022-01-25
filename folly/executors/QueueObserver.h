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

#include <stdint.h>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <folly/Portability.h>
#include <folly/Synchronized.h>
#include <folly/portability/SysTypes.h>

namespace folly {

class RequestContext;

/**
 * WorkerProvider is a simple interface that can be used
 * to collect information about worker threads that are pulling work
 * from a given queue.
 */
class WorkerProvider {
 public:
  virtual ~WorkerProvider() {}

  /**
   * Abstract type returned by the collectThreadIds() method.
   * Implementations of the WorkerProvider interface need to define this class.
   * The intent is to return a guard along with a list of worker IDs which can
   * be removed on destruction of this object.
   */
  class KeepAlive {
   public:
    virtual ~KeepAlive() = 0;
  };

  // collectThreadIds() will return this aggregate type which includes an
  // instance of the WorkersGuard.
  struct IdsWithKeepAlive {
    std::unique_ptr<KeepAlive> guard;
    std::vector<pid_t> threadIds;
  };

  // Capture the Thread IDs of all threads consuming from a given queue.
  // The provided vector should be populated with the OS Thread IDs and the
  // method should return a SharedMutex which the caller can lock.
  virtual IdsWithKeepAlive collectThreadIds() = 0;
};

class ThreadIdWorkerProvider : public WorkerProvider {
 public:
  IdsWithKeepAlive collectThreadIds() override final;
  void addTid(pid_t tid);

  // Will block until all KeepAlives have been destroyed, if any exist
  void removeTid(pid_t tid);

 private:
  Synchronized<std::unordered_set<pid_t>> osThreadIds_;
  SharedMutex threadsExitMutex_;
};

class QueueObserver {
 public:
  virtual ~QueueObserver() {}

  virtual intptr_t onEnqueued(const RequestContext*) = 0;
  virtual void onDequeued(intptr_t) = 0;
};

class QueueObserverFactory {
 public:
  virtual ~QueueObserverFactory() {}
  virtual std::unique_ptr<QueueObserver> create(int8_t pri) = 0;

  static std::unique_ptr<QueueObserverFactory> make(
      const std::string& context,
      size_t numPriorities,
      WorkerProvider* workerProvider = nullptr);
};

using MakeQueueObserverFactory = std::unique_ptr<QueueObserverFactory>(
    const std::string&, size_t, WorkerProvider*);
#if FOLLY_HAVE_WEAK_SYMBOLS
FOLLY_ATTR_WEAK MakeQueueObserverFactory make_queue_observer_factory;
#else
constexpr MakeQueueObserverFactory* make_queue_observer_factory = nullptr;
#endif

} // namespace folly
