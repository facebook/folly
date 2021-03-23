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

#include <type_traits>

#include <folly/io/async/Request.h>
#include <folly/lang/Align.h>

namespace folly {

/**
 * A producer-consumer queue for passing tasks to consumer thread.
 *
 * Users of this class are expected to implement functionality to wakeup
 * consumer thread.
 */
template <typename Task>
class AtomicNotificationQueue {
 public:
  explicit AtomicNotificationQueue();
  ~AtomicNotificationQueue();

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
   * Tries to arm the queue.
   * 1) If the queue was empty: the queue becomes armed and true is returned.
   * 2) Otherwise returns false.
   * Can be called from consumer thread only.
   */
  bool arm();

  /*
   * Executes one round of tasks. Returns true iff tasks were run.
   * Can be called from consumer thread only.
   *
   * `Consumer::operator()` must accept `Task&&` as its first parameter.
   * It may also optionally accept `std::shared_ptr<folly::RequestContext>&&` as
   * its second parameter, in which case it must manage `folly::RequestContext`
   * for the consumed task.
   *
   * Consumer::operator() can optionally return
   * AtomicNotificationQueueTaskStatus to indicate if the provided task should
   * be considered consumed or discarded. Discarded tasks are not counted
   * towards `maxReadAtOnce`.
   */
  template <typename Consumer>
  bool drive(Consumer&& consumer);

  /*
   * Adds a task into the queue.
   * Can be called from any thread.
   * Returns true iff the queue was armed, in which case
   * producers are expected to notify consumer thread.
   */
  template <typename... Args>
  bool push(Args&&... args);

  /*
   * Attempts adding a task into the queue.
   * Can be called from any thread.
   * Similarly to push(), producers are expected to notify
   * consumer iff SUCCESS_AND_ARMED is returned.
   */
  enum class TryPushResult { FAILED_LIMIT_REACHED, SUCCESS, SUCCESS_AND_ARMED };
  TryPushResult tryPush(Task&& task, uint32_t maxSize);

 private:
  struct Node {
    Task task;
    std::shared_ptr<RequestContext> rctx{RequestContext::saveContext()};

   private:
    friend class AtomicNotificationQueue;

    template <typename... Args>
    explicit Node(Args&&... args) : task(std::forward<Args>(args)...) {}

    Node* next{};
  };

  class AtomicQueue;
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
   * arm() can't be called if the queue is already in Armed state:
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
    template <typename... Args>
    bool push(Args&&... args);

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

   private:
    alignas(folly::cacheline_align_v) std::atomic<Node*> head_{};
    static constexpr intptr_t kQueueArmedTag = 1;
  };

 private:
  alignas(folly::cacheline_align_v) std::atomic<ssize_t> pushCount_{0};
  AtomicQueue atomicQueue_;
  Queue queue_;
  std::atomic<ssize_t> taskExecuteCount_{0};
  int32_t maxReadAtOnce_{10};
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
