/*
 * Copyright 2015 Facebook, Inc.
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

#ifndef FOLLY_BATON_H
#define FOLLY_BATON_H

#include <stdint.h>
#include <atomic>
#include <boost/noncopyable.hpp>
#include <errno.h>
#include <assert.h>

#include <folly/detail/Futex.h>
#include <folly/detail/MemoryIdler.h>

namespace folly {

/// A Baton allows a thread to block once and be awoken: it captures
/// a single handoff.  During its lifecycle (from construction/reset to
/// destruction/reset) a baton must either be post()ed and wait()ed exactly
/// once each, or not at all.
///
/// Baton includes no internal padding, and is only 4 bytes in size.
/// Any alignment or padding to avoid false sharing is up to the user.
///
/// This is basically a stripped-down semaphore that supports only a
/// single call to sem_post and a single call to sem_wait.  The current
/// posix semaphore sem_t isn't too bad, but this provides more a bit more
/// speed, inlining, smaller size, a guarantee that the implementation
/// won't change, and compatibility with DeterministicSchedule.  By having
/// a much more restrictive lifecycle we can also add a bunch of assertions
/// that can help to catch race conditions ahead of time.
template <template<typename> class Atom = std::atomic>
struct Baton : boost::noncopyable {
  Baton() : state_(INIT) {}

  /// It is an error to destroy a Baton on which a thread is currently
  /// wait()ing.  In practice this means that the waiter usually takes
  /// responsibility for destroying the Baton.
  ~Baton() {
    // The docblock for this function says that is can't be called when
    // there is a concurrent waiter.  We assume a strong version of this
    // requirement in which the caller must _know_ that this is true, they
    // are not allowed to be merely lucky.  If two threads are involved,
    // the destroying thread must actually have synchronized with the
    // waiting thread after wait() returned.  To convey causality the the
    // waiting thread must have used release semantics and the destroying
    // thread must have used acquire semantics for that communication,
    // so we are guaranteed to see the post-wait() value of state_,
    // which cannot be WAITING.
    //
    // Note that since we only care about a single memory location,
    // the only two plausible memory orders here are relaxed and seq_cst.
    assert(state_.load(std::memory_order_relaxed) != WAITING);
  }

  /// Equivalent to destroying the Baton and creating a new one.  It is
  /// a bug to call this while there is a waiting thread, so in practice
  /// the waiter will be the one that resets the baton.
  void reset() {
    // See ~Baton for a discussion about why relaxed is okay here
    assert(state_.load(std::memory_order_relaxed) != WAITING);

    // We use a similar argument to justify the use of a relaxed store
    // here.  Since both wait() and post() are required to be called
    // only once per lifetime, no thread can actually call those methods
    // correctly after a reset() unless it synchronizes with the thread
    // that performed the reset().  If a post() or wait() on another thread
    // didn't synchronize, then regardless of what operation we performed
    // here there would be a race on proper use of the Baton's spec
    // (although not on any particular load and store).  Put another way,
    // we don't need to synchronize here because anybody that might rely
    // on such synchronization is required by the baton rules to perform
    // an additional synchronization that has the desired effect anyway.
    //
    // There is actually a similar argument to be made about the
    // constructor, in which the fenceless constructor initialization
    // of state_ is piggybacked on whatever synchronization mechanism
    // distributes knowledge of the Baton's existence
    state_.store(INIT, std::memory_order_relaxed);
  }

  /// Causes wait() to wake up.  For each lifetime of a Baton (where a
  /// lifetime starts at construction or reset() and ends at destruction
  /// or reset()) there can be at most one call to post().  Any thread
  /// may call post().
  ///
  /// Although we could implement a more generic semaphore semantics
  /// without any extra size or CPU overhead, the single-call limitation
  /// allows us to have better assert-ions during debug builds.
  void post() {
    uint32_t before = state_.load(std::memory_order_acquire);

    assert(before == INIT || before == WAITING || before == TIMED_OUT);

    if (before == INIT &&
        state_.compare_exchange_strong(before, EARLY_DELIVERY)) {
      return;
    }

    assert(before == WAITING || before == TIMED_OUT);

    if (before == TIMED_OUT) {
      return;
    }

    assert(before == WAITING);
    state_.store(LATE_DELIVERY, std::memory_order_release);
    state_.futexWake(1);
  }

  /// Waits until post() has been called in the current Baton lifetime.
  /// May be called at most once during a Baton lifetime (construction
  /// |reset until destruction|reset).  If post is called before wait in
  /// the current lifetime then this method returns immediately.
  ///
  /// The restriction that there can be at most one wait() per lifetime
  /// could be relaxed somewhat without any perf or size regressions,
  /// but by making this condition very restrictive we can provide better
  /// checking in debug builds.
  void wait() {
    if (spinWaitForEarlyDelivery()) {
      assert(state_.load(std::memory_order_acquire) == EARLY_DELIVERY);
      return;
    }

    // guess we have to block :(
    uint32_t expected = INIT;
    if (!state_.compare_exchange_strong(expected, WAITING)) {
      // CAS failed, last minute reprieve
      assert(expected == EARLY_DELIVERY);
      return;
    }

    while (true) {
      detail::MemoryIdler::futexWait(state_, WAITING);

      // state_ is the truth even if FUTEX_WAIT reported a matching
      // FUTEX_WAKE, since we aren't using type-stable storage and we
      // don't guarantee reuse.  The scenario goes like this: thread
      // A's last touch of a Baton is a call to wake(), which stores
      // LATE_DELIVERY and gets an unlucky context switch before delivering
      // the corresponding futexWake.  Thread B sees LATE_DELIVERY
      // without consuming a futex event, because it calls futexWait
      // with an expected value of WAITING and hence doesn't go to sleep.
      // B returns, so the Baton's memory is reused and becomes another
      // Baton (or a reuse of this one).  B calls futexWait on the new
      // Baton lifetime, then A wakes up and delivers a spurious futexWake
      // to the same memory location.  B's futexWait will then report a
      // consumed wake event even though state_ is still WAITING.
      //
      // It would be possible to add an extra state_ dance to communicate
      // that the futexWake has been sent so that we can be sure to consume
      // it before returning, but that would be a perf and complexity hit.
      uint32_t s = state_.load(std::memory_order_acquire);
      assert(s == WAITING || s == LATE_DELIVERY);

      if (s == LATE_DELIVERY) {
        return;
      }
      // retry
    }
  }

  /// Similar to wait, but with a timeout. The thread is unblocked if the
  /// timeout expires.
  /// Note: Only a single call to timed_wait/wait is allowed during a baton's
  /// life-cycle (from construction/reset to destruction/reset). In other
  /// words, after timed_wait the caller can't invoke wait/timed_wait/try_wait
  /// again on the same baton without resetting it.
  ///
  /// @param  deadline      Time until which the thread can block
  /// @return               true if the baton was posted to before timeout,
  ///                       false otherwise
  template <typename Clock, typename Duration = typename Clock::duration>
  bool timed_wait(const std::chrono::time_point<Clock,Duration>& deadline) {
    if (spinWaitForEarlyDelivery()) {
      assert(state_.load(std::memory_order_acquire) == EARLY_DELIVERY);
      return true;
    }

    // guess we have to block :(
    uint32_t expected = INIT;
    if (!state_.compare_exchange_strong(expected, WAITING)) {
      // CAS failed, last minute reprieve
      assert(expected == EARLY_DELIVERY);
      return true;
    }

    while (true) {
      auto rv = state_.futexWaitUntil(WAITING, deadline);
      if (rv == folly::detail::FutexResult::TIMEDOUT) {
        state_.store(TIMED_OUT, std::memory_order_release);
        return false;
      }

      uint32_t s = state_.load(std::memory_order_acquire);
      assert(s == WAITING || s == LATE_DELIVERY);
      if (s == LATE_DELIVERY) {
        return true;
      }
    }
  }

  /// Similar to wait, but doesn't block the thread if it hasn't been posted.
  ///
  /// try_wait has the following semantics:
  /// - It is ok to call try_wait any number times on the same baton until
  ///   try_wait reports that the baton has been posted.
  /// - It is ok to call timed_wait or wait on the same baton if try_wait
  ///   reports that baton hasn't been posted.
  /// - If try_wait indicates that the baton has been posted, it is invalid to
  ///   call wait, try_wait or timed_wait on the same baton without resetting
  ///
  /// @return       true if baton has been posted, false othewise
  bool try_wait() {
    auto s = state_.load(std::memory_order_acquire);
    assert(s == INIT || s == EARLY_DELIVERY);
    return s == EARLY_DELIVERY;
  }

 private:
  enum State : uint32_t {
    INIT = 0,
    EARLY_DELIVERY = 1,
    WAITING = 2,
    LATE_DELIVERY = 3,
    TIMED_OUT = 4
  };

  enum {
    // Must be positive.  If multiple threads are actively using a
    // higher-level data structure that uses batons internally, it is
    // likely that the post() and wait() calls happen almost at the same
    // time.  In this state, we lose big 50% of the time if the wait goes
    // to sleep immediately.  On circa-2013 devbox hardware it costs about
    // 7 usec to FUTEX_WAIT and then be awoken (half the t/iter as the
    // posix_sem_pingpong test in BatonTests).  We can improve our chances
    // of EARLY_DELIVERY by spinning for a bit, although we have to balance
    // this against the loss if we end up sleeping any way.  Spins on this
    // hw take about 7 nanos (all but 0.5 nanos is the pause instruction).
    // We give ourself 300 spins, which is about 2 usec of waiting.  As a
    // partial consolation, since we are using the pause instruction we
    // are giving a speed boost to the colocated hyperthread.
    PreBlockAttempts = 300,
  };

  // Spin for "some time" (see discussion on PreBlockAttempts) waiting
  // for a post.
  //
  // @return       true if we received an early delivery during the wait,
  //               false otherwise. If the function returns true then
  //               state_ is guaranteed to be EARLY_DELIVERY
  bool spinWaitForEarlyDelivery() {

    static_assert(PreBlockAttempts > 0,
        "isn't this assert clearer than an uninitialized variable warning?");
    for (int i = 0; i < PreBlockAttempts; ++i) {
      if (try_wait()) {
        // hooray!
        return true;
      }
#if FOLLY_X64
      // The pause instruction is the polite way to spin, but it doesn't
      // actually affect correctness to omit it if we don't have it.
      // Pausing donates the full capabilities of the current core to
      // its other hyperthreads for a dozen cycles or so
      asm volatile ("pause");
#endif
    }

    return false;
  }

  detail::Futex<Atom> state_;
};

} // namespace folly

#endif
