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

#pragma once

#include <folly/io/async/AtomicNotificationQueue.h>

#include <folly/FileUtil.h>
#include <folly/system/Pid.h>

namespace folly {

template <typename Task, typename Consumer>
AtomicNotificationQueue<Task, Consumer>::Queue::Queue(Queue&& other) noexcept
    : head_(std::exchange(other.head_, nullptr)),
      size_(std::exchange(other.size_, 0)) {}

template <typename Task, typename Consumer>
typename AtomicNotificationQueue<Task, Consumer>::Queue&
AtomicNotificationQueue<Task, Consumer>::Queue::operator=(
    Queue&& other) noexcept {
  clear();
  std::swap(head_, other.head_);
  std::swap(size_, other.size_);
  return *this;
}

template <typename Task, typename Consumer>
AtomicNotificationQueue<Task, Consumer>::Queue::~Queue() {
  clear();
}

template <typename Task, typename Consumer>
bool AtomicNotificationQueue<Task, Consumer>::Queue::empty() const {
  return !head_;
}

template <typename Task, typename Consumer>
ssize_t AtomicNotificationQueue<Task, Consumer>::Queue::size() const {
  return size_;
}

template <typename Task, typename Consumer>
typename AtomicNotificationQueue<Task, Consumer>::Node&
AtomicNotificationQueue<Task, Consumer>::Queue::front() {
  return *head_;
}

template <typename Task, typename Consumer>
void AtomicNotificationQueue<Task, Consumer>::Queue::pop() {
  std::unique_ptr<Node>(std::exchange(head_, head_->next));
  --size_;
}

template <typename Task, typename Consumer>
void AtomicNotificationQueue<Task, Consumer>::Queue::clear() {
  while (!empty()) {
    pop();
  }
}

template <typename Task, typename Consumer>
AtomicNotificationQueue<Task, Consumer>::Queue::Queue(Node* head, ssize_t size)
    : head_(head), size_(size) {}

template <typename Task, typename Consumer>
typename AtomicNotificationQueue<Task, Consumer>::Queue
AtomicNotificationQueue<Task, Consumer>::Queue::fromReversed(Node* tail) {
  // Reverse a linked list.
  Node* head{nullptr};
  ssize_t size = 0;
  while (tail) {
    head = std::exchange(tail, std::exchange(tail->next, head));
    ++size;
  }
  return Queue(head, size);
}

template <typename Task, typename Consumer>
AtomicNotificationQueue<Task, Consumer>::AtomicQueue::~AtomicQueue() {
  DCHECK(!head_);
  if (reinterpret_cast<intptr_t>(head_.load(std::memory_order_relaxed)) ==
      kQueueArmedTag) {
    return;
  }
  if (auto head = head_.load(std::memory_order_acquire)) {
    auto queueContentsToDrop = Queue::fromReversed(head);
  }
}

template <typename Task, typename Consumer>
template <typename T>
bool AtomicNotificationQueue<Task, Consumer>::AtomicQueue::push(T&& value) {
  std::unique_ptr<Node> node(new Node(std::forward<T>(value)));
  auto head = head_.load(std::memory_order_relaxed);
  while (true) {
    node->next =
        reinterpret_cast<intptr_t>(head) == kQueueArmedTag ? nullptr : head;
    if (head_.compare_exchange_weak(
            head,
            node.get(),
            std::memory_order_release,
            std::memory_order_relaxed)) {
      node.release();
      return reinterpret_cast<intptr_t>(head) == kQueueArmedTag;
    }
  }
}

template <typename Task, typename Consumer>
bool AtomicNotificationQueue<Task, Consumer>::AtomicQueue::hasTasks() const {
  auto head = head_.load(std::memory_order_relaxed);
  return head && reinterpret_cast<intptr_t>(head) != kQueueArmedTag;
}

template <typename Task, typename Consumer>
typename AtomicNotificationQueue<Task, Consumer>::Queue
AtomicNotificationQueue<Task, Consumer>::AtomicQueue::getTasks() {
  auto head = head_.exchange(nullptr, std::memory_order_acquire);
  if (head && reinterpret_cast<intptr_t>(head) != kQueueArmedTag) {
    return Queue::fromReversed(head);
  }
  if (reinterpret_cast<intptr_t>(head) == kQueueArmedTag) {
    ++consumerDisarmCount_;
  }
  return {};
}

template <typename Task, typename Consumer>
typename AtomicNotificationQueue<Task, Consumer>::Queue
AtomicNotificationQueue<Task, Consumer>::AtomicQueue::arm() {
  auto head = head_.load(std::memory_order_relaxed);
  if (!head &&
      head_.compare_exchange_strong(
          head,
          reinterpret_cast<Node*>(kQueueArmedTag),
          std::memory_order_relaxed,
          std::memory_order_relaxed)) {
    ++successfulArmCount_;
    return {};
  }
  DCHECK(reinterpret_cast<intptr_t>(head) != kQueueArmedTag);
  return getTasks();
}

template <typename Task, typename Consumer>
AtomicNotificationQueue<Task, Consumer>::AtomicNotificationQueue(
    Consumer&& consumer)
    : pid_(get_cached_pid()), consumer_(std::move(consumer)) {
#ifdef FOLLY_HAVE_EVENTFD
  eventfd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  if (eventfd_ == -1) {
    if (errno == ENOSYS || errno == EINVAL) {
      // eventfd not availalble
      LOG(ERROR) << "failed to create eventfd for AtomicNotificationQueue: "
                 << errno << ", falling back to pipe mode (is your kernel "
                 << "> 2.6.30?)";
    } else {
      // some other error
      folly::throwSystemError(
          "Failed to create eventfd for AtomicNotificationQueue", errno);
    }
  }
#endif
  if (eventfd_ == -1) {
    if (pipe(pipeFds_)) {
      folly::throwSystemError(
          "Failed to create pipe for AtomicNotificationQueue", errno);
    }
    try {
      // put both ends of the pipe into non-blocking mode
      if (fcntl(pipeFds_[0], F_SETFL, O_RDONLY | O_NONBLOCK) != 0) {
        folly::throwSystemError(
            "failed to put AtomicNotificationQueue pipe read "
            "endpoint into non-blocking mode",
            errno);
      }
      if (fcntl(pipeFds_[1], F_SETFL, O_WRONLY | O_NONBLOCK) != 0) {
        folly::throwSystemError(
            "failed to put AtomicNotificationQueue pipe write "
            "endpoint into non-blocking mode",
            errno);
      }
    } catch (...) {
      ::close(pipeFds_[0]);
      ::close(pipeFds_[1]);
      throw;
    }
  }
}

template <typename Task, typename Consumer>
AtomicNotificationQueue<Task, Consumer>::~AtomicNotificationQueue() {
  // Empty the queue
  atomicQueue_.getTasks();
  // Don't drain fd in the child process.
  if (pid_ == get_cached_pid()) {
    // Wait till we observe all the writes before closing fds
    while (writesObserved_ < atomicQueue_.getArmedPushCount() + writesLocal_) {
      drainFd();
    }
    DCHECK(writesObserved_ == atomicQueue_.getArmedPushCount() + writesLocal_);
  }
  if (eventfd_ >= 0) {
    ::close(eventfd_);
    eventfd_ = -1;
  }
  if (pipeFds_[0] >= 0) {
    ::close(pipeFds_[0]);
    pipeFds_[0] = -1;
  }
  if (pipeFds_[1] >= 0) {
    ::close(pipeFds_[1]);
    pipeFds_[1] = -1;
  }
}

template <typename Task, typename Consumer>
void AtomicNotificationQueue<Task, Consumer>::setMaxReadAtOnce(
    uint32_t maxAtOnce) {
  maxReadAtOnce_ = maxAtOnce;
}

template <typename Task, typename Consumer>
size_t AtomicNotificationQueue<Task, Consumer>::size() const {
  auto queueSize = pushCount_.load(std::memory_order_relaxed) -
      taskExecuteCount_.load(std::memory_order_relaxed);
  return queueSize >= 0 ? queueSize : 0;
}

template <typename Task, typename Consumer>
bool AtomicNotificationQueue<Task, Consumer>::empty() const {
  return queue_.empty() && !atomicQueue_.hasTasks();
}

template <typename Task, typename Consumer>
template <typename T>
void AtomicNotificationQueue<Task, Consumer>::putMessage(T&& task) {
  pushCount_.fetch_add(1, std::memory_order_relaxed);
  putMessageImpl(std::forward<T>(task));
}

template <typename Task, typename Consumer>
template <typename T>
bool AtomicNotificationQueue<Task, Consumer>::tryPutMessage(
    T&& task,
    uint32_t maxSize) {
  auto pushed = pushCount_.load(std::memory_order_relaxed);
  while (true) {
    auto executed = taskExecuteCount_.load(std::memory_order_relaxed);
    if (pushed - executed >= maxSize) {
      return false;
    }
    if (pushCount_.compare_exchange_weak(
            pushed,
            pushed + 1,
            std::memory_order_relaxed,
            std::memory_order_relaxed)) {
      break;
    }
  }
  putMessageImpl(std::forward<T>(task));
  return true;
}

template <typename Task, typename Consumer>
template <typename T>
void AtomicNotificationQueue<Task, Consumer>::putMessageImpl(T&& task) {
  if (atomicQueue_.push(std::forward<T>(task))) {
    notifyFd();
  }
}

template <typename Task, typename Consumer>
void AtomicNotificationQueue<Task, Consumer>::stopConsuming() {
  evb_ = nullptr;
  cancelLoopCallback();
  unregisterHandler();
  detachEventBase();
}

template <typename Task, typename Consumer>
void AtomicNotificationQueue<Task, Consumer>::startConsuming(EventBase* evb) {
  startConsumingImpl(evb, false);
}

template <typename Task, typename Consumer>
void AtomicNotificationQueue<Task, Consumer>::startConsumingInternal(
    EventBase* evb) {
  startConsumingImpl(evb, true);
}

template <typename Task, typename Consumer>
void AtomicNotificationQueue<Task, Consumer>::startConsumingImpl(
    EventBase* evb,
    bool internal) {
  evb_ = evb;
  initHandler(
      evb_,
      folly::NetworkSocket::fromFd(eventfd_ >= 0 ? eventfd_ : pipeFds_[0]));
  auto registerHandlerResult = internal
      ? registerInternalHandler(READ | PERSIST)
      : registerHandler(READ | PERSIST);
  if (registerHandlerResult) {
    edgeTriggeredSet_ = eventfd_ >= 0 && setEdgeTriggered();
    ++writesLocal_;
    notifyFd();
  } else {
    edgeTriggeredSet_ = false;
  }
}

namespace detail {
template <
    typename Task,
    typename Consumer,
    typename = std::enable_if_t<std::is_same<
        invoke_result_t<Consumer, Task&&>,
        AtomicNotificationQueueTaskStatus>::value>>
AtomicNotificationQueueTaskStatus invokeConsumerWithTask(
    Consumer& consumer,
    Task&& task) {
  return consumer(std::forward<Task>(task));
}

template <
    typename Task,
    typename Consumer,
    typename = std::enable_if_t<
        std::is_same<invoke_result_t<Consumer, Task&&>, void>::value>,
    typename = void>
AtomicNotificationQueueTaskStatus invokeConsumerWithTask(
    Consumer& consumer,
    Task&& task) {
  consumer(std::forward<Task>(task));
  return AtomicNotificationQueueTaskStatus::CONSUMED;
}

} // namespace detail

template <typename Task, typename Consumer>
bool AtomicNotificationQueue<Task, Consumer>::drive() {
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
  for (int32_t numConsumed = 0;
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
      RequestContextScopeGuard rcsg(std::move(curNode.rctx));
      AtomicNotificationQueueTaskStatus consumeTaskStatus =
          detail::invokeConsumerWithTask(consumer_, std::move(curNode.task));
      if (consumeTaskStatus == AtomicNotificationQueueTaskStatus::CONSUMED) {
        ++numConsumed;
      }
    }
    queue_.pop();
  }
  return wasAnyProcessed;
}

