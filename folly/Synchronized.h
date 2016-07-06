/*
 * Copyright 2016 Facebook, Inc.
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

/**
 * This module implements a Synchronized abstraction useful in
 * mutex-based concurrency.
 *
 * @author: Andrei Alexandrescu (andrei.alexandrescu@fb.com)
 */

#pragma once

#include <boost/thread.hpp>
#include <folly/LockTraits.h>
#include <folly/LockTraitsBoost.h>
#include <folly/Preprocessor.h>
#include <folly/SharedMutex.h>
#include <folly/Traits.h>
#include <mutex>
#include <type_traits>

namespace folly {

namespace detail {
enum InternalDoNotUse {};
} // namespace detail

/**
 * Synchronized<T> encapsulates an object of type T (a "datum") paired
 * with a mutex. The only way to access the datum is while the mutex
 * is locked, and Synchronized makes it virtually impossible to do
 * otherwise. The code that would access the datum in unsafe ways
 * would look odd and convoluted, thus readily alerting the human
 * reviewer. In contrast, the code that uses Synchronized<T> correctly
 * looks simple and intuitive.
 *
 * The second parameter must be a mutex type.  Any mutex type supported by
 * LockTraits<Mutex> can be used.  By default any class with lock() and
 * unlock() methods will work automatically.  LockTraits can be specialized to
 * teach Locked how to use other custom mutex types.  See the documentation in
 * LockTraits.h for additional details.
 *
 * Supported mutexes that work by default include std::mutex,
 * std::recursive_mutex, std::timed_mutex, std::recursive_timed_mutex,
 * folly::SharedMutex, folly::RWSpinLock, and folly::SpinLock.
 * Include LockTraitsBoost.h to get additional LockTraits specializations to
 * support the following boost mutex types: boost::mutex,
 * boost::recursive_mutex, boost::shared_mutex, boost::timed_mutex, and
 * boost::recursive_timed_mutex.
 */
template <class T, class Mutex = SharedMutex>
struct Synchronized {
  /**
   * Default constructor leaves both members call their own default
   * constructor.
   */
  Synchronized() = default;

 private:
  static constexpr bool nxCopyCtor{
      std::is_nothrow_copy_constructible<T>::value};
  static constexpr bool nxMoveCtor{
      std::is_nothrow_move_constructible<T>::value};

  /**
   * Helper constructors to enable Synchronized for
   * non-default constructible types T.
   * Guards are created in actual public constructors and are alive
   * for the time required to construct the object
   */
  template <typename Guard>
  Synchronized(const Synchronized& rhs,
               const Guard& /*guard*/) noexcept(nxCopyCtor)
      : datum_(rhs.datum_) {}

  template <typename Guard>
  Synchronized(Synchronized&& rhs, const Guard& /*guard*/) noexcept(nxMoveCtor)
      : datum_(std::move(rhs.datum_)) {}

 public:
  /**
   * Copy constructor copies the data (with locking the source and
   * all) but does NOT copy the mutex. Doing so would result in
   * deadlocks.
   */
  Synchronized(const Synchronized& rhs) noexcept(nxCopyCtor)
      : Synchronized(rhs, rhs.operator->()) {}

  /**
   * Move constructor moves the data (with locking the source and all)
   * but does not move the mutex.
   */
  Synchronized(Synchronized&& rhs) noexcept(nxMoveCtor)
      : Synchronized(std::move(rhs), rhs.operator->()) {}

  /**
   * Constructor taking a datum as argument copies it. There is no
   * need to lock the constructing object.
   */
  explicit Synchronized(const T& rhs) noexcept(nxCopyCtor) : datum_(rhs) {}

  /**
   * Constructor taking a datum rvalue as argument moves it. Again,
   * there is no need to lock the constructing object.
   */
  explicit Synchronized(T&& rhs) noexcept(nxMoveCtor)
      : datum_(std::move(rhs)) {}

