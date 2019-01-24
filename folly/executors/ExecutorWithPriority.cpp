/*
 * Copyright 2019-present Facebook, Inc.
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
#include <folly/executors/ExecutorWithPriority.h>
#include <glog/logging.h>

namespace folly {
Executor::KeepAlive<ExecutorWithPriority> ExecutorWithPriority::create(
    KeepAlive<Executor> executor,
    int8_t priority) {
  return makeKeepAlive<ExecutorWithPriority>(
      new ExecutorWithPriority(std::move(executor), priority));
}

void ExecutorWithPriority::add(Func func) {
  executor_->addWithPriority(std::move(func), priority_);
}

bool ExecutorWithPriority::keepAliveAcquire() {
  auto keepAliveCounter =
      keepAliveCounter_.fetch_add(1, std::memory_order_relaxed);
  DCHECK(keepAliveCounter > 0);
  return true;
}

void ExecutorWithPriority::keepAliveRelease() {
  auto keepAliveCounter =
      keepAliveCounter_.fetch_sub(1, std::memory_order_acq_rel);
  DCHECK(keepAliveCounter > 0);
  if (keepAliveCounter == 1) {
    delete this;
  }
}
} // namespace folly
