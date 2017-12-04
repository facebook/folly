/*
 * Copyright 2017 Facebook, Inc.
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

#pragma once

#include <atomic>
#include <chrono>
#include <memory>

#include <folly/concurrency/CacheLocality.h>

namespace folly {

/// UnboundedQueue supports a variety of options for unbounded
/// dynamically expanding an shrinking queues, including variations of:
/// - Single vs. multiple producers
/// - Single vs. multiple consumers
/// - Blocking vs. spin-waiting
/// - Non-waiting, timed, and waiting consumer operations.
/// Producer operations never wait or fail (unless out-of-memory).
///
/// Template parameters:
/// - T: element type
/// - SingleProducer: true if there can be only one producer at a
///   time.
/// - SingleConsumer: true if there can be only one consumer at a
///   time.
/// - MayBlock: true if consumers may block, false if they only
///   spins. A performance tuning parameter.
/// - LgSegmentSize (default 8): Log base 2 of number of elements per
///   segment. A performance tuning parameter. See below.
///
/// When to use UnboundedQueue:
/// - If a small bound may lead to deadlock or performance degradation
///   under bursty patterns.
/// - If there is no risk of the queue growing too much.
///
/// When not to use UnboundedQueue:
/// - If there is risk of the queue growing too much and a large bound
///   is acceptable, then use DynamicBoundedQueue.
/// - If the queue must not allocate on enqueue or it must have a
///   small bound, then use fixed-size MPMCQueue or (if non-blocking
///   SPSC) ProducerConsumerQueue.
///
/// Template Aliases:
///   USPSCQueue<T, MayBlock, LgSegmentSize>
///   UMPSCQueue<T, MayBlock, LgSegmentSize>
///   USPMCQueue<T, MayBlock, LgSegmentSize>
///   UMPMCQueue<T, MayBlock, LgSegmentSize>
///
/// Functions:
///   Producer operations never wait or fail (unless OOM)
///     void enqueue(const T&);
///     void enqueue(T&&);
///         Adds an element to the end of the queue.
///
///   Consumer operations:
///     void dequeue(T&);
///         Extracts an element from the front of the queue. Waits
///         until an element is available if needed.
///     bool try_dequeue(T&);
///         Tries to extracts an element from the front of the queue
///         if available. Returns true if successful, false otherwise.
///     bool try_dequeue_until(T&, time_point& deadline);
///         Tries to extracts an element from the front of the queue
///         if available until the specified deadline.  Returns true
///         if successful, false otherwise.
///     bool try_dequeue_for(T&, duration&);
///         Tries to extracts an element from the front of the queue
///         if available for for the specified duration.  Returns true
///         if successful, false otherwise.
///
///   Secondary functions:
///     size_t size();
///         Returns an estimate of the size of the queue.
///     bool empty();
///         Returns true only if the queue was empty during the call.
///     Note: size() and empty() are guaranteed to be accurate only if
///     the queue is not changed concurrently.
///
/// Usage examples:
/// @code
///   /* UMPSC, doesn't block, 1024 int elements per segment */
///   UMPSCQueue<int, false, 10> q;
///   q.enqueue(1);
///   q.enqueue(2);
///   q.enqueue(3);
///   ASSERT_FALSE(q.empty());
///   ASSERT_EQ(q.size(), 3);
///   int v;
///   q.dequeue(v);
///   ASSERT_EQ(v, 1);
///   ASSERT_TRUE(try_dequeue(v));
///   ASSERT_EQ(v, 2);
///   ASSERT_TRUE(try_dequeue_until(v, now() + seconds(1)));
///   ASSERT_EQ(v, 3);
///   ASSERT_TRUE(q.empty());
///   ASSERT_EQ(q.size(), 0);
///   ASSERT_FALSE(try_dequeue(v));
///   ASSERT_FALSE(try_dequeue_for(v, microseconds(100)));
/// @endcode
///
/// Design:
/// - The queue is composed of one or more segments. Each segment has
///   a fixed size of 2^LgSegmentSize entries. Each segment is used
///   exactly once.
/// - Each entry is composed of a futex and a single element.
/// - The queue contains two 64-bit ticket variables. The producer
///   ticket counts the number of producer tickets isued so far, and
///   the same for the consumer ticket. Each ticket number corresponds
///   to a specific entry in a specific segment.
/// - The queue maintains two pointers, head and tail. Head points to
///   the segment that corresponds to the current consumer
///   ticket. Similarly, tail pointer points to the segment that
///   corresponds to the producer ticket.
/// - Segments are organized as a singly linked list.
/// - The producer with the first ticket in the current producer
///   segment is solely responsible for allocating and linking the
///   next segment.
/// - The producer with the last ticket in the current producer
///   segment is solely responsible for advancing the tail pointer to
///   the next segment.
/// - Similarly, the consumer with the last ticket in the current
///   consumer segment is solely responsible for advancing the head
///   pointer to the next segment. It must ensure that head never
///   overtakes tail.
///
/// Memory Usage:
/// - An empty queue contains one segment. A nonempty queue contains
///   one or two more segment than fits its contents.
/// - Removed segments are not reclaimed until there are no threads,
///   producers or consumers, have references to them or their
///   predessors. That is, a lagging thread may delay the reclamation
///   of a chain of removed segments.
///
/// Performance considerations:
/// - All operations take constant time, excluding the costs of
///   allocation, reclamation, interence from other threads, and
///   waiting for actions by other threads.
/// - In general, using the single producer and or single consumer
///   variants yields better performance than the MP and MC
///   alternatives.
/// - SPSC without blocking is the fastest configuration. It doesn't
///   include any read-modify-write atomic operations, full fences, or
///   system calls in the critical path.
/// - MP adds a fetch_add to the critical path of each producer operation.
/// - MC adds a fetch_add or compare_exchange to the critical path of
///   each consumer operation.
/// - The possibility of consumers blocking, even if they never do,
///   adds a compare_exchange to the crtical path of each producer
///   operation.
/// - MPMC, SPMC, MPSC require the use of a deferred reclamation
///   mechanism to guarantee that segments removed from the linked
///   list, i.e., unreachable from the head pointer, are reclaimed
///   only after they are no longer needed by any lagging producers or
///   consumers.
/// - The overheads of segment allocation and reclamation are intended
///   to be mostly out of the critical path of the queue's throughput.
/// - If the template parameter LgSegmentSize is changed, it should be
///   set adequately high to keep the amortized cost of allocation and
///   reclamation low.
/// - Another consideration is that the queue is guaranteed to have
///   enough space for a number of consumers equal to 2^LgSegmentSize
///   for local blocking. Excess waiting consumers spin.
/// - It is recommended to measure perforamnce with different variants
///   when applicable, e.g., UMPMC vs UMPSC. Depending on the use
///   case, sometimes the variant with the higher sequential overhead
///   may yield better results due to, for example, more favorable
///   producer-consumer balance or favorable timining for avoiding
///   costly blocking.