  /**
   * Lets you construct non-movable types in-place. Use the constexpr
   * instance `construct_in_place` as the first argument.
   */
  template <typename... Args>
  explicit Synchronized(construct_in_place_t, Args&&... args)
      : datum_(std::forward<Args>(args)...) {}

  /**
   * The canonical assignment operator only assigns the data, NOT the
   * mutex. It locks the two objects in ascending order of their
   * addresses.
   */
  Synchronized& operator=(const Synchronized& rhs) {
    if (this == &rhs) {
      // Self-assignment, pass.
    } else if (this < &rhs) {
      auto guard1 = operator->();
      auto guard2 = rhs.operator->();
      datum_ = rhs.datum_;
    } else {
      auto guard1 = rhs.operator->();
      auto guard2 = operator->();
      datum_ = rhs.datum_;
    }
    return *this;
  }

  /**
   * Move assignment operator, only assigns the data, NOT the
   * mutex. It locks the two objects in ascending order of their
   * addresses.
   */
  Synchronized& operator=(Synchronized&& rhs) {
    if (this == &rhs) {
      // Self-assignment, pass.
    } else if (this < &rhs) {
      auto guard1 = operator->();
      auto guard2 = rhs.operator->();
      datum_ = std::move(rhs.datum_);
    } else {
      auto guard1 = rhs.operator->();
      auto guard2 = operator->();
      datum_ = std::move(rhs.datum_);
    }
    return *this;
  }

  /**
   * Lock object, assign datum.
   */
  Synchronized& operator=(const T& rhs) {
    auto guard = operator->();
    datum_ = rhs;
    return *this;
  }

  /**
   * Lock object, move-assign datum.
   */
  Synchronized& operator=(T&& rhs) {
    auto guard = operator->();
    datum_ = std::move(rhs);
    return *this;
  }

  /**
   * A LockedPtr lp keeps a modifiable (i.e. non-const)
   * Synchronized<T> object locked for the duration of lp's
   * existence. Because of this, you get to access the datum's methods
   * directly by using lp->fun().
   */
  struct LockedPtr {
    /**
     * Found no reason to leave this hanging.
     */
    LockedPtr() = delete;

    /**
     * Takes a Synchronized and locks it.
     */
    explicit LockedPtr(Synchronized* parent) : parent_(parent) {
      acquire();
    }

    /**
     * Takes a Synchronized and attempts to lock it for some
     * milliseconds. If not, the LockedPtr will be subsequently null.
     */
    LockedPtr(Synchronized* parent, unsigned int milliseconds) {
      std::chrono::milliseconds chronoMS(milliseconds);
      if (LockTraits<Mutex>::try_lock_for(parent->mutex_, chronoMS)) {
        parent_ = parent;
        return;
      }
      // Could not acquire the resource, pointer is null
      parent_ = nullptr;
    }

    /**
     * This is used ONLY inside SYNCHRONIZED_DUAL. It initializes
     * everything properly, but does not lock the parent because it
     * "knows" someone else will lock it. Please do not use.
     */
    LockedPtr(Synchronized* parent, detail::InternalDoNotUse)
        : parent_(parent) {
    }

    /**
     * Copy ctor adds one lock.
     */
    LockedPtr(const LockedPtr& rhs) : parent_(rhs.parent_) {
      acquire();
    }

    /**
     * Assigning from another LockedPtr results in freeing the former
     * lock and acquiring the new one. The method works with
     * self-assignment (does nothing).
     */
    LockedPtr& operator=(const LockedPtr& rhs) {
      if (parent_ != rhs.parent_) {
        if (parent_) parent_->mutex_.unlock();
        parent_ = rhs.parent_;
        acquire();
      }
      return *this;
    }

    /**
     * Destructor releases.
     */
    ~LockedPtr() {
      if (parent_) {
        LockTraits<Mutex>::unlock(parent_->mutex_);
      }
    }

    /**
     * Safe to access the data. Don't save the obtained pointer by
     * invoking lp.operator->() by hand. Also, if the method returns a
     * handle stored inside the datum, don't use this idiom - use
     * SYNCHRONIZED below.
     */
    T* operator->() {
      return parent_ ? &parent_->datum_ : nullptr;
    }

