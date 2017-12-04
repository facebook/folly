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

#include <folly/experimental/hazptr/hazptr.h>
#include <folly/lang/Launder.h>
#include <folly/synchronization/SaturatingSemaphore.h>

#include <glog/logging.h>

namespace folly {

/* constructor */

template <
    typename T,
    bool SingleProducer,
    bool SingleConsumer,
    bool MayBlock,
    size_t LgSegmentSize,
    template <typename> class Atom>
inline UnboundedQueue<
    T,
    SingleProducer,
    SingleConsumer,
    MayBlock,
    LgSegmentSize,
    Atom>::UnboundedQueue() {
  setProducerTicket(0);
  setConsumerTicket(0);
  auto s = new Segment(0);
  DEBUG_PRINT(s);
  setTail(s);
  setHead(s);
}

/* destructor */

template <
    typename T,
    bool SingleProducer,
    bool SingleConsumer,
    bool MayBlock,
    size_t LgSegmentSize,
    template <typename> class Atom>
inline UnboundedQueue<
    T,
    SingleProducer,
    SingleConsumer,
    MayBlock,
    LgSegmentSize,
    Atom>::~UnboundedQueue() {
  Segment* next;
  for (auto s = head(); s; s = next) {
    next = s->nextSegment();
    if (SPSC) {
      delete s;
    } else {
      s->retire(); // hazptr
    }
  }
}

/* dequeue */

template <
    typename T,
    bool SingleProducer,
    bool SingleConsumer,
    bool MayBlock,
    size_t LgSegmentSize,
    template <typename> class Atom>
FOLLY_ALWAYS_INLINE void UnboundedQueue<
    T,
    SingleProducer,
    SingleConsumer,
    MayBlock,
    LgSegmentSize,
    Atom>::dequeue(T& item) noexcept {
  if (SPSC) {
    auto s = head();
    dequeueCommon(s, item);
  } else {
    // Using hazptr_holder instead of hazptr_local because it is
    // possible to call ~T() and it may happen to use hazard pointers.
    folly::hazptr::hazptr_holder hptr;
    auto s = hptr.get_protected(head_);
    dequeueCommon(s, item);
  }
}

/* try_dequeue_until */

template <
    typename T,
    bool SingleProducer,
    bool SingleConsumer,
    bool MayBlock,
    size_t LgSegmentSize,
    template <typename> class Atom>
template <typename Clock, typename Duration>
FOLLY_ALWAYS_INLINE bool UnboundedQueue<
    T,
    SingleProducer,
    SingleConsumer,
    MayBlock,
    LgSegmentSize,
    Atom>::
    try_dequeue_until(
        T& item,
        const std::chrono::time_point<Clock, Duration>& deadline) noexcept {
  if (SingleConsumer) {
    auto s = head();
    return singleConsumerTryDequeueUntil(s, item, deadline);
  } else {
    // Using hazptr_holder instead of hazptr_local because it is
    // possible to call ~T() and it may happen to use hazard pointers.
    folly::hazptr::hazptr_holder hptr;
    auto s = hptr.get_protected(head_);
    return multiConsumerTryDequeueUntil(s, item, deadline);
  }
}

/* enqueueImpl */

template <
    typename T,
    bool SingleProducer,
    bool SingleConsumer,
    bool MayBlock,
    size_t LgSegmentSize,
    template <typename> class Atom>
template <typename Arg>
FOLLY_ALWAYS_INLINE void UnboundedQueue<
    T,
    SingleProducer,
    SingleConsumer,
    MayBlock,
    LgSegmentSize,
    Atom>::enqueueImpl(Arg&& arg) {
  if (SPSC) {
    auto s = tail();
    enqueueCommon(s, std::forward<Arg>(arg));
  } else {
    // Using hazptr_holder instead of hazptr_local because it is
    // possible that the T construcctor happens to use hazard
    // pointers.
    folly::hazptr::hazptr_holder hptr;
    auto s = hptr.get_protected(tail_);
    enqueueCommon(s, std::forward<Arg>(arg));
  }
}

/* enqueueCommon */

template <
    typename T,
    bool SingleProducer,
    bool SingleConsumer,
    bool MayBlock,
    size_t LgSegmentSize,
    template <typename> class Atom>
template <typename Arg>
FOLLY_ALWAYS_INLINE void UnboundedQueue<
    T,
    SingleProducer,
    SingleConsumer,
    MayBlock,
    LgSegmentSize,
    Atom>::enqueueCommon(Segment* s, Arg&& arg) {
  auto t = fetchIncrementProducerTicket();
  if (!SingleProducer) {
    s = findSegment(s, t);
  }
  DCHECK_GE(t, s->minTicket());
  DCHECK_LT(t, (s->minTicket() + SegmentSize));
  size_t idx = index(t);
  Entry& e = s->entry(idx);
  e.putItem(std::forward<Arg>(arg));
  if (responsibleForAlloc(t)) {
    allocNextSegment(s, t + SegmentSize);
  }
  if (responsibleForAdvance(t)) {
    advanceTail(s);
  }
}

/* dequeueCommon */

template <
    typename T,
    bool SingleProducer,
    bool SingleConsumer,
    bool MayBlock,
    size_t LgSegmentSize,
    template <typename> class Atom>
FOLLY_ALWAYS_INLINE void UnboundedQueue<
    T,
    SingleProducer,
    SingleConsumer,
    MayBlock,
    LgSegmentSize,
    Atom>::dequeueCommon(Segment* s, T& item) noexcept {
  auto t = fetchIncrementConsumerTicket();
  if (!SingleConsumer) {
    s = findSegment(s, t);
  }
  size_t idx = index(t);
  Entry& e = s->entry(idx);
  e.takeItem(item);
  if (responsibleForAdvance(t)) {
    advanceHead(s);
  }
}

/* singleConsumerTryDequeueUntil */

template <
    typename T,
    bool SingleProducer,
    bool SingleConsumer,
    bool MayBlock,
    size_t LgSegmentSize,
    template <typename> class Atom>
template <typename Clock, typename Duration>
FOLLY_ALWAYS_INLINE bool UnboundedQueue<
    T,
    SingleProducer,
    SingleConsumer,
    MayBlock,
    LgSegmentSize,
    Atom>::
    singleConsumerTryDequeueUntil(
        Segment* s,
        T& item,
        const std::chrono::time_point<Clock, Duration>& deadline) noexcept {
  auto t = consumerTicket();
  DCHECK_GE(t, s->minTicket());
  DCHECK_LT(t, (s->minTicket() + SegmentSize));
  size_t idx = index(t);
  Entry& e = s->entry(idx);
  if (!e.tryWaitUntil(deadline)) {
    return false;
  }
  setConsumerTicket(t + 1);
  e.takeItem(item);
  if (responsibleForAdvance(t)) {
    advanceHead(s);
  }
  return true;
}

/* multiConsumerTryDequeueUntil */

template <
    typename T,
    bool SingleProducer,
    bool SingleConsumer,
    bool MayBlock,
    size_t LgSegmentSize,
    template <typename> class Atom>
template <typename Clock, typename Duration>
FOLLY_ALWAYS_INLINE bool UnboundedQueue<
    T,
    SingleProducer,
    SingleConsumer,
    MayBlock,
    LgSegmentSize,
    Atom>::
    multiConsumerTryDequeueUntil(
        Segment* s,
        T& item,
        const std::chrono::time_point<Clock, Duration>& deadline) noexcept {
  while (true) {
    auto t = consumerTicket();
    if (UNLIKELY(t >= (s->minTicket() + SegmentSize))) {
      Segment* next;
      // Note that the following loop will not spin indefinitely (as
      // long as the number of concurrently waiting consumers is not
      // greater than SegmentSize). The algorithm guarantees in such a
      // case that the producer reponsible for setting the next
      // pointer is already running.
      while ((next = s->nextSegment()) == nullptr) {
        if (Clock::now() > deadline) {
          return false;
        }
        asm_volatile_pause();
      }
      s = next;
      DCHECK(s != nullptr);
      continue;
    }
    size_t idx = index(t);
    Entry& e = s->entry(idx);
    if (!e.tryWaitUntil(deadline)) {
      return false;
    }
    if (!consumerTicket_.compare_exchange_weak(
            t, t + 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
      continue;
    }
    e.takeItem(item);
    if (responsibleForAdvance(t)) {
      advanceHead(s);
    }
    return true;
  }
}

/* findSegment */

template <
    typename T,
    bool SingleProducer,
    bool SingleConsumer,
    bool MayBlock,
    size_t LgSegmentSize,
    template <typename> class Atom>
inline typename UnboundedQueue<
    T,
    SingleProducer,
    SingleConsumer,
    MayBlock,
    LgSegmentSize,
    Atom>::Segment*
UnboundedQueue<
    T,
    SingleProducer,
    SingleConsumer,
    MayBlock,
    LgSegmentSize,
    Atom>::findSegment(Segment* s, const Ticket t) const noexcept {
  while (t >= (s->minTicket() + SegmentSize)) {
    Segment* next = s->nextSegment();
    // Note that the following loop will not spin indefinitely. The
    // algorithm guarantees that the producer reponsible for setting
    // the next pointer is already running.
    while (next == nullptr) {
      asm_volatile_pause();
      next = s->nextSegment();
    }
    DCHECK(next != nullptr);
    s = next;
  }
  return s;
}

/* allocNextSegment */

template <
    typename T,
    bool SingleProducer,
    bool SingleConsumer,
    bool MayBlock,
    size_t LgSegmentSize,
    template <typename> class Atom>
inline void UnboundedQueue<
    T,
    SingleProducer,
    SingleConsumer,
    MayBlock,
    LgSegmentSize,
    Atom>::allocNextSegment(Segment* s, const Ticket t) {
  auto next = new Segment(t);
  if (!SPSC) {
    next->acquire_ref_safe(); // hazptr
  }
  DEBUG_PRINT(s << " " << next);
  DCHECK(s->nextSegment() == nullptr);
  s->setNextSegment(next);
}

/* advanceTail */

template <
    typename T,
    bool SingleProducer,
    bool SingleConsumer,
    bool MayBlock,
    size_t LgSegmentSize,
    template <typename> class Atom>
inline void UnboundedQueue<
    T,
    SingleProducer,
    SingleConsumer,
    MayBlock,
    LgSegmentSize,
    Atom>::advanceTail(Segment* s) noexcept {
  Segment* next = s->nextSegment();
  if (!SingleProducer) {
    // Note that the following loop will not spin indefinitely. The
    // algorithm guarantees that the producer reponsible for setting
    // the next pointer is already running.
    while (next == nullptr) {
      asm_volatile_pause();
      next = s->nextSegment();
    }
  }
  DCHECK(next != nullptr);
  DEBUG_PRINT(s << " " << next);
  setTail(next);
}

/* advanceHead */

template <
    typename T,
    bool SingleProducer,
    bool SingleConsumer,
    bool MayBlock,
    size_t LgSegmentSize,
    template <typename> class Atom>
inline void UnboundedQueue<
    T,
    SingleProducer,
    SingleConsumer,
    MayBlock,
    LgSegmentSize,
    Atom>::advanceHead(Segment* s) noexcept {
  // Note that the following loops will not spin indefinitely. The
  // algorithm guarantees that the producers reponsible for advancing
  // the tail pointer and setting the next pointer are already
  // running.
  while (tail() == s) {
    asm_volatile_pause();
  }
  auto next = s->nextSegment();
  while (next == nullptr) {
    next = s->nextSegment();
  }
  DEBUG_PRINT(s << " " << next);
  setHead(next);
  if (SPSC) {
    delete s;
  } else {
    s->retire(); // hazptr
  }
}

/**
 *  Entry
 */

template <
    typename T,
    bool SingleProducer,
    bool SingleConsumer,
    bool MayBlock,
    size_t LgSegmentSize,
    template <typename> class Atom>
class UnboundedQueue<
    T,
    SingleProducer,
    SingleConsumer,
    MayBlock,
    LgSegmentSize,
    Atom>::Entry {
  folly::SaturatingSemaphore<MayBlock, Atom> flag_;
  typename std::aligned_storage<sizeof(T), alignof(T)>::type item_;

 public:
  template <typename Arg>
  FOLLY_ALWAYS_INLINE void putItem(Arg&& arg) {
    new (&item_) T(std::forward<Arg>(arg));
    flag_.post();
  }

  FOLLY_ALWAYS_INLINE void takeItem(T& item) noexcept {
    flag_.wait();
    getItem(item);
  }

  template <typename Clock, typename Duration>
  FOLLY_ALWAYS_INLINE bool tryWaitUntil(
      const std::chrono::time_point<Clock, Duration>& deadline) noexcept {
    return flag_.try_wait_until(deadline);
  }

 private:
  FOLLY_ALWAYS_INLINE void getItem(T& item) noexcept {
    item = std::move(*(folly::launder(itemPtr())));
    destroyItem();
  }

  FOLLY_ALWAYS_INLINE T* itemPtr() noexcept {
    return static_cast<T*>(static_cast<void*>(&item_));
  }

  FOLLY_ALWAYS_INLINE void destroyItem() noexcept {
    itemPtr()->~T();
  }
}; // UnboundedQueue::Entry

/**
 *  Segment
 */

template <
    typename T,
    bool SingleProducer,
    bool SingleConsumer,
    bool MayBlock,
    size_t LgSegmentSize,
    template <typename> class Atom>
class UnboundedQueue<
    T,
    SingleProducer,
    SingleConsumer,
    MayBlock,
    LgSegmentSize,
    Atom>::Segment : public folly::hazptr::hazptr_obj_base_refcounted<Segment> {
  Atom<Segment*> next_;
  const Ticket min_;
  bool marked_; // used for iterative deletion
  FOLLY_ALIGN_TO_AVOID_FALSE_SHARING
  Entry b_[SegmentSize];

 public:
  explicit Segment(const Ticket t) : next_(nullptr), min_(t), marked_(false) {}

  ~Segment() {
    if (!SPSC && !marked_) {
      auto next = nextSegment();
      while (next) {
        if (!next->release_ref()) { // hazptr
          return;
        }
        auto s = next;
        next = s->nextSegment();
        s->marked_ = true;
        delete s;
      }
    }
  }

  Segment* nextSegment() const noexcept {
    return next_.load(std::memory_order_acquire);
  }

  void setNextSegment(Segment* s) noexcept {
    next_.store(s, std::memory_order_release);
  }

  FOLLY_ALWAYS_INLINE Ticket minTicket() const noexcept {
    DCHECK_EQ((min_ & (SegmentSize - 1)), 0);
    return min_;
  }

  FOLLY_ALWAYS_INLINE Entry& entry(size_t index) noexcept {
    return b_[index];
  }
}; // UnboundedQueue::Segment

} // namespace folly
