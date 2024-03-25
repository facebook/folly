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

#include <glog/logging.h>

#include <folly/ExceptionString.h>

namespace folly::detail {

template <template <typename> typename Queue>
class SerialExecutorImpl<Queue>::Worker {
 public:
  explicit Worker(KeepAlive<SerialExecutorImpl> ka) : ka_(std::move(ka)) {}

  ~Worker() {
    if (ka_) {
      ka_->drain(); // We own the queue but we did not run.
    }
  }

  Worker(Worker&& other) : ka_(std::exchange(other.ka_, {})) {}

  Worker(const Worker&) = delete;
  Worker& operator=(const Worker&) = delete;
  Worker& operator=(Worker&&) = delete;

  void operator()() { std::exchange(ka_, {})->worker(); }

 private:
  KeepAlive<SerialExecutorImpl> ka_;
};

template <template <typename> typename Queue>
SerialExecutorImpl<Queue>::SerialExecutorImpl(KeepAlive<Executor> parent)
    : parent_(std::move(parent)) {}

template <template <typename> typename Queue>
SerialExecutorImpl<Queue>::~SerialExecutorImpl() {
  DCHECK(!keepAliveCounter_);
}

template <template <typename> typename Queue>
Executor::KeepAlive<SerialExecutorImpl<Queue>>
SerialExecutorImpl<Queue>::create(KeepAlive<Executor> parent) {
  return makeKeepAlive<SerialExecutorImpl<Queue>>(
      new SerialExecutorImpl<Queue>(std::move(parent)));
}

template <template <typename> typename Queue>
typename SerialExecutorImpl<Queue>::UniquePtr
SerialExecutorImpl<Queue>::createUnique(std::shared_ptr<Executor> parent) {
  auto executor =
      new SerialExecutorImpl<Queue>(getKeepAliveToken(parent.get()));
  return {executor, Deleter{std::move(parent)}};
}

template <template <typename> typename Queue>
bool SerialExecutorImpl<Queue>::keepAliveAcquire() noexcept {
  auto keepAliveCounter =
      keepAliveCounter_.fetch_add(1, std::memory_order_relaxed);
  DCHECK(keepAliveCounter > 0);
  return true;
}

template <template <typename> typename Queue>
void SerialExecutorImpl<Queue>::keepAliveRelease() noexcept {
  auto keepAliveCounter =
      keepAliveCounter_.fetch_sub(1, std::memory_order_acq_rel);
  DCHECK(keepAliveCounter > 0);
  if (keepAliveCounter == 1) {
    delete this;
  }
}

template <template <typename> typename Queue>
void SerialExecutorImpl<Queue>::add(Func func) {
  if (scheduleTask(std::move(func))) {
    parent_->add(Worker{getKeepAliveToken(this)});
  }
}

template <template <typename> typename Queue>
void SerialExecutorImpl<Queue>::addWithPriority(Func func, int8_t priority) {
  if (scheduleTask(std::move(func))) {
    parent_->addWithPriority(Worker{getKeepAliveToken(this)}, priority);
  }
}

template <template <typename> typename Queue>
bool SerialExecutorImpl<Queue>::scheduleTask(Func&& func) {
  queue_.enqueue(Task{std::move(func), RequestContext::saveContext()});
  // If this thread is the first to mark the queue as non-empty, schedule the
  // worker.
  return scheduled_.fetch_add(1, std::memory_order_acq_rel) == 0;
}

template <template <typename> typename Queue>
void SerialExecutorImpl<Queue>::worker() {
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

template <template <typename> typename Queue>
void SerialExecutorImpl<Queue>::drain() {
  auto queueSize = scheduled_.load(std::memory_order_acquire);
  while (queueSize != 0) {
    Task task;
    queue_.dequeue(task);
    queueSize = scheduled_.fetch_sub(1, std::memory_order_acq_rel) - 1;
  }
}

} // namespace folly::detail