template <
    typename T,
    bool SingleProducer,
    bool SingleConsumer,
    bool MayBlock,
    size_t LgSegmentSize = 8,
    template <typename> class Atom = std::atomic>
class UnboundedQueue {
  using Ticket = uint64_t;
  class Entry;
  class Segment;

  static constexpr bool SPSC = SingleProducer && SingleConsumer;
  static constexpr size_t SegmentSize = 1 << LgSegmentSize;
  static constexpr size_t Stride = SPSC || (LgSegmentSize <= 1) ? 1 : 27;

  static_assert(
      std::is_nothrow_destructible<T>::value,
      "T must be nothrow_destructible");
  static_assert((Stride & 1) == 1, "Stride must be odd");
  static_assert(LgSegmentSize < 32, "LgSegmentSize must be < 32");

  FOLLY_ALIGN_TO_AVOID_FALSE_SHARING
  Atom<Segment*> head_;
  Atom<Ticket> consumerTicket_;
  FOLLY_ALIGN_TO_AVOID_FALSE_SHARING
  Atom<Segment*> tail_;
  Atom<Ticket> producerTicket_;

 public:
  UnboundedQueue();
  ~UnboundedQueue();

  /** enqueue */
  FOLLY_ALWAYS_INLINE void enqueue(const T& arg) {
    enqueueImpl(arg);
  }

  FOLLY_ALWAYS_INLINE void enqueue(T&& arg) {
    enqueueImpl(std::move(arg));
  }

  /** dequeue */
  void dequeue(T& item) noexcept;

  /** try_dequeue */
  bool try_dequeue(T& item) noexcept {
    return try_dequeue_until(
        item, std::chrono::steady_clock::time_point::min());
  }

  /** try_dequeue_until */
  template <typename Clock, typename Duration>
  bool try_dequeue_until(
      T& item,
      const std::chrono::time_point<Clock, Duration>& deadline) noexcept;

  /** try_dequeue_for */
  template <typename Rep, typename Period>
  bool try_dequeue_for(
      T& item,
      const std::chrono::duration<Rep, Period>& duration) noexcept {
    return try_dequeue_until(item, std::chrono::steady_clock::now() + duration);
  }