    /**
     * This class temporarily unlocks a LockedPtr in a scoped
     * manner. It is used inside of the UNSYNCHRONIZED macro.
     */
    struct Unsynchronizer {
      explicit Unsynchronizer(LockedPtr* p) : parent_(p) {
        LockTraits<Mutex>::unlock(parent_->parent_->mutex_);
      }
      Unsynchronizer(const Unsynchronizer&) = delete;
      Unsynchronizer& operator=(const Unsynchronizer&) = delete;
      ~Unsynchronizer() {
        parent_->acquire();
      }
      LockedPtr* operator->() const {
        return parent_;
      }
    private:
      LockedPtr* parent_;
    };
    friend struct Unsynchronizer;
    Unsynchronizer typeHackDoNotUse();

    template <class P1, class P2>
    friend void lockInOrder(P1& p1, P2& p2);

  private:
    void acquire() {
      if (parent_) {
        LockTraits<Mutex>::lock(parent_->mutex_);
      }
    }

    // This is the entire state of LockedPtr.
    Synchronized* parent_;
  };

  /**
   * ConstLockedPtr does exactly what LockedPtr does, but for const
   * Synchronized objects. Of interest is that ConstLockedPtr only
   * uses a read lock, which is faster but more restrictive - you only
   * get to call const methods of the datum.
   *
   * Much of the code between LockedPtr and
   * ConstLockedPtr is identical and could be factor out, but there
   * are enough nagging little differences to not justify the trouble.
   */
  struct ConstLockedPtr {
    ConstLockedPtr() = delete;
    explicit ConstLockedPtr(const Synchronized* parent) : parent_(parent) {
      acquire();
    }
    ConstLockedPtr(const Synchronized* parent, detail::InternalDoNotUse)
        : parent_(parent) {
    }
    ConstLockedPtr(const ConstLockedPtr& rhs) : parent_(rhs.parent_) {
      acquire();
    }
    explicit ConstLockedPtr(const LockedPtr& rhs) : parent_(rhs.parent_) {
      acquire();
    }
    ConstLockedPtr(const Synchronized* parent, unsigned int milliseconds) {
      if (try_lock_shared_or_unique_for(
              parent->mutex_, std::chrono::milliseconds(milliseconds))) {
        parent_ = parent;
        return;
      }
      // Could not acquire the resource, pointer is null
      parent_ = nullptr;
    }

    ConstLockedPtr& operator=(const ConstLockedPtr& rhs) {
      if (parent_ != rhs.parent_) {
        if (parent_) parent_->mutex_.unlock_shared();
        parent_ = rhs.parent_;
        acquire();
      }
    }
    ~ConstLockedPtr() {
      if (parent_) {
        unlock_shared_or_unique(parent_->mutex_);
      }
    }

    const T* operator->() const {
      return parent_ ? &parent_->datum_ : nullptr;
    }

    struct Unsynchronizer {
      explicit Unsynchronizer(ConstLockedPtr* p) : parent_(p) {
        unlock_shared_or_unique(parent_->parent_->mutex_);
      }
      Unsynchronizer(const Unsynchronizer&) = delete;
      Unsynchronizer& operator=(const Unsynchronizer&) = delete;
      ~Unsynchronizer() {
        lock_shared_or_unique(parent_->parent_->mutex_);
      }
      ConstLockedPtr* operator->() const {
        return parent_;
      }
    private:
      ConstLockedPtr* parent_;
    };
    friend struct Unsynchronizer;
    Unsynchronizer typeHackDoNotUse();

    template <class P1, class P2>
    friend void lockInOrder(P1& p1, P2& p2);

  private:
    void acquire() {
      if (parent_) {
        lock_shared_or_unique(parent_->mutex_);
      }
    }

    const Synchronized* parent_;
  };

