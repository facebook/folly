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

#include <folly/executors/StrandExecutor.h>

#include <glog/logging.h>

#include <folly/ExceptionString.h>
#include <folly/executors/GlobalExecutor.h>

namespace folly {

// Callable that will process a batch of tasks scheduled
// for execution on the same executor when called.
class StrandContext::Task {
 public:
  explicit Task(std::shared_ptr<StrandContext> ctx) noexcept
      : context_(std::move(ctx)) {}

  Task(Task&& t) noexcept : context_(std::move(t.context_)) {}

  Task(const Task&) = delete;
  Task& operator=(Task&&) = delete;
  Task& operator=(const Task&) = delete;

  ~Task() {
    if (context_) {
      // Task destructed without being invoked.
      // This either happens because an error occurred when attempting
      // to enqueue this task to an Executor, or if the Executor dropped
      // the task without executing it. Both of these situations are fatal.
      LOG(FATAL) << "StrandExecutor Task destroyed without being executed";
    }
  }

  void operator()() noexcept {
    DCHECK(context_);
    StrandContext::executeNext(std::move(context_));
  }

 private:
  std::shared_ptr<StrandContext> context_;
};

std::shared_ptr<StrandContext> StrandContext::create() {
  return std::make_shared<StrandContext>(PrivateTag{});
}

void StrandContext::add(Func func, Executor::KeepAlive<> executor) {
  addImpl(QueueItem{std::move(func), std::move(executor), folly::none});
}

void StrandContext::addWithPriority(
    Func func, Executor::KeepAlive<> executor, int8_t priority) {
  addImpl(QueueItem{std::move(func), std::move(executor), priority});
}

void StrandContext::addImpl(QueueItem&& item) {
  queue_.enqueue(std::move(item));
  if (scheduled_.fetch_add(1, std::memory_order_acq_rel) == 0) {
    // This thread was first to mark queue as nonempty, so we are
    // responsible for scheduling the first queued item onto the
    // executor.

    // Note that due to a potential race with another thread calling
    // add[WithPriority]() concurrently, the first item in queue is
    // not necessarily the item this thread just enqueued so need
    // to re-query it from the queue.
    dispatchFrontQueueItem(shared_from_this());
  }
}

void StrandContext::executeNext(
    std::shared_ptr<StrandContext> thisPtr) noexcept {
  // Put a cap on the number of items we process in one batch before
  // rescheduling on to the executor to avoid starvation of other
  // items queued to the current executor.
  const std::size_t maxItemsToProcessSynchronously = 32;

  std::size_t queueSize = thisPtr->scheduled_.load(std::memory_order_acquire);
  DCHECK(queueSize != 0u);

  const QueueItem* nextItem = nullptr;

  std::size_t pendingCount = 0;
  for (std::size_t i = 0; i < maxItemsToProcessSynchronously; ++i) {
    QueueItem item = thisPtr->queue_.dequeue();
    Executor::invokeCatchingExns(
        "StrandExecutor: func", std::exchange(item.func, {}));

    ++pendingCount;

    if (pendingCount == queueSize) {
      queueSize = thisPtr->scheduled_.fetch_sub(
                      pendingCount, std::memory_order_acq_rel) -
          pendingCount;
      if (queueSize == 0) {
        // Queue is now empty
        return;
      }

      pendingCount = 0;
    }

    nextItem = thisPtr->queue_.try_peek();
    DCHECK(nextItem != nullptr);

    // Check if the next item has the same executor.
    // If so we'll go around the loop again, otherwise
    // we'll dispatch to the other executor and return.
    if (nextItem->executor.get() != item.executor.get()) {
      break;
    }
  }

  DCHECK(nextItem != nullptr);
  DCHECK(pendingCount < queueSize);

  [[maybe_unused]] auto prevQueueSize =
      thisPtr->scheduled_.fetch_sub(pendingCount, std::memory_order_relaxed);
  DCHECK(pendingCount < prevQueueSize);

  // Reuse the shared_ptr from the previous item.
  dispatchFrontQueueItem(std::move(thisPtr));
}

void StrandContext::dispatchFrontQueueItem(
    std::shared_ptr<StrandContext> thisPtr) noexcept {
  const QueueItem* item = thisPtr->queue_.try_peek();
  DCHECK(item != nullptr);

  // NOTE: We treat any failure to schedule work onto the item's executor as a
  // fatal error. This will either result in LOG(FATAL) being called from
  // the Task destructor or std::terminate() being called by
  // exception-unwinding, depending on the compiler/runtime.
  Task task{std::move(thisPtr)};
  if (item->priority.has_value()) {
    item->executor->addWithPriority(std::move(task), item->priority.value());
  } else {
    item->executor->add(std::move(task));
  }
}

Executor::KeepAlive<StrandExecutor> StrandExecutor::create() {
  return create(StrandContext::create(), getGlobalCPUExecutor());
}

Executor::KeepAlive<StrandExecutor> StrandExecutor::create(
    std::shared_ptr<StrandContext> context) {
  return create(std::move(context), getGlobalCPUExecutor());
}

Executor::KeepAlive<StrandExecutor> StrandExecutor::create(
    Executor::KeepAlive<> parentExecutor) {
  return create(StrandContext::create(), std::move(parentExecutor));
}

Executor::KeepAlive<StrandExecutor> StrandExecutor::create(
    std::shared_ptr<StrandContext> context,
    Executor::KeepAlive<> parentExecutor) {
  auto* ex = new StrandExecutor(std::move(context), std::move(parentExecutor));
  return Executor::makeKeepAlive<StrandExecutor>(ex);
}

void StrandExecutor::add(Func f) {
  context_->add(std::move(f), parent_);
}

void StrandExecutor::addWithPriority(Func f, int8_t priority) {
  context_->addWithPriority(std::move(f), parent_, priority);
}

uint8_t StrandExecutor::getNumPriorities() const {
  return parent_->getNumPriorities();
}

bool StrandExecutor::keepAliveAcquire() noexcept {
  [[maybe_unused]] auto oldCount =
      refCount_.fetch_add(1, std::memory_order_relaxed);
  DCHECK(oldCount > 0);
  return true;
}

void StrandExecutor::keepAliveRelease() noexcept {
  const auto oldCount = refCount_.fetch_sub(1, std::memory_order_acq_rel);
  DCHECK(oldCount > 0);
  if (oldCount == 1) {
    // Last reference.
    delete this;
  }
}

StrandExecutor::StrandExecutor(
    std::shared_ptr<StrandContext> context,
    Executor::KeepAlive<> parent) noexcept
    : refCount_(1), parent_(std::move(parent)), context_(std::move(context)) {}

} // namespace folly
