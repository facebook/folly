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
#include <folly/io/async/EventBase.h>
#include <folly/io/async/EventHandler.h>
#include <folly/portability/Fcntl.h>
#include <folly/portability/Sockets.h>
#include <folly/portability/Unistd.h>

#if __has_include(<sys/eventfd.h>)
#include <sys/eventfd.h>
#endif

namespace folly {

/**
 * A producer-consumer queue for passing tasks to EventBase thread.
 *
 * Tasks can be added to the queue from any thread. A single EventBase
 * thread can be listening to the queue. Tasks are processed in FIFO order.
 */
template <typename Task, typename Consumer>
class EventBaseAtomicNotificationQueue : private EventBase::LoopCallback,
                                         private EventHandler {
  static_assert(
      noexcept(std::declval<Consumer>()(std::declval<Task&&>())),
      "Consumer::operator()(Task&&) should be noexcept.");

 public:
  explicit EventBaseAtomicNotificationQueue(Consumer&& consumer);

  template <
      typename C = Consumer,
      typename = std::enable_if_t<std::is_default_constructible<C>::value>>
  EventBaseAtomicNotificationQueue()
      : EventBaseAtomicNotificationQueue(Consumer()) {}

  ~EventBaseAtomicNotificationQueue() override;

  /*
   * Set the maximum number of tasks processed in a single round.
   * Can be called from consumer thread only.
   */
  void setMaxReadAtOnce(uint32_t maxAtOnce);

  /*
   * Returns the number of tasks in the queue.
   * Can be called from any thread.
   */
  size_t size() const;

  /*
   * Checks if the queue is empty.
   * Can be called from consumer thread only.
   */
  bool empty() const;

  /*
   * Executes all tasks until the queue is empty.
   * Can be called from consumer thread only.
   */
  void drain();

  /*
   * Adds a task into the queue.
   * Can be called from any thread.
   */
  template <typename... Args>
  void putMessage(Args&&... task);

  /**
   * Adds a task into the queue unless the max queue size is reached.
   * Returns true iff the task was queued.
   * Can be called from any thread.
   */
  FOLLY_NODISCARD bool tryPutMessage(Task&& task, uint32_t maxSize);

  /*
   * Detaches the queue from an EventBase.
   * Can be called from consumer thread only.
   */
  void stopConsuming();

  /*
   * Attaches the queue to an EventBase.
   * Can be called from consumer thread only.
   */
  void startConsuming(EventBase* evb);

  /*
   * Attaches the queue to an EventBase.
   * Can be called from consumer thread only.
   *
   * Unlike startConsuming, startConsumingInternal registers this queue as
   * an internal event. This means that this event may be skipped if
   * EventBase doesn't have any other registered events. This generally should
   * only be used for queues managed by an EventBase itself.
   */
  void startConsumingInternal(EventBase* evb);

  /*
   * Executes one round of tasks. Re-activates the event if more tasks are
   * available.
   * Can be called from consumer thread only.
   */
  void execute();

 private:
  /*
   * Adds a task to the queue without incrementing the push count.
   */
  template <typename T>
  void putMessageImpl(T&& task);

  template <typename T>
  bool drive(T&& t);

  /*
   * Write into the signal fd to wake up the consumer thread.
   */
  void notifyFd();

  /*
   * Read all messages from the signal fd.
   */
  void drainFd();

  /*
   * Either arm the queue or reactivate the EventBase event.
   * This has to be a loop callback because the event can't be activated from
   * within the event callback. It also allows delayed re-arming the queue.
   */
  void runLoopCallback() noexcept override;

  void startConsumingImpl(EventBase* evb, bool internal);

  void handlerReady(uint16_t) noexcept override;

  void activateEvent();

  /**
   * Check that the AtomicNotificationQueue is being used from the correct
   * process.
   *
   * If you create a AtomicNotificationQueue in one process, then fork, and try
   * to send messages to the queue from the child process, you're going to have
   * a bad time.  Unfortunately users have (accidentally) run into this.
   *
   * Because we use an eventfd/pipe, the child process can actually signal the
   * parent process that an event is ready.  However, it can't put anything on
   * the parent's queue, so the parent wakes up and finds an empty queue.  This
   * check ensures that we catch the problem in the misbehaving child process
   * code, and crash before signalling the parent process.
   */
  void checkPid() const;

  [[noreturn]] FOLLY_NOINLINE void checkPidFail() const;

  int eventfd_{-1};
  int pipeFds_[2]{-1, -1}; // to fallback to on older/non-linux systems
  /*
   * If event is registered with the EventBase, this describes whether
   * edge-triggered flag was set for it. For edge-triggered events we don't
   * need to drain the fd to deactivate them.
   */
  EventBase* evb_{nullptr};
  const pid_t pid_;
  AtomicNotificationQueue<Task> notificationQueue_;
  Consumer consumer_;
  ssize_t successfulArmCount_{0};
  ssize_t consumerDisarmedCount_{0};
  ssize_t writesObserved_{0};
  ssize_t writesLocal_{0};
  bool armed_{false};
  bool edgeTriggeredSet_{false};
};

} // namespace folly

#include <folly/io/async/EventBaseAtomicNotificationQueue-inl.h>
