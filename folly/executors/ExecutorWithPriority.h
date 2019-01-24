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

#pragma once

#include <folly/Executor.h>
#include <atomic>

namespace folly {

class ExecutorWithPriority : public virtual Executor {
 public:
  ExecutorWithPriority(ExecutorWithPriority const&) = delete;
  ExecutorWithPriority& operator=(ExecutorWithPriority const&) = delete;
  ExecutorWithPriority(ExecutorWithPriority&&) = delete;
  ExecutorWithPriority& operator=(ExecutorWithPriority&&) = delete;

  static Executor::KeepAlive<ExecutorWithPriority> create(
      KeepAlive<Executor> executor,
      int8_t priority);

  void add(Func func) override;

 protected:
  bool keepAliveAcquire() override;
  void keepAliveRelease() override;

 private:
  ExecutorWithPriority(KeepAlive<Executor> executor, int8_t priority)
      : executor_(std::move(executor)), priority_(priority) {}

  std::atomic<ssize_t> keepAliveCounter_{1};
  KeepAlive<Executor> executor_;
  int8_t priority_;
};
} // namespace folly