  /**
   * This accessor offers a LockedPtr. In turn. LockedPtr offers
   * operator-> returning a pointer to T. The operator-> keeps
   * expanding until it reaches a pointer, so syncobj->foo() will lock
   * the object and call foo() against it.
  */
  LockedPtr operator->() {
    return LockedPtr(this);
  }

  /**
   * Same, for constant objects. You will be able to invoke only const
   * methods.
   */
  ConstLockedPtr operator->() const {
    return ConstLockedPtr(this);
  }

  /**
   * Attempts to acquire for a given number of milliseconds. If
   * acquisition is unsuccessful, the returned LockedPtr is NULL.
   */
  LockedPtr timedAcquire(unsigned int milliseconds) {
    return LockedPtr(this, milliseconds);
  }

  /**
   * As above, for a constant object.
   */
  ConstLockedPtr timedAcquire(unsigned int milliseconds) const {
    return ConstLockedPtr(this, milliseconds);
  }

  /**
   * Used by SYNCHRONIZED_DUAL.
   */
  LockedPtr internalDoNotUse() {
    return LockedPtr(this, detail::InternalDoNotUse());
  }

  /**
   * ditto
   */
  ConstLockedPtr internalDoNotUse() const {
    return ConstLockedPtr(this, detail::InternalDoNotUse());
  }

  /**
   * Sometimes, although you have a mutable object, you only want to
   * call a const method against it. The most efficient way to achieve
   * that is by using a read lock. You get to do so by using
   * obj.asConst()->method() instead of obj->method().
   */
  const Synchronized& asConst() const {
    return *this;
  }

  /**
   * Swaps with another Synchronized. Protected against
   * self-swap. Only data is swapped. Locks are acquired in increasing
   * address order.
   */
  void swap(Synchronized& rhs) {
    if (this == &rhs) {
      return;
    }
    if (this > &rhs) {
      return rhs.swap(*this);
    }
    auto guard1 = operator->();
    auto guard2 = rhs.operator->();

    using std::swap;
    swap(datum_, rhs.datum_);
  }

  /**
   * Swap with another datum. Recommended because it keeps the mutex
   * held only briefly.
   */
  void swap(T& rhs) {
    LockedPtr guard = operator->();

    using std::swap;
    swap(datum_, rhs);
  }

  /**
   * Copies datum to a given target.
   */
  void copy(T* target) const {
    ConstLockedPtr guard = operator->();
    *target = datum_;
  }

