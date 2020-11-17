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

#include <folly/io/async/EventBase.h>
#include <folly/io/async/EventHandler.h>
#include <folly/portability/Fcntl.h>
#include <folly/portability/Sockets.h>
#include <folly/portability/Unistd.h>

#if defined(__linux__) && !defined(__ANDROID__)
#define FOLLY_HAVE_EVENTFD
#include <folly/io/async/EventFDWrapper.h>
#endif

#include <type_traits>

namespace folly {

/**
 * A producer-consumer queue for passing tasks to EventBase thread.
 *
 * Tasks can be added to the queue from any thread. A single EventBase
 * thread can be listening to the queue. Tasks are processed in FIFO order.
 */
template <typename Task, typename Consumer>
class AtomicNotificationQueue : private EventBase::LoopCallback,
                                private EventHandler {
  static_assert(
      noexcept(std::declval<Consumer>()(std::declval<Task&&>())),
      "Consumer::operator()(Task&&) should be noexcept.");
  class AtomicQueue;
  struct Node {
    Task task;
    std::shared_ptr<RequestContext> rctx{RequestContext::saveContext()};

   private:
    friend class AtomicNotificationQueue;

    template <typename T>
    explicit Node(T&& t) : task(std::forward<T>(t)) {}

    Node* next{};
  };

  class Queue {
   public:
    Queue() {}
    Queue(Queue&& other) noexcept;
    Queue& operator=(Queue&& other) noexcept;
    ~Queue();

    bool empty() const;

    ssize_t size() const;

    Node& front();

    void pop();

    void clear();

   private:
    friend class AtomicNotificationQueue::AtomicQueue;

    Queue(Node* head, ssize_t size);
    static Queue fromReversed(Node* tail);

    Node* head_{nullptr};
    ssize_t size_{0};
  };

  /**
   * Lock-free queue implementation.
   * The queue can be in 3 states:
   *   1) Empty
   *   2) Armed
   *   3) Non-empty (1 or more tasks in it)
   *
   * This diagram shows possible state transitions:
   *
   * +---------+         successful arm          +-------------+
   * |         |  +---------- arm() ---------->  |             |
   * |  Empty  |                                 |    Armed    | +-+
   * |         |  <------- getTasks() --------+  |             |   |
   * +-+--+----+         consumer disarm         +-------------+   |
   *   |  ^                                                        |
   *   |  |                                                        |
   *   |  | consumer pull                               armed push v
   *   |  |                                                        |
   *   |  |                 +-------------------+                  |
   *   v  +- getTasks() -+  |                   |                  |
   *   |  |                 |     Non-empty     |  <---- push()----+
   *   |  ^---- arm() ---+  |                   |
   *   |                    +-+--+------------+-+
   *   |                      ^  ^            |
   *   |                      |  |            |
   *   +------- push() -------^  ^-- push() --+
   *                 disarmed push
   *
   * push() can be called in any state. It always transitions the queue into
   * Non-empty:
   *   When Armed - push() returns true
   *   When Empty/Non-empty - push() returns false
   *
   * getTasks() can be called in any state. It always transitions the queue into
   * Empty.
   *
   * arm() can be can't be called if the queue is already in Armed state:
   *   When Empty - arm() returns an empty queue and transitions into Armed
   *   When Non-Empty: equivalent to getTasks()
   *
   */
  class AtomicQueue {
   public:
    AtomicQueue() {}
    ~AtomicQueue();
    AtomicQueue(const AtomicQueue&) = delete;
    AtomicQueue& operator=(const AtomicQueue&) = delete;

    /*
     * Pushes a task into the queue. Returns true iff the queue was armed.
     * Can be called from any thread.
     */
    template <typename T>
    bool push(T&& value);

    /*
     * Returns true if the queue has tasks.
     * Can be called from any thread.
     */
    bool hasTasks() const;

    /*
     * Returns all tasks currently in the queue (in FIFO order). Queue becomes
     * empty.
     * Can be called from consumer thread only.
     */
    Queue getTasks();

    /*
     * Tries to arm the queue.
     * 1) If the queue was empty: the queue becomes armed and an empty queue is
     * returned.
     * 2) If the queue wasn't empty: acts as getTasks().
     * Can be called from consumer thread only.
     */
    Queue arm();

    /*
     * Returns how many armed push happened.
     * Can be called from consumer thread only. And only when queue state is
     * Empty.
     */
    ssize_t getArmedPushCount() const {
      DCHECK(!head_) << "AtomicQueue state has to be Empty";
      DCHECK(successfulArmCount_ >= consumerDisarmCount_);
      return successfulArmCount_ - consumerDisarmCount_;
    }

   private:
    alignas(folly::cacheline_align_v) std::atomic<Node*> head_{};
    alignas(folly::cacheline_align_v) ssize_t successfulArmCount_{0};
    ssize_t consumerDisarmCount_{0};
    static constexpr intptr_t kQueueArmedTag = 1;
  };

 public:
  explicit AtomicNotificationQueue(Consumer&& consumer);

  template <
      typename C = Consumer,
      typename = std::enable_if_t<std::is_default_constructible<C>::value>>
  AtomicNotificationQueue() : AtomicNotificationQueue(Consumer()) {}

  ~AtomicNotificationQueue() override;

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
   * Adds a task into the queue.
   * Can be called from any thread.
   */
  template <typename T>
  void putMessage(T&& task);

  /**
   * Adds a task into the queue unless the max queue size is reached.
   * Returns true iff the task was queued.
   * Can be called from any thread.
   */
  template <typename T>
  FOLLY_NODISCARD bool tryPutMessage(T&& task, uint32_t maxSize);

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
   * Executes all tasks until the queue is empty.
   * Can be called from consumer thread only.
   */
  void drain();

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

  /*
   * Write into the signal fd to wake up the consumer thread.
   */
  void notifyFd();

  /*
   * Read all messages from the signal fd.
   */
  void drainFd();

  /*
   * Executes one round of tasks. Returns true iff tasks were processed.
   * Can be called from consumer thread only.
   */
  bool drive();

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

  alignas(folly::cacheline_align_v) std::atomic<ssize_t> pushCount_{0};
  AtomicQueue atomicQueue_;
  Queue queue_;
  std::atomic<ssize_t> taskExecuteCount_{0};
  int32_t maxReadAtOnce_{10};
  int eventfd_{-1};
  int pipeFds_[2]{-1, -1}; // to fallback to on older/non-linux systems
  /*
   * If event is registered with the EventBase, this describes whether
   * edge-triggered flag was set for it. For edge-triggered events we don't
   * need to drain the fd to deactivate them.
   */
  bool edgeTriggeredSet_{false};
  EventBase* evb_{nullptr};
  ssize_t writesObserved_{0};
  ssize_t writesLocal_{0};
  const pid_t pid_;
  Consumer consumer_;
};

/**
 * Consumer::operator() can optionally return AtomicNotificationQueueTaskStatus
 * to indicate if the provided task should be considered consumed or
 * discarded. Discarded tasks are not counted towards maxReadAtOnce_.
 */
enum class AtomicNotificationQueueTaskStatus : bool {
  // The dequeued task was consumed and should be counted as such
  CONSUMED = true,
  // The dequeued task should be discarded and the queue not count it as
  // consumed
  DISCARD = false
};

} // namespace folly

#include <folly/io/async/AtomicNotificationQueue-inl.h>
