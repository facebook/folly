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

#include <folly/executors/QueueObserver.h>

namespace {

std::unique_ptr<folly::QueueObserverFactory>
make_queue_observer_factory_fallback(
    const std::string&, size_t, folly::WorkerProvider*) noexcept {
  return std::unique_ptr<folly::QueueObserverFactory>();
}

class WorkerKeepAlive : public folly::WorkerProvider::KeepAlive {
 public:
  explicit WorkerKeepAlive(std::shared_lock<folly::SharedMutex> idsLock)
      : threadsExitLock_(std::move(idsLock)) {}
  ~WorkerKeepAlive() override {}

 private:
  std::shared_lock<folly::SharedMutex> threadsExitLock_;
};

} // namespace

namespace folly {

ThreadIdWorkerProvider::IdsWithKeepAlive
ThreadIdWorkerProvider::collectThreadIds() {
  auto keepAlive =
      std::make_unique<WorkerKeepAlive>(std::shared_lock{threadsExitMutex_});
  auto locked = osThreadIds_.rlock();
  return {std::move(keepAlive), {locked->begin(), locked->end()}};
}

void ThreadIdWorkerProvider::addTid(pid_t tid) {
  osThreadIds_.wlock()->insert(tid);
}

void ThreadIdWorkerProvider::removeTid(pid_t tid) {
  osThreadIds_.wlock()->erase(tid);
  // block until all WorkerKeepAlives have been destroyed
  std::unique_lock w{threadsExitMutex_};
}

WorkerProvider::KeepAlive::~KeepAlive() {}

/* static */ std::unique_ptr<QueueObserverFactory> QueueObserverFactory::make(
    const std::string& context,
    size_t numPriorities,
    WorkerProvider* workerProvider) {
  auto f = make_queue_observer_factory ? make_queue_observer_factory
                                       : make_queue_observer_factory_fallback;
  return f(context, numPriorities, workerProvider);
}
} // namespace folly
