/*
 * Copyright 2016-present Facebook, Inc.
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
#pragma once

#include <folly/ExceptionString.h>
#include <folly/Function.h>
#include <folly/executors/DrivableExecutor.h>
#include <folly/executors/SequencedExecutor.h>
#include <folly/io/async/NotificationQueue.h>

namespace folly {
namespace python {

class AsyncioExecutor : public DrivableExecutor, public SequencedExecutor {
 public:
  using Func = folly::Func;

  void add(Func func) override {
    queue_.putMessage(std::move(func));
  }

  int fileno() const {
    return consumer_.getFd();
  }

  void drive() noexcept override {
    consumer_.consumeUntilDrained([](Func&& func) {
      try {
        func();
      } catch (...) {
        LOG(ERROR) << "Exception thrown by NotificationQueueExecutor task."
                   << "Exception message: "
                   << folly::exceptionStr(std::current_exception());
      }
    });
  }

 private:
  folly::NotificationQueue<Func> queue_;
  folly::NotificationQueue<Func>::SimpleConsumer consumer_{queue_};
}; // AsyncioExecutor

} // namespace python
} // namespace folly
