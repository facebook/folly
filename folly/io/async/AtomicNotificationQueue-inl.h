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

#include <folly/FileUtil.h>
#include <folly/io/async/AtomicNotificationQueue.h>
#include <folly/system/Pid.h>

namespace folly {

template <typename Task>
AtomicNotificationQueue<Task>::Queue::Queue(Queue&& other) noexcept
    : head_(std::exchange(other.head_, nullptr)),
      size_(std::exchange(other.size_, 0)) {}

template <typename Task>
typename AtomicNotificationQueue<Task>::Queue&
AtomicNotificationQueue<Task>::Queue::operator=(Queue&& other) noexcept {
  clear();
  std::swap(head_, other.head_);
  std::swap(size_, other.size_);
  return *this;
}

template <typename Task>
AtomicNotificationQueue<Task>::Queue::~Queue() {
  clear();
}

template <typename Task>
bool AtomicNotificationQueue<Task>::Queue::empty() const {
  return !head_;
}

template <typename Task>
ssize_t AtomicNotificationQueue<Task>::Queue::size() const {
  return size_;
}

template <typename Task>
typename AtomicNotificationQueue<Task>::Node&
AtomicNotificationQueue<Task>::Queue::front() {
  return *head_;
}

template <typename Task>
void AtomicNotificationQueue<Task>::Queue::pop() {
  std::unique_ptr<Node>(std::exchange(head_, head_->next));
  --size_;
}

template <typename Task>
void AtomicNotificationQueue<Task>::Queue::clear() {
  while (!empty()) {
    pop();
  }
}

template <typename Task>
AtomicNotificationQueue<Task>::Queue::Queue(Node* head, ssize_t size)
    : head_(head), size_(size) {}

template <typename Task>
typename AtomicNotificationQueue<Task>::Queue
AtomicNotificationQueue<Task>::Queue::fromReversed(Node* tail) {
  // Reverse a linked list.
  Node* head{nullptr};
  ssize_t size = 0;
  while (tail) {
    head = std::exchange(tail, std::exchange(tail->next, head));
    ++size;
  }
  return Queue(head, size);
}

template <typename Task>
AtomicNotificationQueue<Task>::AtomicQueue::~AtomicQueue() {
  DCHECK(!head_);
  if (reinterpret_cast<intptr_t>(head_.load(std::memory_order_relaxed)) ==
      kQueueArmedTag) {
    return;
  }
  if (auto head = head_.load(std::memory_order_acquire)) {
    auto queueContentsToDrop = Queue::fromReversed(head);
  }
}

template <typename Task>
template <typename... Args>
bool AtomicNotificationQueue<Task>::AtomicQueue::pushImpl(
    std::shared_ptr<RequestContext> rctx, Args&&... args) {
  std::unique_ptr<Node> node(
      new Node(std::move(rctx), std::forward<Args>(args)...));
  auto head = head_.load(std::memory_order_relaxed);
  while (true) {
    node->next =
        reinterpret_cast<intptr_t>(head) == kQueueArmedTag ? nullptr : head;
    if (head_.compare_exchange_weak(
            head,
            node.get(),
            std::memory_order_acq_rel,
            std::memory_order_relaxed)) {
      node.release();
      return reinterpret_cast<intptr_t>(head) == kQueueArmedTag;
    }
  }
}

template <typename Task>
template <typename... Args>
bool AtomicNotificationQueue<Task>::AtomicQueue::push(Args&&... args) {
  auto rctx = RequestContext::saveContext();
  return pushImpl(std::move(rctx), std::forward<Args>(args)...);
}

template <typename Task>
template <typename... Args>
bool AtomicNotificationQueue<Task>::AtomicQueue::push(
    std::shared_ptr<RequestContext> rctx, Args&&... args) {
  return pushImpl(std::move(rctx), std::forward<Args>(args)...);
}

template <typename Task>
bool AtomicNotificationQueue<Task>::AtomicQueue::hasTasks() const {
  auto head = head_.load(std::memory_order_relaxed);
  return head && reinterpret_cast<intptr_t>(head) != kQueueArmedTag;
}

template <typename Task>
typename AtomicNotificationQueue<Task>::Queue
AtomicNotificationQueue<Task>::AtomicQueue::getTasks() {
  auto head = head_.exchange(nullptr, std::memory_order_acquire);
  if (head && reinterpret_cast<intptr_t>(head) != kQueueArmedTag) {
    return Queue::fromReversed(head);
  }
  return {};
}

template <typename Task>
typename AtomicNotificationQueue<Task>::Queue
AtomicNotificationQueue<Task>::AtomicQueue::arm() {
  auto head = head_.load(std::memory_order_relaxed);
  if (!head &&
      head_.compare_exchange_strong(
          head,
          reinterpret_cast<Node*>(kQueueArmedTag),
          std::memory_order_release,
          std::memory_order_relaxed)) {
    return {};
  }
  DCHECK(reinterpret_cast<intptr_t>(head) != kQueueArmedTag);
  return getTasks();
}

template <typename Task>
AtomicNotificationQueue<Task>::AtomicNotificationQueue() {}

template <typename Task>
AtomicNotificationQueue<Task>::~AtomicNotificationQueue() {
  // Empty the queue
  atomicQueue_.getTasks();
}

template <typename Task>
void AtomicNotificationQueue<Task>::setMaxReadAtOnce(uint32_t maxAtOnce) {
  maxReadAtOnce_ = maxAtOnce;
}

template <typename Task>
size_t AtomicNotificationQueue<Task>::size() const {
  auto queueSize = pushCount_.load(std::memory_order_relaxed) -
      taskExecuteCount_.load(std::memory_order_relaxed);
  return queueSize >= 0 ? queueSize : 0;
}

template <typename Task>
bool AtomicNotificationQueue<Task>::empty() const {
  return queue_.empty() && !atomicQueue_.hasTasks();
}

template <typename Task>
bool AtomicNotificationQueue<Task>::arm() {
  if (!queue_.empty()) {
    return false;
  }
  auto queue = atomicQueue_.arm();
  if (queue.empty()) {
    return true;
  } else {
    queue_ = std::move(queue);
    return false;
  }
}

template <typename Task>
template <typename... Args>
bool AtomicNotificationQueue<Task>::push(Args&&... args) {
  pushCount_.fetch_add(1, std::memory_order_relaxed);
  return atomicQueue_.push(std::forward<Args>(args)...);
}

template <typename Task>
template <typename... Args>
bool AtomicNotificationQueue<Task>::push(
    std::shared_ptr<RequestContext> rctx, Args&&... args) {
  pushCount_.fetch_add(1, std::memory_order_relaxed);
  return atomicQueue_.push(std::move(rctx), std::forward<Args>(args)...);
}

template <typename Task>
typename AtomicNotificationQueue<Task>::TryPushResult
AtomicNotificationQueue<Task>::tryPush(Task&& task, uint32_t maxSize) {
  auto pushed = pushCount_.load(std::memory_order_relaxed);
  while (true) {
    auto executed = taskExecuteCount_.load(std::memory_order_relaxed);
    if (pushed - executed >= maxSize) {
      return TryPushResult::FAILED_LIMIT_REACHED;
    }
    if (pushCount_.compare_exchange_weak(
            pushed,
            pushed + 1,
            std::memory_order_relaxed,
            std::memory_order_relaxed)) {
      break;
    }
  }
  return atomicQueue_.push(std::move(task)) ? TryPushResult::SUCCESS_AND_ARMED
                                            : TryPushResult::SUCCESS;
}

namespace detail {
template <
    typename Task,
    typename Consumer,
    typename = std::enable_if_t<std::is_same<
        invoke_result_t<Consumer, Task&&>,
        AtomicNotificationQueueTaskStatus>::value>>
AtomicNotificationQueueTaskStatus invokeConsumerWithTask(
    Consumer&& consumer, Task&& task, std::shared_ptr<RequestContext>&& rctx) {
  RequestContextScopeGuard rcsg(std::move(rctx));
  return consumer(std::forward<Task>(task));
}

template <
    typename Task,
    typename Consumer,
    typename = std::enable_if_t<std::is_same<
        invoke_result_t<Consumer, Task&&, std::shared_ptr<RequestContext>&&>,
        AtomicNotificationQueueTaskStatus>::value>,
    typename = void>
AtomicNotificationQueueTaskStatus invokeConsumerWithTask(
    Consumer&& consumer, Task&& task, std::shared_ptr<RequestContext>&& rctx) {
  return consumer(
      std::forward<Task>(task),
      std::forward<std::shared_ptr<RequestContext>>(rctx));
}

template <
    typename Task,
    typename Consumer,
    typename = std::enable_if_t<
        std::is_same<invoke_result_t<Consumer, Task&&>, void>::value>,
    typename = void,
    typename = void>
AtomicNotificationQueueTaskStatus invokeConsumerWithTask(
    Consumer&& consumer, Task&& task, std::shared_ptr<RequestContext>&& rctx) {
  RequestContextScopeGuard rcsg(std::move(rctx));
  consumer(std::forward<Task>(task));
  return AtomicNotificationQueueTaskStatus::CONSUMED;
}

template <
    typename Task,
    typename Consumer,
    typename = std::enable_if_t<std::is_same<
        invoke_result_t<Consumer, Task&&, std::shared_ptr<RequestContext>&&>,
        void>::value>,
    typename = void,
    typename = void,
    typename = void>
AtomicNotificationQueueTaskStatus invokeConsumerWithTask(
    Consumer&& consumer, Task&& task, std::shared_ptr<RequestContext>&& rctx) {
  consumer(
      std::forward<Task>(task),
      std::forward<std::shared_ptr<RequestContext>>(rctx));
  return AtomicNotificationQueueTaskStatus::CONSUMED;
}

} // namespace detail

template <typename Task>
template <typename Consumer>
bool AtomicNotificationQueue<Task>::drive(Consumer&& consumer) {
  Queue nextQueue;
  // Since we cannot know if a task will be discarded before trying to execute
  // it, this check may cause this function to return early. That is, even
  // though:
  //   1. numConsumed < maxReadAtOnce_
  //   2. atomicQueue_ is not empty
  // This is not an issue in practice because these tasks will be executed in
  // the next round.
  //
  // In short, if `size() > maxReadAtOnce_`:
  //   * at least maxReadAtOnce_ tasks will be "processed"
  //   * at most maxReadAtOnce_ tasks will be "executed" (rest being discarded)
  if (maxReadAtOnce_ == 0 || queue_.size() < maxReadAtOnce_) {
    nextQueue = atomicQueue_.getTasks();
  }
  const bool wasAnyProcessed = !queue_.empty() || !nextQueue.empty();
  for (uint32_t numConsumed = 0;
       maxReadAtOnce_ == 0 || numConsumed < maxReadAtOnce_;) {
    if (queue_.empty()) {
      queue_ = std::move(nextQueue);
      if (queue_.empty()) {
        break;
      }
    }
    // This is faster than fetch_add and is safe because only consumer thread
    // writes to taskExecuteCount_.
    taskExecuteCount_.store(
        taskExecuteCount_.load(std::memory_order_relaxed) + 1,
        std::memory_order_relaxed);
    {
      auto& curNode = queue_.front();
      AtomicNotificationQueueTaskStatus consumeTaskStatus =
          detail::invokeConsumerWithTask(
              std::forward<Consumer>(consumer),
              std::move(curNode.task),
              std::move(curNode.rctx));
      if (consumeTaskStatus == AtomicNotificationQueueTaskStatus::CONSUMED) {
        ++numConsumed;
      }
    }
    queue_.pop();
  }
  return wasAnyProcessed;
}

} // namespace folly