template <typename Task, typename Consumer>
void AtomicNotificationQueue<Task, Consumer>::drain() {
  while (drive()) {
  }
}

template <typename Task, typename Consumer>
void AtomicNotificationQueue<Task, Consumer>::notifyFd() {
  checkPid();

  ssize_t bytes_written = 0;
  size_t bytes_expected = 0;

  do {
    if (eventfd_ >= 0) {
      // eventfd(2) dictates that we must write a 64-bit integer
      uint64_t signal = 1;
      bytes_expected = sizeof(signal);
      bytes_written = ::write(eventfd_, &signal, bytes_expected);
    } else {
      uint8_t signal = 1;
      bytes_expected = sizeof(signal);
      bytes_written = ::write(pipeFds_[1], &signal, bytes_expected);
    }
  } while (bytes_written == -1 && errno == EINTR);

  if (bytes_written != ssize_t(bytes_expected)) {
    folly::throwSystemError(
        "failed to signal AtomicNotificationQueue after "
        "write",
        errno);
  }
}

template <typename Task, typename Consumer>
void AtomicNotificationQueue<Task, Consumer>::drainFd() {
  checkPid();

  uint64_t message = 0;
  if (eventfd_ >= 0) {
    auto result = readNoInt(eventfd_, &message, sizeof(message));
    CHECK(result == sizeof(message) || errno == EAGAIN);
    writesObserved_ += message;
  } else {
    ssize_t result;
    while ((result = readNoInt(pipeFds_[0], &message, sizeof(message))) != -1) {
      writesObserved_ += result;
    }
    CHECK(result == -1 && errno == EAGAIN);
  }
}

