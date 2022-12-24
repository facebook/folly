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

#include <folly/executors/SerialExecutor.h>

#include <glog/logging.h>

#include <folly/ExceptionString.h>

namespace folly {

SerialExecutor::SerialExecutor(KeepAlive<Executor> parent)
    : parent_(std::move(parent)) {}

SerialExecutor::~SerialExecutor() {
  DCHECK(!keepAliveCounter_);
}

Executor::KeepAlive<SerialExecutor> SerialExecutor::create(
    KeepAlive<Executor> parent) {
  return makeKeepAlive<SerialExecutor>(new SerialExecutor(std::move(parent)));
}

SerialExecutor::UniquePtr SerialExecutor::createUnique(
    std::shared_ptr<Executor> parent) {
  auto executor = new SerialExecutor(getKeepAliveToken(parent.get()));
  return {executor, Deleter{std::move(parent)}};
}

bool SerialExecutor::keepAliveAcquire() noexcept {
  auto keepAliveCounter =
      keepAliveCounter_.fetch_add(1, std::memory_order_relaxed);
  DCHECK(keepAliveCounter > 0);
  return true;
}

void SerialExecutor::keepAliveRelease() noexcept {
  auto keepAliveCounter =
      keepAliveCounter_.fetch_sub(1, std::memory_order_acq_rel);
  DCHECK(keepAliveCounter > 0);
  if (keepAliveCounter == 1) {
    delete this;
  }
}

void SerialExecutor::add(Func func) {
  if (scheduleTask(std::move(func))) {
    parent_->add(
        [keepAlive = getKeepAliveToken(this)] { keepAlive->worker(); });
  }
}

void SerialExecutor::addWithPriority(Func func, int8_t priority) {
  if (scheduleTask(std::move(func))) {
    parent_->addWithPriority(
        [keepAlive = getKeepAliveToken(this)] { keepAlive->worker(); },
        priority);
  }
}

bool SerialExecutor::scheduleTask(Func&& func) {
  queue_.enqueue(Task{std::move(func), RequestContext::saveContext()});
  // If this thread is the first to mark the queue as non-empty, schedule the
  // worker.
  return scheduled_.fetch_add(1, std::memory_order_acq_rel) == 0;
}

void SerialExecutor::worker() {
  std::size_t queueSize = scheduled_.load(std::memory_order_acquire);
  DCHECK_NE(queueSize, 0);

  std::size_t processed = 0;
  while (true) {
    Task task;
    queue_.dequeue(task);
    folly::RequestContextScopeGuard ctxGuard(std::move(task.ctx));
    invokeCatchingExns("SerialExecutor: func", std::exchange(task.func, {}));

    if (++processed == queueSize) {
      // NOTE: scheduled_ must be decremented after the task has been processed,
      // or add() may concurrently start another worker.
      queueSize = scheduled_.fetch_sub(queueSize, std::memory_order_acq_rel) -
          queueSize;
      if (queueSize == 0) {
        // Queue is now empty
        return;
      }
      processed = 0;
    }
  }
}

} // namespace folly
