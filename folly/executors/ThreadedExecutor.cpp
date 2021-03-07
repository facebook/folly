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

#include <folly/executors/ThreadedExecutor.h>

#include <chrono>
#include <utility>

#include <glog/logging.h>

#include <folly/ScopeGuard.h>
#include <folly/executors/thread_factory/NamedThreadFactory.h>
#include <folly/system/ThreadName.h>

namespace folly {

ThreadedExecutor::ThreadedExecutor(std::shared_ptr<ThreadFactory> threadFactory)
    : threadFactory_(std::move(threadFactory)),
      controlThread_([this] { control(); }) {}

ThreadedExecutor::~ThreadedExecutor() {
  stopping_.store(true, std::memory_order_release);
  controlMessages_.enqueue({Message::Type::StopControl, {}, {}});
  controlThread_.join();
  CHECK(running_.empty());
  CHECK(controlMessages_.empty());
}

void ThreadedExecutor::add(Func func) {
  CHECK(!stopping_.load(std::memory_order_acquire));
  controlMessages_.enqueue({Message::Type::Start, std::move(func), {}});
}

std::shared_ptr<ThreadFactory> ThreadedExecutor::newDefaultThreadFactory() {
  return std::make_shared<NamedThreadFactory>("Threaded");
}

void ThreadedExecutor::work(Func& func) {
  invokeCatchingExns("ThreadedExecutor: func", std::exchange(func, {}));
  controlMessages_.enqueue(
      {Message::Type::Join, {}, std::this_thread::get_id()});
}

void ThreadedExecutor::control() {
  folly::setThreadName("ThreadedCtrl");
  bool controlStopping = false;
  while (!(controlStopping && running_.empty())) {
    auto msg = controlMessages_.dequeue();
    switch (msg.type) {
      case Message::Type::Start: {
        auto th = threadFactory_->newThread(
            [this, func = std::move(msg.startFunc)]() mutable { work(func); });
        auto id = th.get_id();
        running_[id] = std::move(th);
        break;
      }
      case Message::Type::Join: {
        auto it = running_.find(msg.joinTid);
        CHECK(it != running_.end());
        it->second.join();
        running_.erase(it);
        break;
      }
      case Message::Type::StopControl: {
        CHECK(!std::exchange(controlStopping, true));
        break;
      }
    }
  }
}

} // namespace folly