  /**
   * Returns a fresh copy of the datum.
   */
  T copy() const {
    ConstLockedPtr guard = operator->();
    return datum_;
  }

private:
  T datum_;
  mutable Mutex mutex_;
};

// Non-member swap primitive
template <class T, class M>
void swap(Synchronized<T, M>& lhs, Synchronized<T, M>& rhs) {
  lhs.swap(rhs);
}

/**
 * SYNCHRONIZED is the main facility that makes Synchronized<T>
 * helpful. It is a pseudo-statement that introduces a scope where the
 * object is locked. Inside that scope you get to access the unadorned
 * datum.
 *
 * Example:
 *
 * Synchronized<vector<int>> svector;
 * ...
 * SYNCHRONIZED (svector) { ... use svector as a vector<int> ... }
 * or
 * SYNCHRONIZED (v, svector) { ... use v as a vector<int> ... }
 *
 * Refer to folly/docs/Synchronized.md for a detailed explanation and more
 * examples.
 */
#define SYNCHRONIZED(...)                                             \
  FOLLY_PUSH_WARNING                                                  \
  FOLLY_GCC_DISABLE_WARNING(shadow)                                   \
  if (bool SYNCHRONIZED_state = false) {                              \
  } else                                                              \
    for (auto SYNCHRONIZED_lockedPtr =                                \
             (FB_VA_GLUE(FB_ARG_2_OR_1, (__VA_ARGS__))).operator->(); \
         !SYNCHRONIZED_state;                                         \
         SYNCHRONIZED_state = true)                                   \
      for (auto& FB_VA_GLUE(FB_ARG_1, (__VA_ARGS__)) =                \
               *SYNCHRONIZED_lockedPtr.operator->();                  \
           !SYNCHRONIZED_state;                                       \
           SYNCHRONIZED_state = true)                                 \
  FOLLY_POP_WARNING

#define TIMED_SYNCHRONIZED(timeout, ...)                                       \
  if (bool SYNCHRONIZED_state = false) {                                       \
  } else                                                                       \
    for (auto SYNCHRONIZED_lockedPtr =                                         \
             (FB_VA_GLUE(FB_ARG_2_OR_1, (__VA_ARGS__))).timedAcquire(timeout); \
         !SYNCHRONIZED_state;                                                  \
         SYNCHRONIZED_state = true)                                            \
      for (auto FB_VA_GLUE(FB_ARG_1, (__VA_ARGS__)) =                          \
               SYNCHRONIZED_lockedPtr.operator->();                            \
           !SYNCHRONIZED_state;                                                \
           SYNCHRONIZED_state = true)

/**
 * Similar to SYNCHRONIZED, but only uses a read lock.
 */
#define SYNCHRONIZED_CONST(...)            \
  SYNCHRONIZED(                            \
      FB_VA_GLUE(FB_ARG_1, (__VA_ARGS__)), \
      (FB_VA_GLUE(FB_ARG_2_OR_1, (__VA_ARGS__))).asConst())

/**
 * Similar to TIMED_SYNCHRONIZED, but only uses a read lock.
 */
#define TIMED_SYNCHRONIZED_CONST(timeout, ...) \
  TIMED_SYNCHRONIZED(                          \
      timeout,                                 \
      FB_VA_GLUE(FB_ARG_1, (__VA_ARGS__)),     \
      (FB_VA_GLUE(FB_ARG_2_OR_1, (__VA_ARGS__))).asConst())

/**
 * Temporarily disables synchronization inside a SYNCHRONIZED block.
 */
#define UNSYNCHRONIZED(name)                                    \
  for (decltype(SYNCHRONIZED_lockedPtr.typeHackDoNotUse())      \
         SYNCHRONIZED_state3(&SYNCHRONIZED_lockedPtr);          \
       !SYNCHRONIZED_state; SYNCHRONIZED_state = true)          \
    for (auto& name = *SYNCHRONIZED_state3.operator->();        \
         !SYNCHRONIZED_state; SYNCHRONIZED_state = true)

/**
 * Locks two objects in increasing order of their addresses.
 */
template <class P1, class P2>
void lockInOrder(P1& p1, P2& p2) {
  if (static_cast<const void*>(p1.operator->()) >
      static_cast<const void*>(p2.operator->())) {
    p2.acquire();
    p1.acquire();
  } else {
    p1.acquire();
    p2.acquire();
  }
}

/**
 * Synchronizes two Synchronized objects (they may encapsulate
 * different data). Synchronization is done in increasing address of
 * object order, so there is no deadlock risk.
 */
#define SYNCHRONIZED_DUAL(n1, e1, n2, e2)                       \
  if (bool SYNCHRONIZED_state = false) {} else                  \
    for (auto SYNCHRONIZED_lp1 = (e1).internalDoNotUse();       \
         !SYNCHRONIZED_state; SYNCHRONIZED_state = true)        \
      for (auto& n1 = *SYNCHRONIZED_lp1.operator->();           \
           !SYNCHRONIZED_state;  SYNCHRONIZED_state = true)     \
        for (auto SYNCHRONIZED_lp2 = (e2).internalDoNotUse();   \
             !SYNCHRONIZED_state;  SYNCHRONIZED_state = true)   \
          for (auto& n2 = *SYNCHRONIZED_lp2.operator->();       \
               !SYNCHRONIZED_state; SYNCHRONIZED_state = true)  \
            if ((::folly::lockInOrder(                          \
                   SYNCHRONIZED_lp1, SYNCHRONIZED_lp2),         \
                 false)) {}                                     \
            else

} /* namespace folly */
