/*
 * Copyright 2017-present Facebook, Inc.
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

#include "SerialExecutor.h"

#include <mutex>
#include <queue>

#include <glog/logging.h>

#include <folly/ExceptionString.h>

namespace folly {

class SerialExecutor::TaskQueueImpl {
 public:
  void add(Func&& func) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(std::move(func));
  }

  void run() {
    std::unique_lock<std::mutex> lock(mutex_);

    ++scheduled_;

    if (scheduled_ > 1) {
      return;
    }

    do {
      DCHECK(!queue_.empty());
      Func func = std::move(queue_.front());
      queue_.pop();
      lock.unlock();

      try {
        func();
      } catch (std::exception const& ex) {
        LOG(ERROR) << "SerialExecutor: func threw unhandled exception "
                   << folly::exceptionStr(ex);
      } catch (...) {
        LOG(ERROR) << "SerialExecutor: func threw unhandled non-exception "
                      "object";
      }

      // Destroy the function (and the data it captures) before we acquire the
      // lock again.
      func = {};

      lock.lock();
      --scheduled_;
    } while (scheduled_);
  }

 private:
  std::mutex mutex_;
  std::size_t scheduled_{0};
  std::queue<Func> queue_;
};

SerialExecutor::SerialExecutor(std::shared_ptr<folly::Executor> parent)
    : parent_(std::move(parent)),
      taskQueueImpl_(std::make_shared<TaskQueueImpl>()) {}

void SerialExecutor::add(Func func) {
  taskQueueImpl_->add(std::move(func));
  parent_->add([impl = taskQueueImpl_] { impl->run(); });
}

void SerialExecutor::addWithPriority(Func func, int8_t priority) {
  taskQueueImpl_->add(std::move(func));
  parent_->addWithPriority([impl = taskQueueImpl_] { impl->run(); }, priority);
}

} // namespace folly