  /** size */
  size_t size() const noexcept {
    auto p = producerTicket();
    auto c = consumerTicket();
    return p > c ? p - c : 0;
  }

  /** empty */
  bool empty() const noexcept {
    auto c = consumerTicket();
    auto p = producerTicket();
    return p <= c;
  }

 private:
  template <typename Arg>
  void enqueueImpl(Arg&& arg);

  template <typename Arg>
  void enqueueCommon(Segment* s, Arg&& arg);

  void dequeueCommon(Segment* s, T& item) noexcept;

  template <typename Clock, typename Duration>
  bool singleConsumerTryDequeueUntil(
      Segment* s,
      T& item,
      const std::chrono::time_point<Clock, Duration>& deadline) noexcept;

  template <typename Clock, typename Duration>
  bool multiConsumerTryDequeueUntil(
      Segment* s,
      T& item,
      const std::chrono::time_point<Clock, Duration>& deadline) noexcept;

  Segment* findSegment(Segment* s, const Ticket t) const noexcept;

  void allocNextSegment(Segment* s, const Ticket t);

  void advanceTail(Segment* s) noexcept;

  void advanceHead(Segment* s) noexcept;

  FOLLY_ALWAYS_INLINE size_t index(Ticket t) const noexcept {
    return (t * Stride) & (SegmentSize - 1);
  }

  FOLLY_ALWAYS_INLINE bool responsibleForAlloc(Ticket t) const noexcept {
    return (t & (SegmentSize - 1)) == 0;
  }

  FOLLY_ALWAYS_INLINE bool responsibleForAdvance(Ticket t) const noexcept {
    return (t & (SegmentSize - 1)) == (SegmentSize - 1);
  }

  FOLLY_ALWAYS_INLINE Segment* head() const noexcept {
    return head_.load(std::memory_order_acquire);
  }

  FOLLY_ALWAYS_INLINE Segment* tail() const noexcept {
    return tail_.load(std::memory_order_acquire);
  }

  FOLLY_ALWAYS_INLINE Ticket producerTicket() const noexcept {
    return producerTicket_.load(std::memory_order_acquire);
  }

  FOLLY_ALWAYS_INLINE Ticket consumerTicket() const noexcept {
    return consumerTicket_.load(std::memory_order_acquire);
  }

  void setHead(Segment* s) noexcept {
    head_.store(s, std::memory_order_release);
  }

  void setTail(Segment* s) noexcept {
    tail_.store(s, std::memory_order_release);
  }

  FOLLY_ALWAYS_INLINE void setProducerTicket(Ticket t) noexcept {
    producerTicket_.store(t, std::memory_order_release);
  }

  FOLLY_ALWAYS_INLINE void setConsumerTicket(Ticket t) noexcept {
    consumerTicket_.store(t, std::memory_order_release);
  }

  FOLLY_ALWAYS_INLINE Ticket fetchIncrementConsumerTicket() noexcept {
    if (SingleConsumer) {
      auto oldval = consumerTicket();
      setConsumerTicket(oldval + 1);
      return oldval;
    } else { // MC
      return consumerTicket_.fetch_add(1, std::memory_order_acq_rel);
    }
  }

  FOLLY_ALWAYS_INLINE Ticket fetchIncrementProducerTicket() noexcept {
    if (SingleProducer) {
      auto oldval = producerTicket();
      setProducerTicket(oldval + 1);
      return oldval;
    } else { // MP
      return producerTicket_.fetch_add(1, std::memory_order_acq_rel);
    }
  }
}; // UnboundedQueue

/* Aliases */

template <
    typename T,
    bool MayBlock,
    size_t LgSegmentSize = 8,
    template <typename> class Atom = std::atomic>
using USPSCQueue = UnboundedQueue<T, true, true, MayBlock, LgSegmentSize, Atom>;

template <
    typename T,
    bool MayBlock,
    size_t LgSegmentSize = 8,
    template <typename> class Atom = std::atomic>
using UMPSCQueue =
    UnboundedQueue<T, false, true, MayBlock, LgSegmentSize, Atom>;

template <
    typename T,
    bool MayBlock,
    size_t LgSegmentSize = 8,
    template <typename> class Atom = std::atomic>
using USPMCQueue =
    UnboundedQueue<T, true, false, MayBlock, LgSegmentSize, Atom>;

template <
    typename T,
    bool MayBlock,
    size_t LgSegmentSize = 8,
    template <typename> class Atom = std::atomic>
using UMPMCQueue =
    UnboundedQueue<T, false, false, MayBlock, LgSegmentSize, Atom>;

} // namespace folly

#include <folly/concurrency/UnboundedQueue-inl.h>
