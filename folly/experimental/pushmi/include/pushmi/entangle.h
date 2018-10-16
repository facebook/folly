#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "forwards.h"

namespace pushmi {

#if 0

template <class T, class Dual>
struct entangled {
  T t;
  entangled<Dual, T>* dual;

  ~entangled() {
    if (!!dual) {
      dual->dual = nullptr;
    }
  }
  explicit entangled(T t) : t(std::move(t)), dual(nullptr) {}
  entangled(entangled&& o) : t(std::move(o.t)), dual(o.dual) {
    o.dual = nullptr;
    if (!!dual) {
      dual->dual = this;
    }
  }

  entangled() = delete;
  entangled(const entangled&) = delete;
  entangled& operator=(const entangled&) = delete;
  entangled& operator=(entangled&&) = delete;

  Dual* lockPointerToDual() {
    if (!!dual) {
      return std::addressof(dual->t);
    }
    return nullptr;
  }

  void unlockPointerToDual() {
  }
};

#else

// This class can be used to keep a pair of values with pointers to each other
// in sync, even when both objects are allowed to move. Ordinarily you'd do this
// with a heap-allocated, refcounted, control block (or do something else using
// external storage, like a lock table chosen by the current addresses of both
// objects).
// Another thing you could do is have locks, and a backoff strategy for dealing
// with deadlock: lock locally, trylock your dual, if the trylock fails,
// unlock yourself, wait a little while (giving a thread touching the dual a
// chance to acquire the local lock), and repeat. That's kind of ugly.
// This algorithm (which, to be clear, is untested and I haven't really even
// thought through carefully) provides the same guarantees, but without using
// external storage or backoff-based deadlock recovery.

template <class T, class Dual>
struct entangled {
  // must be constructed first so that the other.lockBoth() in the move
  // constructor is run before moving other.t and other.dual
  std::atomic<int> stateMachine;

  T t;
  // In a couple places, we can save on some atomic ops by making this atomic,
  // and adding a "dual == null" fast-path without locking.
  entangled<Dual, T>* dual;

  const static int kUnlocked = 0;
  const static int kLocked = 1;
  const static int kLockedAndLossAcknowledged = 2;

  // Note: *not* thread-safe; it's a bug for two threads to concurrently call
  // lockBoth() on the same entangled (just as it's a bug for two threads to
  // concurrently move from the same object).
  // However, calling lockBoth() on two entangled objects at once is
  // thread-safe.
  // Note also that this may wait indefinitely; it's not the usual non-blocking
  // tryLock().
  bool tryLockBoth() {
    // Try to acquire the local lock. We have to start locally, since local
    // addresses are the only ones we know are safe at first. The rule is, you
    // have to hold *both* locks to write any of either entangled object's
    // metadata, but need only one to read it.
    int expected = kUnlocked;
    if (!stateMachine.compare_exchange_weak(expected, kLocked)) {
      return false;
    }
    // Having *either* object local-locked protects the data in both objects.
    // Once we hold our lock, no control data can change, in either object.
    if (dual == nullptr) {
      return true;
    }
    expected = kUnlocked;
    if (dual->stateMachine.compare_exchange_strong(expected, kLocked)) {
      return true;
    }
    // We got here, and so hit the race; we're deadlocked if we stick to
    // locking. Revert to address-ordering. Note that address-ordering would
    // not be safe on its own, because of the lifetime issues involved; the
    // addresses here are only stable *because* we know both sides are locked,
    // and because of the invariant that you must hold both locks to modify
    // either piece of data.
    if ((uintptr_t)this < (uintptr_t)dual) {
      // I get to win the race. I'll acquire the locks, but have to make sure
      // my memory stays valid until the other thread acknowledges its loss.
      while (stateMachine.load() != kLockedAndLossAcknowledged) {
        // Spin.
      }
      stateMachine.store(kLocked);
      return true;
    } else {
      // I lose the race, but have to coordinate with the winning thread, so
      // that it knows that I'm not about to try to touch it's data
      dual->stateMachine.store(kLockedAndLossAcknowledged);
      return false;
    }
  }

  void lockBoth() {
    while (!tryLockBoth()) {
      // Spin. But, note that all the unbounded spinning in tryLockBoth can be
      // straightforwardly futex-ified. There's a potentialy starvation issue
      // here, but note that it can be dealt with by adding a "priority" bit to
      // the state machine (i.e. if my priority bit is set, the thread for whom
      // I'm the local member of the pair gets to win the race, rather than
      // using address-ordering).
    }
  }

  void unlockBoth() {
    // Note that unlocking locally and then remotely is the right order. There
    // are no concurrent accesses to this object (as an API constraint -- lock
    // and unlock are not thread safe!), and no other thread will touch the
    // other object so long as its locked. Going in the other order could let
    // another thread incorrectly think we're going down the deadlock-avoidance
    // path in tryLock().
    stateMachine.store(kUnlocked);
    if (dual != nullptr) {
      dual->stateMachine.store(kUnlocked);
    }
  }

  entangled() = delete;
  entangled(const entangled&) = delete;
  entangled& operator=(const entangled&) = delete;
  entangled& operator=(entangled&&) = delete;

  explicit entangled(T t)
      : t(std::move(t)), dual(nullptr), stateMachine(kUnlocked) {}
  entangled(entangled&& other)
      : stateMachine((other.lockBoth(), kLocked)),
        t(std::move(other.t)),
        dual(std::move(other.dual)) {
    // Note that, above, we initialized stateMachine to the locked state; the
    // address of this object hasn't escaped yet, and won't (until we unlock
    // the dual), so it doesn't *really* matter, but it's conceptually helpful
    // to maintain that invariant.

    // Update our dual's data.
    if (dual != nullptr) {
      dual->dual = this;
    }

    // Update other's data.
    other.dual = nullptr;
    // unlock other so that its destructor can complete
    other.stateMachine.store(kUnlocked);

    // We locked on other, but will unlock on *this. The locking protocol
    // ensured that no accesses to other will occur after lock() returns, and
    // since then we updated dual's dual to be us.
    unlockBoth();
  }

  ~entangled() {
    lockBoth();
    if (dual != nullptr) {
      dual->dual = nullptr;
    }
    unlockBoth();
  }

  // Must unlock later even if dual is nullptr. This is fixable.
  Dual* lockPointerToDual() {
    lockBoth();
    return !!dual ? std::addressof(dual->t) : nullptr;
  }

  void unlockPointerToDual() {
    unlockBoth();
  }
};
#endif

template <class First, class Second>
auto entangle(First f, Second s)
    -> std::pair<entangled<First, Second>, entangled<Second, First>> {
  entangled<First, Second> ef(std::move(f));
  entangled<Second, First> es(std::move(s));
  ef.dual = std::addressof(es);
  es.dual = std::addressof(ef);
  return {std::move(ef), std::move(es)};
}

} // namespace pushmi
