/*
 * Copyright 2013 Facebook, Inc.
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

#include <algorithm>
#include <atomic>
#include <assert.h>
#include <boost/noncopyable.hpp>
#include <errno.h>
#include <limits>
#include <linux/futex.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <folly/Traits.h>
#include <folly/detail/Futex.h>

namespace folly {

namespace detail {

template<typename T, template<typename> class Atom>
class SingleElementQueue;

} // namespace detail

/// MPMCQueue<T> is a high-performance bounded concurrent queue that
/// supports multiple producers, multiple consumers, and optional blocking.
/// The queue has a fixed capacity, for which all memory will be allocated
/// up front.  The bulk of the work of enqueuing and dequeuing can be
/// performed in parallel.
///
/// The underlying implementation uses a ticket dispenser for the head and
/// the tail, spreading accesses across N single-element queues to produce
/// a queue with capacity N.  The ticket dispensers use atomic increment,
/// which is more robust to contention than a CAS loop.  Each of the
/// single-element queues uses its own CAS to serialize access, with an
/// adaptive spin cutoff.  When spinning fails on a single-element queue
/// it uses futex()'s _BITSET operations to reduce unnecessary wakeups
/// even if multiple waiters are present on an individual queue (such as
/// when the MPMCQueue's capacity is smaller than the number of enqueuers
/// or dequeuers).
///
/// NOEXCEPT INTERACTION: Ticket-based queues separate the assignment
/// of In benchmarks (contained in tao/queues/ConcurrentQueueTests)
/// it handles 1 to 1, 1 to N, N to 1, and N to M thread counts better
/// than any of the alternatives present in fbcode, for both small (~10)
/// and large capacities.  In these benchmarks it is also faster than
/// tbb::concurrent_bounded_queue for all configurations.  When there are
/// many more threads than cores, MPMCQueue is _much_ faster than the tbb
/// queue because it uses futex() to block and unblock waiting threads,
/// rather than spinning with sched_yield.
///
/// queue positions from the actual construction of the in-queue elements,
/// which means that the T constructor used during enqueue must not throw
/// an exception.  This is enforced at compile time using type traits,
/// which requires that T be adorned with accurate noexcept information.
/// If your type does not use noexcept, you will have to wrap it in
/// something that provides the guarantee.  We provide an alternate
/// safe implementation for types that don't use noexcept but that are
/// marked folly::IsRelocatable and boost::has_nothrow_constructor,
/// which is common for folly types.  In particular, if you can declare
/// FOLLY_ASSUME_FBVECTOR_COMPATIBLE then your type can be put in
/// MPMCQueue.
template<typename T,
         template<typename> class Atom = std::atomic,
         typename = typename std::enable_if<
             std::is_nothrow_constructible<T,T&&>::value ||
             folly::IsRelocatable<T>::value>::type>
class MPMCQueue : boost::noncopyable {
 public:
  typedef T value_type;

  explicit MPMCQueue(size_t capacity)
    : capacity_(capacity)
    , slots_(new detail::SingleElementQueue<T,Atom>[capacity +
                                                    2 * kSlotPadding])
    , stride_(computeStride(capacity))
    , pushTicket_(0)
    , popTicket_(0)
    , pushSpinCutoff_(0)
    , popSpinCutoff_(0)
  {
    // ideally this would be a static assert, but g++ doesn't allow it
    assert(alignof(MPMCQueue<T,Atom>) >= kFalseSharingRange);
  }

  /// A default-constructed queue is useful because a usable (non-zero
  /// capacity) queue can be moved onto it or swapped with it
  MPMCQueue() noexcept
    : capacity_(0)
    , slots_(nullptr)
    , stride_(0)
    , pushTicket_(0)
    , popTicket_(0)
    , pushSpinCutoff_(0)
    , popSpinCutoff_(0)
  {}

  /// IMPORTANT: The move constructor is here to make it easier to perform
  /// the initialization phase, it is not safe to use when there are any
  /// concurrent accesses (this is not checked).
  MPMCQueue(MPMCQueue<T,Atom>&& rhs) noexcept
    : capacity_(rhs.capacity_)
    , slots_(rhs.slots_)
    , stride_(rhs.stride_)
    , pushTicket_(rhs.pushTicket_.load(std::memory_order_relaxed))
    , popTicket_(rhs.popTicket_.load(std::memory_order_relaxed))
    , pushSpinCutoff_(rhs.pushSpinCutoff_.load(std::memory_order_relaxed))
    , popSpinCutoff_(rhs.popSpinCutoff_.load(std::memory_order_relaxed))
  {
    // relaxed ops are okay for the previous reads, since rhs queue can't
    // be in concurrent use

    // zero out rhs
    rhs.capacity_ = 0;
    rhs.slots_ = nullptr;
    rhs.stride_ = 0;
    rhs.pushTicket_.store(0, std::memory_order_relaxed);
    rhs.popTicket_.store(0, std::memory_order_relaxed);
    rhs.pushSpinCutoff_.store(0, std::memory_order_relaxed);
    rhs.popSpinCutoff_.store(0, std::memory_order_relaxed);
  }

  /// IMPORTANT: The move operator is here to make it easier to perform
  /// the initialization phase, it is not safe to use when there are any
  /// concurrent accesses (this is not checked).
  MPMCQueue<T,Atom> const& operator= (MPMCQueue<T,Atom>&& rhs) {
    if (this != &rhs) {
      this->~MPMCQueue();
      new (this) MPMCQueue(std::move(rhs));
    }
    return *this;
  }

  /// MPMCQueue can only be safely destroyed when there are no
  /// pending enqueuers or dequeuers (this is not checked).
  ~MPMCQueue() {
    delete[] slots_;
  }

  /// Returns the number of successful reads minus the number of successful
  /// writes.  Waiting blockingRead and blockingWrite calls are included,
  /// so this value can be negative.
  ssize_t size() const noexcept {
    // since both pushes and pops increase monotonically, we can get a
    // consistent snapshot either by bracketing a read of popTicket_ with
    // two reads of pushTicket_ that return the same value, or the other
    // way around.  We maximize our chances by alternately attempting
    // both bracketings.
    uint64_t pushes = pushTicket_.load(std::memory_order_acquire); // A
    uint64_t pops = popTicket_.load(std::memory_order_acquire); // B
    while (true) {
      uint64_t nextPushes = pushTicket_.load(std::memory_order_acquire); // C
      if (pushes == nextPushes) {
        // pushTicket_ didn't change from A (or the previous C) to C,
        // so we can linearize at B (or D)
        return pushes - pops;
      }
      pushes = nextPushes;
      uint64_t nextPops = popTicket_.load(std::memory_order_acquire); // D
      if (pops == nextPops) {
        // popTicket_ didn't chance from B (or the previous D), so we
        // can linearize at C
        return pushes - pops;
      }
      pops = nextPops;
    }
  }

  /// Returns true if there are no items available for dequeue
  bool isEmpty() const noexcept {
    return size() <= 0;
  }

  /// Returns true if there is currently no empty space to enqueue
  bool isFull() const noexcept {
    // careful with signed -> unsigned promotion, since size can be negative
    return size() >= static_cast<ssize_t>(capacity_);
  }

  /// Returns is a guess at size() for contexts that don't need a precise
  /// value, such as stats.
  uint64_t sizeGuess() const noexcept {
    return writeCount() - readCount();
  }

  /// Doesn't change
  size_t capacity() const noexcept {
    return capacity_;
  }

  /// Returns the total number of calls to blockingWrite or successful
  /// calls to write, including those blockingWrite calls that are
  /// currently blocking
  uint64_t writeCount() const noexcept {
    return pushTicket_.load(std::memory_order_acquire);
  }

  /// Returns the total number of calls to blockingRead or successful
  /// calls to read, including those blockingRead calls that are currently
  /// blocking
  uint64_t readCount() const noexcept {
    return popTicket_.load(std::memory_order_acquire);
  }

  /// Enqueues a T constructed from args, blocking until space is
  /// available.  Note that this method signature allows enqueue via
  /// move, if args is a T rvalue, via copy, if args is a T lvalue, or
  /// via emplacement if args is an initializer list that can be passed
  /// to a T constructor.
  template <typename ...Args>
  void blockingWrite(Args&&... args) noexcept {
    enqueueWithTicket(pushTicket_++, std::forward<Args>(args)...);
  }

  /// If an item can be enqueued with no blocking, does so and returns
  /// true, otherwise returns false.  This method is similar to
  /// writeIfNotFull, but if you don't have a specific need for that
  /// method you should use this one.
  ///
  /// One of the common usages of this method is to enqueue via the
  /// move constructor, something like q.write(std::move(x)).  If write
  /// returns false because the queue is full then x has not actually been
  /// consumed, which looks strange.  To understand why it is actually okay
  /// to use x afterward, remember that std::move is just a typecast that
  /// provides an rvalue reference that enables use of a move constructor
  /// or operator.  std::move doesn't actually move anything.  It could
  /// more accurately be called std::rvalue_cast or std::move_permission.
  template <typename ...Args>
  bool write(Args&&... args) noexcept {
    uint64_t ticket;
    if (tryObtainReadyPushTicket(ticket)) {
      // we have pre-validated that the ticket won't block
      enqueueWithTicket(ticket, std::forward<Args>(args)...);
      return true;
    } else {
      return false;
    }
  }

  /// If the queue is not full, enqueues and returns true, otherwise
  /// returns false.  Unlike write this method can be blocked by another
  /// thread, specifically a read that has linearized (been assigned
  /// a ticket) but not yet completed.  If you don't really need this
  /// function you should probably use write.
  ///
  /// MPMCQueue isn't lock-free, so just because a read operation has
  /// linearized (and isFull is false) doesn't mean that space has been
  /// made available for another write.  In this situation write will
  /// return false, but writeIfNotFull will wait for the dequeue to finish.
  /// This method is required if you are composing queues and managing
  /// your own wakeup, because it guarantees that after every successful
  /// write a readIfNotFull will succeed.
  template <typename ...Args>
  bool writeIfNotFull(Args&&... args) noexcept {
    uint64_t ticket;
    if (tryObtainPromisedPushTicket(ticket)) {
      // some other thread is already dequeuing the slot into which we
      // are going to enqueue, but we might have to wait for them to finish
      enqueueWithTicket(ticket, std::forward<Args>(args)...);
      return true;
    } else {
      return false;
    }
  }

  /// Moves a dequeued element onto elem, blocking until an element
  /// is available
  void blockingRead(T& elem) noexcept {
    dequeueWithTicket(popTicket_++, elem);
  }

  /// If an item can be dequeued with no blocking, does so and returns
  /// true, otherwise returns false.
  bool read(T& elem) noexcept {
    uint64_t ticket;
    if (tryObtainReadyPopTicket(ticket)) {
      // the ticket has been pre-validated to not block
      dequeueWithTicket(ticket, elem);
      return true;
    } else {
      return false;
    }
  }

  /// If the queue is not empty, dequeues and returns true, otherwise
  /// returns false.  If the matching write is still in progress then this
  /// method may block waiting for it.  If you don't rely on being able
  /// to dequeue (such as by counting completed write) then you should
  /// prefer read.
  bool readIfNotEmpty(T& elem) noexcept {
    uint64_t ticket;
    if (tryObtainPromisedPopTicket(ticket)) {
      // the matching enqueue already has a ticket, but might not be done
      dequeueWithTicket(ticket, elem);
      return true;
    } else {
      return false;
    }
  }

 private:
  enum {
    /// Once every kAdaptationFreq we will spin longer, to try to estimate
    /// the proper spin backoff
    kAdaptationFreq = 128,

    /// Memory locations on the same cache line are subject to false
    /// sharing, which is very bad for performance
    kFalseSharingRange = 64,

    /// To avoid false sharing in slots_ with neighboring memory
    /// allocations, we pad it with this many SingleElementQueue-s at
    /// each end
    kSlotPadding = 1 +
        (kFalseSharingRange - 1) / sizeof(detail::SingleElementQueue<T,Atom>)
  };

#define FOLLY_ON_NEXT_CACHE_LINE __attribute__((aligned(kFalseSharingRange)))

  /// The maximum number of items in the queue at once
  size_t capacity_ FOLLY_ON_NEXT_CACHE_LINE;

  /// An array of capacity_ SingleElementQueue-s, each of which holds
  /// either 0 or 1 item.  We over-allocate by 2 * kSlotPadding and don't
  /// touch the slots at either end, to avoid false sharing
  detail::SingleElementQueue<T,Atom>* slots_;

  /// The number of slots_ indices that we advance for each ticket, to
  /// avoid false sharing.  Ideally slots_[i] and slots_[i + stride_]
  /// aren't on the same cache line
  int stride_;

  /// Enqueuers get tickets from here
  Atom<uint64_t> pushTicket_ FOLLY_ON_NEXT_CACHE_LINE;

  /// Dequeuers get tickets from here
  Atom<uint64_t> popTicket_ FOLLY_ON_NEXT_CACHE_LINE;

  /// This is how many times we will spin before using FUTEX_WAIT when
  /// the queue is full on enqueue, adaptively computed by occasionally
  /// spinning for longer and smoothing with an exponential moving average
  Atom<int> pushSpinCutoff_ FOLLY_ON_NEXT_CACHE_LINE;

  /// The adaptive spin cutoff when the queue is empty on dequeue
  Atom<int> popSpinCutoff_ FOLLY_ON_NEXT_CACHE_LINE;

  /// Alignment doesn't avoid false sharing at the end of the struct,
  /// so fill out the last cache line
  char padding_[kFalseSharingRange - sizeof(Atom<int>)];

#undef FOLLY_ON_NEXT_CACHE_LINE

  /// We assign tickets in increasing order, but we don't want to
  /// access neighboring elements of slots_ because that will lead to
  /// false sharing (multiple cores accessing the same cache line even
  /// though they aren't accessing the same bytes in that cache line).
  /// To avoid this we advance by stride slots per ticket.
  ///
  /// We need gcd(capacity, stride) to be 1 so that we will use all
  /// of the slots.  We ensure this by only considering prime strides,
  /// which either have no common divisors with capacity or else have
  /// a zero remainder after dividing by capacity.  That is sufficient
  /// to guarantee correctness, but we also want to actually spread the
  /// accesses away from each other to avoid false sharing (consider a
  /// stride of 7 with a capacity of 8).  To that end we try a few taking
  /// care to observe that advancing by -1 is as bad as advancing by 1
  /// when in comes to false sharing.
  ///
  /// The simple way to avoid false sharing would be to pad each
  /// SingleElementQueue, but since we have capacity_ of them that could
  /// waste a lot of space.
  static int computeStride(size_t capacity) noexcept {
    static const int smallPrimes[] = { 2, 3, 5, 7, 11, 13, 17, 19, 23 };

    int bestStride = 1;
    size_t bestSep = 1;
    for (int stride : smallPrimes) {
      if ((stride % capacity) == 0 || (capacity % stride) == 0) {
        continue;
      }
      size_t sep = stride % capacity;
      sep = std::min(sep, capacity - sep);
      if (sep > bestSep) {
        bestStride = stride;
        bestSep = sep;
      }
    }
    return bestStride;
  }

  /// Returns the index into slots_ that should be used when enqueuing or
  /// dequeuing with the specified ticket
  size_t idx(uint64_t ticket) noexcept {
    return ((ticket * stride_) % capacity_) + kSlotPadding;
  }

  /// Maps an enqueue or dequeue ticket to the turn should be used at the
  /// corresponding SingleElementQueue
  uint32_t turn(uint64_t ticket) noexcept {
    return ticket / capacity_;
  }

  /// Tries to obtain a push ticket for which SingleElementQueue::enqueue
  /// won't block.  Returns true on immediate success, false on immediate
  /// failure.
  bool tryObtainReadyPushTicket(uint64_t& rv) noexcept {
    auto ticket = pushTicket_.load(std::memory_order_acquire); // A
    while (true) {
      if (!slots_[idx(ticket)].mayEnqueue(turn(ticket))) {
        // if we call enqueue(ticket, ...) on the SingleElementQueue
        // right now it would block, but this might no longer be the next
        // ticket.  We can increase the chance of tryEnqueue success under
        // contention (without blocking) by rechecking the ticket dispenser
        auto prev = ticket;
        ticket = pushTicket_.load(std::memory_order_acquire); // B
        if (prev == ticket) {
          // mayEnqueue was bracketed by two reads (A or prev B or prev
          // failing CAS to B), so we are definitely unable to enqueue
          return false;
        }
      } else {
        // we will bracket the mayEnqueue check with a read (A or prev B
        // or prev failing CAS) and the following CAS.  If the CAS fails
        // it will effect a load of pushTicket_
        if (pushTicket_.compare_exchange_strong(ticket, ticket + 1)) {
          rv = ticket;
          return true;
        }
      }
    }
  }

  /// Tries to obtain a push ticket which can be satisfied if all
  /// in-progress pops complete.  This function does not block, but
  /// blocking may be required when using the returned ticket if some
  /// other thread's pop is still in progress (ticket has been granted but
  /// pop has not yet completed).
  bool tryObtainPromisedPushTicket(uint64_t& rv) noexcept {
    auto numPushes = pushTicket_.load(std::memory_order_acquire); // A
    while (true) {
      auto numPops = popTicket_.load(std::memory_order_acquire); // B
      // n will be negative if pops are pending
      int64_t n = numPushes - numPops;
      if (n >= static_cast<ssize_t>(capacity_)) {
        // Full, linearize at B.  We don't need to recheck the read we
        // performed at A, because if numPushes was stale at B then the
        // real numPushes value is even worse
        return false;
      }
      if (pushTicket_.compare_exchange_strong(numPushes, numPushes + 1)) {
        rv = numPushes;
        return true;
      }
    }
  }

  /// Tries to obtain a pop ticket for which SingleElementQueue::dequeue
  /// won't block.  Returns true on immediate success, false on immediate
  /// failure.
  bool tryObtainReadyPopTicket(uint64_t& rv) noexcept {
    auto ticket = popTicket_.load(std::memory_order_acquire);
    while (true) {
      if (!slots_[idx(ticket)].mayDequeue(turn(ticket))) {
        auto prev = ticket;
        ticket = popTicket_.load(std::memory_order_acquire);
        if (prev == ticket) {
          return false;
        }
      } else {
        if (popTicket_.compare_exchange_strong(ticket, ticket + 1)) {
          rv = ticket;
          return true;
        }
      }
    }
  }

  /// Similar to tryObtainReadyPopTicket, but returns a pop ticket whose
  /// corresponding push ticket has already been handed out, rather than
  /// returning one whose corresponding push ticket has already been
  /// completed.  This means that there is a possibility that the caller
  /// will block when using the ticket, but it allows the user to rely on
  /// the fact that if enqueue has succeeded, tryObtainPromisedPopTicket
  /// will return true.  The "try" part of this is that we won't have
  /// to block waiting for someone to call enqueue, although we might
  /// have to block waiting for them to finish executing code inside the
  /// MPMCQueue itself.
  bool tryObtainPromisedPopTicket(uint64_t& rv) noexcept {
    auto numPops = popTicket_.load(std::memory_order_acquire); // A
    while (true) {
      auto numPushes = pushTicket_.load(std::memory_order_acquire); // B
      if (numPops >= numPushes) {
        // Empty, or empty with pending pops.  Linearize at B.  We don't
        // need to recheck the read we performed at A, because if numPops
        // is stale then the fresh value is larger and the >= is still true
        return false;
      }
      if (popTicket_.compare_exchange_strong(numPops, numPops + 1)) {
        rv = numPops;
        return true;
      }
    }
  }

  // Given a ticket, constructs an enqueued item using args
  template <typename ...Args>
  void enqueueWithTicket(uint64_t ticket, Args&&... args) noexcept {
    slots_[idx(ticket)].enqueue(turn(ticket),
                                pushSpinCutoff_,
                                (ticket % kAdaptationFreq) == 0,
                                std::forward<Args>(args)...);
  }

  // Given a ticket, dequeues the corresponding element
  void dequeueWithTicket(uint64_t ticket, T& elem) noexcept {
    slots_[idx(ticket)].dequeue(turn(ticket),
                                popSpinCutoff_,
                                (ticket % kAdaptationFreq) == 0,
                                elem);
  }
};


namespace detail {

/// A TurnSequencer allows threads to order their execution according to
/// a monotonically increasing (with wraparound) "turn" value.  The two
/// operations provided are to wait for turn T, and to move to the next
/// turn.  Every thread that is waiting for T must have arrived before
/// that turn is marked completed (for MPMCQueue only one thread waits
/// for any particular turn, so this is trivially true).
///
/// TurnSequencer's state_ holds 26 bits of the current turn (shifted
/// left by 6), along with a 6 bit saturating value that records the
/// maximum waiter minus the current turn.  Wraparound of the turn space
/// is expected and handled.  This allows us to atomically adjust the
/// number of outstanding waiters when we perform a FUTEX_WAKE operation.
/// Compare this strategy to sem_t's separate num_waiters field, which
/// isn't decremented until after the waiting thread gets scheduled,
/// during which time more enqueues might have occurred and made pointless
/// FUTEX_WAKE calls.
///
/// TurnSequencer uses futex() directly.  It is optimized for the
/// case that the highest awaited turn is 32 or less higher than the
/// current turn.  We use the FUTEX_WAIT_BITSET variant, which lets
/// us embed 32 separate wakeup channels in a single futex.  See
/// http://locklessinc.com/articles/futex_cheat_sheet for a description.
///
/// We only need to keep exact track of the delta between the current
/// turn and the maximum waiter for the 32 turns that follow the current
/// one, because waiters at turn t+32 will be awoken at turn t.  At that
/// point they can then adjust the delta using the higher base.  Since we
/// need to encode waiter deltas of 0 to 32 inclusive, we use 6 bits.
/// We actually store waiter deltas up to 63, since that might reduce
/// the number of CAS operations a tiny bit.
///
/// To avoid some futex() calls entirely, TurnSequencer uses an adaptive
/// spin cutoff before waiting.  The overheads (and convergence rate)
/// of separately tracking the spin cutoff for each TurnSequencer would
/// be prohibitive, so the actual storage is passed in as a parameter and
/// updated atomically.  This also lets the caller use different adaptive
/// cutoffs for different operations (read versus write, for example).
/// To avoid contention, the spin cutoff is only updated when requested
/// by the caller.
template <template<typename> class Atom>
struct TurnSequencer {
  explicit TurnSequencer(const uint32_t firstTurn = 0) noexcept
      : state_(encode(firstTurn << kTurnShift, 0))
  {}

  /// Returns true iff a call to waitForTurn(turn, ...) won't block
  bool isTurn(const uint32_t turn) const noexcept {
    auto state = state_.load(std::memory_order_acquire);
    return decodeCurrentSturn(state) == (turn << kTurnShift);
  }

  // Internally we always work with shifted turn values, which makes the
  // truncation and wraparound work correctly.  This leaves us bits at
  // the bottom to store the number of waiters.  We call shifted turns
  // "sturns" inside this class.

  /// Blocks the current thread until turn has arrived.  If
  /// updateSpinCutoff is true then this will spin for up to kMaxSpins tries
  /// before blocking and will adjust spinCutoff based on the results,
  /// otherwise it will spin for at most spinCutoff spins.
  void waitForTurn(const uint32_t turn,
                   Atom<int>& spinCutoff,
                   const bool updateSpinCutoff) noexcept {
    int prevThresh = spinCutoff.load(std::memory_order_relaxed);
    const int effectiveSpinCutoff =
        updateSpinCutoff || prevThresh == 0 ? kMaxSpins : prevThresh;
    int tries;

    const uint32_t sturn = turn << kTurnShift;
    for (tries = 0; ; ++tries) {
      uint32_t state = state_.load(std::memory_order_acquire);
      uint32_t current_sturn = decodeCurrentSturn(state);
      if (current_sturn == sturn) {
        break;
      }

      // wrap-safe version of assert(current_sturn < sturn)
      assert(sturn - current_sturn < std::numeric_limits<uint32_t>::max() / 2);

      // the first effectSpinCutoff tries are spins, after that we will
      // record ourself as a waiter and block with futexWait
      if (tries < effectiveSpinCutoff) {
        asm volatile ("pause");
        continue;
      }

      uint32_t current_max_waiter_delta = decodeMaxWaitersDelta(state);
      uint32_t our_waiter_delta = (sturn - current_sturn) >> kTurnShift;
      uint32_t new_state;
      if (our_waiter_delta <= current_max_waiter_delta) {
        // state already records us as waiters, probably because this
        // isn't our first time around this loop
        new_state = state;
      } else {
        new_state = encode(current_sturn, our_waiter_delta);
        if (state != new_state &&
            !state_.compare_exchange_strong(state, new_state)) {
          continue;
        }
      }
      state_.futexWait(new_state, futexChannel(turn));
    }

    if (updateSpinCutoff || prevThresh == 0) {
      // if we hit kMaxSpins then spinning was pointless, so the right
      // spinCutoff is kMinSpins
      int target;
      if (tries >= kMaxSpins) {
        target = kMinSpins;
      } else {
        // to account for variations, we allow ourself to spin 2*N when
        // we think that N is actually required in order to succeed
        target = std::min(int{kMaxSpins}, std::max(int{kMinSpins}, tries * 2));
      }

      if (prevThresh == 0) {
        // bootstrap
        spinCutoff = target;
      } else {
        // try once, keep moving if CAS fails.  Exponential moving average
        // with alpha of 7/8
        spinCutoff.compare_exchange_weak(
            prevThresh, prevThresh + (target - prevThresh) / 8);
      }
    }
  }

  /// Unblocks a thread running waitForTurn(turn + 1)
  void completeTurn(const uint32_t turn) noexcept {
    uint32_t state = state_.load(std::memory_order_acquire);
    while (true) {
      assert(state == encode(turn << kTurnShift, decodeMaxWaitersDelta(state)));
      uint32_t max_waiter_delta = decodeMaxWaitersDelta(state);
      uint32_t new_state = encode(
              (turn + 1) << kTurnShift,
              max_waiter_delta == 0 ? 0 : max_waiter_delta - 1);
      if (state_.compare_exchange_strong(state, new_state)) {
        if (max_waiter_delta != 0) {
          state_.futexWake(std::numeric_limits<int>::max(),
                           futexChannel(turn + 1));
        }
        break;
      }
      // failing compare_exchange_strong updates first arg to the value
      // that caused the failure, so no need to reread state_
    }
  }

  /// Returns the least-most significant byte of the current uncompleted
  /// turn.  The full 32 bit turn cannot be recovered.
  uint8_t uncompletedTurnLSB() const noexcept {
    return state_.load(std::memory_order_acquire) >> kTurnShift;
  }

 private:
  enum : uint32_t {
    /// kTurnShift counts the bits that are stolen to record the delta
    /// between the current turn and the maximum waiter. It needs to be big
    /// enough to record wait deltas of 0 to 32 inclusive.  Waiters more
    /// than 32 in the future will be woken up 32*n turns early (since
    /// their BITSET will hit) and will adjust the waiter count again.
    /// We go a bit beyond and let the waiter count go up to 63, which
    /// is free and might save us a few CAS
    kTurnShift = 6,
    kWaitersMask = (1 << kTurnShift) - 1,

    /// The minimum spin count that we will adaptively select
    kMinSpins = 20,

    /// The maximum spin count that we will adaptively select, and the
    /// spin count that will be used when probing to get a new data point
    /// for the adaptation
    kMaxSpins = 2000,
  };

  /// This holds both the current turn, and the highest waiting turn,
  /// stored as (current_turn << 6) | min(63, max(waited_turn - current_turn))
  Futex<Atom> state_;

  /// Returns the bitmask to pass futexWait or futexWake when communicating
  /// about the specified turn
  int futexChannel(uint32_t turn) const noexcept {
    return 1 << (turn & 31);
  }

  uint32_t decodeCurrentSturn(uint32_t state) const noexcept {
    return state & ~kWaitersMask;
  }

  uint32_t decodeMaxWaitersDelta(uint32_t state) const noexcept {
    return state & kWaitersMask;
  }

  uint32_t encode(uint32_t currentSturn, uint32_t maxWaiterD) const noexcept {
    return currentSturn | std::min(uint32_t{ kWaitersMask }, maxWaiterD);
  }
};


/// SingleElementQueue implements a blocking queue that holds at most one
/// item, and that requires its users to assign incrementing identifiers
/// (turns) to each enqueue and dequeue operation.  Note that the turns
/// used by SingleElementQueue are doubled inside the TurnSequencer
template <typename T, template <typename> class Atom>
struct SingleElementQueue {

  ~SingleElementQueue() noexcept {
    if ((sequencer_.uncompletedTurnLSB() & 1) == 1) {
      // we are pending a dequeue, so we have a constructed item
      destroyContents();
    }
  }

  /// enqueue using in-place noexcept construction
  template <typename ...Args,
            typename = typename std::enable_if<
                std::is_nothrow_constructible<T,Args...>::value>::type>
  void enqueue(const uint32_t turn,
               Atom<int>& spinCutoff,
               const bool updateSpinCutoff,
               Args&&... args) noexcept {
    sequencer_.waitForTurn(turn * 2, spinCutoff, updateSpinCutoff);
    new (contents_) T(std::forward<Args>(args)...);
    sequencer_.completeTurn(turn * 2);
  }

  /// enqueue using move construction, either real (if
  /// is_nothrow_move_constructible) or simulated using relocation and
  /// default construction (if IsRelocatable and has_nothrow_constructor)
  template <typename = typename std::enable_if<
                (folly::IsRelocatable<T>::value &&
                 boost::has_nothrow_constructor<T>::value) ||
                std::is_nothrow_constructible<T,T&&>::value>::type>
  void enqueue(const uint32_t turn,
               Atom<int>& spinCutoff,
               const bool updateSpinCutoff,
               T&& goner) noexcept {
    if (std::is_nothrow_constructible<T,T&&>::value) {
      // this is preferred
      sequencer_.waitForTurn(turn * 2, spinCutoff, updateSpinCutoff);
      new (contents_) T(std::move(goner));
      sequencer_.completeTurn(turn * 2);
    } else {
      // simulate nothrow move with relocation, followed by default
      // construction to fill the gap we created
      sequencer_.waitForTurn(turn * 2, spinCutoff, updateSpinCutoff);
      memcpy(contents_, &goner, sizeof(T));
      sequencer_.completeTurn(turn * 2);
      new (&goner) T();
    }
  }

  bool mayEnqueue(const uint32_t turn) const noexcept {
    return sequencer_.isTurn(turn * 2);
  }

  void dequeue(uint32_t turn,
               Atom<int>& spinCutoff,
               const bool updateSpinCutoff,
               T& elem) noexcept {
    if (folly::IsRelocatable<T>::value) {
      // this version is preferred, because we do as much work as possible
      // before waiting
      try {
        elem.~T();
      } catch (...) {
        // unlikely, but if we don't complete our turn the queue will die
      }
      sequencer_.waitForTurn(turn * 2 + 1, spinCutoff, updateSpinCutoff);
      memcpy(&elem, contents_, sizeof(T));
      sequencer_.completeTurn(turn * 2 + 1);
    } else {
      // use nothrow move assignment
      sequencer_.waitForTurn(turn * 2 + 1, spinCutoff, updateSpinCutoff);
      elem = std::move(*ptr());
      destroyContents();
      sequencer_.completeTurn(turn * 2 + 1);
    }
  }

  bool mayDequeue(const uint32_t turn) const noexcept {
    return sequencer_.isTurn(turn * 2 + 1);
  }

 private:
  /// Storage for a T constructed with placement new
  char contents_[sizeof(T)] __attribute__((aligned(alignof(T))));

  /// Even turns are pushes, odd turns are pops
  TurnSequencer<Atom> sequencer_;

  T* ptr() noexcept {
    return static_cast<T*>(static_cast<void*>(contents_));
  }

  void destroyContents() noexcept {
    try {
      ptr()->~T();
    } catch (...) {
      // g++ doesn't seem to have std::is_nothrow_destructible yet
    }
#ifndef NDEBUG
    memset(contents_, 'Q', sizeof(T));
#endif
  }
};

} // namespace detail

} // namespace folly