template <typename Task, typename Consumer>
void AtomicNotificationQueue<Task, Consumer>::runLoopCallback() noexcept {
  if (!queue_.empty()) {
    activateEvent();
    return;
  }
  queue_ = atomicQueue_.arm();
  if (!queue_.empty()) {
    activateEvent();
  }
}

template <typename Task, typename Consumer>
void AtomicNotificationQueue<Task, Consumer>::handlerReady(uint16_t) noexcept {
  execute();
}

template <typename Task, typename Consumer>
void AtomicNotificationQueue<Task, Consumer>::execute() {
  if (!edgeTriggeredSet_) {
    drainFd();
  }
  drive();
  evb_->runInLoop(this, false, nullptr);
}

template <typename Task, typename Consumer>
void AtomicNotificationQueue<Task, Consumer>::activateEvent() {
  if (!EventHandler::activateEvent(0)) {
    // Fallback for EventBase backends that don't support activateEvent
    ++writesLocal_;
    notifyFd();
  }
}

template <typename Task, typename Consumer>
void AtomicNotificationQueue<Task, Consumer>::checkPid() const {
  if (FOLLY_UNLIKELY(pid_ != get_cached_pid())) {
    checkPidFail();
  }
}

template <typename Task, typename Consumer>
[[noreturn]] FOLLY_NOINLINE void
AtomicNotificationQueue<Task, Consumer>::checkPidFail() const {
  folly::terminate_with<std::runtime_error>(
      "Pid mismatch. Pid = " + folly::to<std::string>(get_cached_pid()) +
      ". Expecting " + folly::to<std::string>(pid_));
}
} // namespace folly
