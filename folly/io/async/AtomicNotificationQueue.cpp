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

#include <folly/io/async/AtomicNotificationQueue.h>

#include <folly/ExceptionString.h>
#include <folly/FileUtil.h>
#include <folly/system/Pid.h>

namespace folly {

void AtomicNotificationQueue::Task::execute() && noexcept {
  RequestContextScopeGuard rctx(std::move(rctx_));
  try {
    func_();
    func_ = nullptr;
  } catch (...) {
    LOG(FATAL) << "Exception thrown by a task in AtomicNotificationQueue: "
               << exceptionStr(std::current_exception());
  }
}

AtomicNotificationQueue::Queue::Queue(Queue&& other) noexcept
    : head_(std::exchange(other.head_, nullptr)),
      size_(std::exchange(other.size_, 0)) {}

AtomicNotificationQueue::Queue& AtomicNotificationQueue::Queue::operator=(
    Queue&& other) noexcept {
  clear();
  std::swap(head_, other.head_);
  std::swap(size_, other.size_);
  return *this;
}

AtomicNotificationQueue::Queue::~Queue() {
  clear();
}

bool AtomicNotificationQueue::Queue::empty() const {
  return !head_;
}

ssize_t AtomicNotificationQueue::Queue::size() const {
  return size_;
}

AtomicNotificationQueue::Task& AtomicNotificationQueue::Queue::front() {
  return head_->value;
}

void AtomicNotificationQueue::Queue::pop() {
  std::unique_ptr<Node>(std::exchange(head_, head_->next));
  --size_;
}

void AtomicNotificationQueue::Queue::clear() {
  while (!empty()) {
    pop();
  }
}

AtomicNotificationQueue::Queue::Queue(Node* head, ssize_t size)
    : head_(head), size_(size) {}
AtomicNotificationQueue::Queue AtomicNotificationQueue::Queue::fromReversed(
    Node* tail) {
  // Reverse a linked list.
  Node* head{nullptr};
  ssize_t size = 0;
  while (tail) {
    head = std::exchange(tail, std::exchange(tail->next, head));
    ++size;
  }
  return Queue(head, size);
}

AtomicNotificationQueue::AtomicQueue::~AtomicQueue() {
  DCHECK(!head_);
  if (reinterpret_cast<intptr_t>(head_.load(std::memory_order_relaxed)) ==
      kQueueArmedTag) {
    return;
  }
  if (auto head = head_.load(std::memory_order_acquire)) {
    auto queueContentsToDrop = Queue::fromReversed(head);
  }
}

bool AtomicNotificationQueue::AtomicQueue::push(Task&& value) {
  pushCount_.fetch_add(1, std::memory_order_relaxed);

  std::unique_ptr<Queue::Node> node(new Queue::Node(std::move(value)));
  auto head = head_.load(std::memory_order_relaxed);
  while (true) {
    node->next =
        reinterpret_cast<intptr_t>(head) == kQueueArmedTag ? nullptr : head;
    if (atomic_compare_exchange_weak_explicit(
            &head_,
            &head,
            node.get(),
            std::memory_order_release,
            std::memory_order_relaxed)) {
      node.release();
      return reinterpret_cast<intptr_t>(head) == kQueueArmedTag;
    }
  }
}

bool AtomicNotificationQueue::AtomicQueue::hasTasks() const {
  auto head = head_.load(std::memory_order_relaxed);
  return head && reinterpret_cast<intptr_t>(head) != kQueueArmedTag;
}

AtomicNotificationQueue::Queue
AtomicNotificationQueue::AtomicQueue::getTasks() {
  auto head = head_.exchange(nullptr, std::memory_order_acquire);
  if (head && reinterpret_cast<intptr_t>(head) != kQueueArmedTag) {
    return Queue::fromReversed(head);
  }
  if (reinterpret_cast<intptr_t>(head) == kQueueArmedTag) {
    ++consumerDisarmCount_;
  }
  return {};
}

AtomicNotificationQueue::Queue AtomicNotificationQueue::AtomicQueue::arm() {
  auto head = head_.load(std::memory_order_relaxed);
  if (!head &&
      atomic_compare_exchange_strong_explicit(
          &head_,
          &head,
          reinterpret_cast<Queue::Node*>(kQueueArmedTag),
          std::memory_order_relaxed,
          std::memory_order_relaxed)) {
    ++successfulArmCount_;
    return {};
  }
  DCHECK(reinterpret_cast<intptr_t>(head) != kQueueArmedTag);
  return getTasks();
}

AtomicNotificationQueue::AtomicNotificationQueue() : pid_(get_cached_pid()) {
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

AtomicNotificationQueue::~AtomicNotificationQueue() {
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

void AtomicNotificationQueue::setMaxReadAtOnce(uint32_t maxAtOnce) {
  maxReadAtOnce_ = maxAtOnce;
}

int32_t AtomicNotificationQueue::size() const {
  auto queueSize = atomicQueue_.getPushCount() -
      taskExecuteCount_.load(std::memory_order_relaxed);
  DCHECK(!evb_ || !evb_->isInEventBaseThread() || queueSize >= 0);
  return queueSize >= 0 ? queueSize : 0;
}

bool AtomicNotificationQueue::empty() const {
  return queue_.empty() && !atomicQueue_.hasTasks();
}

void AtomicNotificationQueue::putMessage(Func&& func) {
  if (atomicQueue_.push(Task{std::move(func), RequestContext::saveContext()})) {
    notifyFd();
  }
}

void AtomicNotificationQueue::stopConsuming() {
  evb_ = nullptr;
  cancelLoopCallback();
  unregisterHandler();
  detachEventBase();
}

void AtomicNotificationQueue::startConsuming(EventBase* evb) {
  startConsumingImpl(evb, false);
}

void AtomicNotificationQueue::startConsumingInternal(EventBase* evb) {
  startConsumingImpl(evb, true);
}

void AtomicNotificationQueue::startConsumingImpl(
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

bool AtomicNotificationQueue::drive() {
  Queue nextQueue;
  if (maxReadAtOnce_ == 0 || queue_.size() < maxReadAtOnce_) {
    nextQueue = atomicQueue_.getTasks();
  }
  int32_t i;
  for (i = 0; maxReadAtOnce_ == 0 || i < maxReadAtOnce_; ++i) {
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
    std::move(queue_.front()).execute();
    queue_.pop();
  }
  return i > 0;
}

void AtomicNotificationQueue::drain() {
  while (drive()) {
  }
}

void AtomicNotificationQueue::notifyFd() {
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

void AtomicNotificationQueue::drainFd() {
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

void AtomicNotificationQueue::runLoopCallback() noexcept {
  if (!queue_.empty()) {
    activateEvent();
    return;
  }
  queue_ = atomicQueue_.arm();
  if (!queue_.empty()) {
    activateEvent();
  }
}

void AtomicNotificationQueue::handlerReady(uint16_t) noexcept {
  execute();
}

void AtomicNotificationQueue::execute() {
  if (!edgeTriggeredSet_) {
    drainFd();
  }
  drive();
  evb_->runInLoop(this, false, nullptr);
}

void AtomicNotificationQueue::activateEvent() {
  if (!EventHandler::activateEvent(0)) {
    // Fallback for EventBase backends that don't support activateEvent
    ++writesLocal_;
    notifyFd();
  }
}

void AtomicNotificationQueue::checkPid() const {
  if (FOLLY_UNLIKELY(pid_ != get_cached_pid())) {
    checkPidFail();
  }
}

[[noreturn]] FOLLY_NOINLINE void AtomicNotificationQueue::checkPidFail() const {
  folly::terminate_with<std::runtime_error>(
      "Pid mismatch. Pid = " + folly::to<std::string>(get_cached_pid()) +
      ". Expecting " + folly::to<std::string>(pid_));
}
} // namespace folly
