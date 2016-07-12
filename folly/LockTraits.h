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
 * This module provides a traits class for describing properties about mutex
 * classes.
 *
 * This is a primitive for building higher-level abstractions that can work
 * with a variety of mutex classes.  For instance, this allows
 * folly::Synchronized to support a number of different mutex types.
 */
#pragma once

#include <chrono>
#include <type_traits>

// Android, OSX, and Cygwin don't have timed mutexes
#if defined(ANDROID) || defined(__ANDROID__) || defined(__APPLE__) || \
    defined(__CYGWIN__)
#define FOLLY_LOCK_TRAITS_HAVE_TIMED_MUTEXES 0
#else
#define FOLLY_LOCK_TRAITS_HAVE_TIMED_MUTEXES 1
#endif

namespace folly {
namespace detail {

/**
 * An internal helper class for identifying if a lock type supports
 * lock_shared() and try_lock_for() methods.
 */
template <class Mutex>
class LockTraitsImpl {
 private:
  // Helper functions for implementing the traits using SFINAE
  template <class T>
  static auto timed_lock_test(T*) -> typename std::is_same<
      decltype(std::declval<T>().try_lock_for(std::chrono::milliseconds(0))),
      bool>::type;
  template <class T>
  static std::false_type timed_lock_test(...);

  template <class T>
  static auto lock_shared_test(T*) -> typename std::
      is_same<decltype(std::declval<T>().lock_shared()), void>::type;
  template <class T>
  static std::false_type lock_shared_test(...);

 public:
  static constexpr bool has_timed_lock =
      decltype(timed_lock_test<Mutex>(0))::value;
  static constexpr bool has_lock_shared =
      decltype(lock_shared_test<Mutex>(0))::value;
};

template <class Mutex>
struct LockTraitsUniqueBase {
  /**
   * Acquire the lock exclusively.
   */
  static void lock(Mutex& mutex) {
    mutex.lock();
  }

  /**
   * Release an exclusively-held lock.
   */
  static void unlock(Mutex& mutex) {
    mutex.unlock();
  }
};

template <class Mutex>
struct LockTraitsSharedBase : public LockTraitsUniqueBase<Mutex> {
  /**
   * Acquire the lock in shared (read) mode.
   */
  static void lock_shared(Mutex& mutex) {
    mutex.lock_shared();
  }

  /**
   * Release a lock held in shared mode.
   */
  static void unlock_shared(Mutex& mutex) {
    mutex.unlock_shared();
  }
};

template <class Mutex, bool is_shared, bool is_timed>
struct LockTraitsBase {};

template <class Mutex>
struct LockTraitsBase<Mutex, false, false>
    : public LockTraitsUniqueBase<Mutex> {
  static constexpr bool is_shared = false;
  static constexpr bool is_timed = false;
};

template <class Mutex>
struct LockTraitsBase<Mutex, true, false> : public LockTraitsSharedBase<Mutex> {
  static constexpr bool is_shared = true;
  static constexpr bool is_timed = false;
};

template <class Mutex>
struct LockTraitsBase<Mutex, false, true> : public LockTraitsUniqueBase<Mutex> {
  static constexpr bool is_shared = false;
  static constexpr bool is_timed = true;

  /**
   * Acquire the lock exclusively, with a timeout.
   *
   * Returns true or false indicating if the lock was acquired or not.
   */
  template <class Rep, class Period>
  static bool try_lock_for(
      Mutex& mutex,
      const std::chrono::duration<Rep, Period>& timeout) {
    return mutex.try_lock_for(timeout);
  }
};

template <class Mutex>
struct LockTraitsBase<Mutex, true, true> : public LockTraitsSharedBase<Mutex> {
  static constexpr bool is_shared = true;
  static constexpr bool is_timed = true;

  /**
   * Acquire the lock exclusively, with a timeout.
   *
   * Returns true or false indicating if the lock was acquired or not.
   */
  template <class Rep, class Period>
  static bool try_lock_for(
      Mutex& mutex,
      const std::chrono::duration<Rep, Period>& timeout) {
    return mutex.try_lock_for(timeout);
  }

  /**
   * Acquire the lock in shared (read) mode, with a timeout.
   *
   * Returns true or false indicating if the lock was acquired or not.
   */
  template <class Rep, class Period>
  static bool try_lock_shared_for(
      Mutex& mutex,
      const std::chrono::duration<Rep, Period>& timeout) {
    return mutex.try_lock_shared_for(timeout);
  }
};
} // detail

/**
 * LockTraits describes details about a particular mutex type.
 *
 * The default implementation automatically attempts to detect traits
 * based on the presence of various member functions.
 *
 * You can specialize LockTraits to provide custom behavior for lock
 * classes that do not use the standard method names
 * (lock()/unlock()/lock_shared()/unlock_shared()/try_lock_for())
 *
 *
 * LockTraits contains the following members variables:
 * - static constexpr bool is_shared
 *   True if the lock supports separate shared vs exclusive locking states.
 * - static constexpr bool is_timed
 *   True if the lock supports acquiring the lock with a timeout.
 *
 * The following static methods always exist:
 * - lock(Mutex& mutex)
 * - unlock(Mutex& mutex)
 *
 * The following static methods may exist, depending on is_shared and
 * is_timed:
 * - try_lock_for(Mutex& mutex, <std_chrono_duration> timeout)
 * - lock_shared(Mutex& mutex)
 * - unlock_shared(Mutex& mutex)
 * - try_lock_shared_for(Mutex& mutex, <std_chrono_duration> timeout)
 */
template <class Mutex>
struct LockTraits : public detail::LockTraitsBase<
                        Mutex,
                        detail::LockTraitsImpl<Mutex>::has_lock_shared,
                        detail::LockTraitsImpl<Mutex>::has_timed_lock> {};

/**
 * If the lock is a shared lock, acquire it in shared mode.
 * Otherwise, for plain (exclusive-only) locks, perform a normal acquire.
 */
template <class Mutex>
typename std::enable_if<LockTraits<Mutex>::is_shared>::type
lock_shared_or_unique(Mutex& mutex) {
  LockTraits<Mutex>::lock_shared(mutex);
}
template <class Mutex>
typename std::enable_if<!LockTraits<Mutex>::is_shared>::type
lock_shared_or_unique(Mutex& mutex) {
  LockTraits<Mutex>::lock(mutex);
}

/**
 * If the lock is a shared lock, try to acquire it in shared mode, for up to
 * the given timeout.  Otherwise, for plain (exclusive-only) locks, try to
 * perform a normal acquire.
 *
 * Returns true if the lock was acquired, or false on time out.
 */
template <class Mutex, class Rep, class Period>
typename std::enable_if<LockTraits<Mutex>::is_shared, bool>::type
try_lock_shared_or_unique_for(
    Mutex& mutex,
    const std::chrono::duration<Rep, Period>& timeout) {
  return LockTraits<Mutex>::try_lock_shared_for(mutex, timeout);
}
template <class Mutex, class Rep, class Period>
typename std::enable_if<!LockTraits<Mutex>::is_shared, bool>::type
try_lock_shared_or_unique_for(
    Mutex& mutex,
    const std::chrono::duration<Rep, Period>& timeout) {
  return LockTraits<Mutex>::try_lock_for(mutex, timeout);
}

/**
 * Release a lock acquired with lock_shared_or_unique()
 */
template <class Mutex>
typename std::enable_if<LockTraits<Mutex>::is_shared>::type
unlock_shared_or_unique(Mutex& mutex) {
  LockTraits<Mutex>::unlock_shared(mutex);
}
template <class Mutex>
typename std::enable_if<!LockTraits<Mutex>::is_shared>::type
unlock_shared_or_unique(Mutex& mutex) {
  LockTraits<Mutex>::unlock(mutex);
}

/*
 * Lock policy classes.
 *
 * These can be used as template parameters to provide compile-time
 * selection over the type of lock operation to perform.
 */

/**
 * A lock policy that performs exclusive lock operations.
 */
class LockPolicyExclusive {
 public:
  template <class Mutex>
  static void lock(Mutex& mutex) {
    LockTraits<Mutex>::lock(mutex);
  }
  template <class Mutex, class Rep, class Period>
  static bool try_lock_for(
      Mutex& mutex,
      const std::chrono::duration<Rep, Period>& timeout) {
    return LockTraits<Mutex>::try_lock_for(mutex, timeout);
  }
  template <class Mutex>
  static void unlock(Mutex& mutex) {
    LockTraits<Mutex>::unlock(mutex);
  }
};

/**
 * A lock policy that performs shared lock operations.
 * This policy only works with shared mutex types.
 */
class LockPolicyShared {
 public:
  template <class Mutex>
  static void lock(Mutex& mutex) {
    LockTraits<Mutex>::lock_shared(mutex);
  }
  template <class Mutex, class Rep, class Period>
  static bool try_lock_for(
      Mutex& mutex,
      const std::chrono::duration<Rep, Period>& timeout) {
    return LockTraits<Mutex>::try_lock_shared_for(mutex, timeout);
  }
  template <class Mutex>
  static void unlock(Mutex& mutex) {
    LockTraits<Mutex>::unlock_shared(mutex);
  }
};

/**
 * A lock policy that performs a shared lock operation if a shared mutex type
 * is given, or a normal exclusive lock operation on non-shared mutex types.
 */
class LockPolicyShareable {
 public:
  template <class Mutex>
  static void lock(Mutex& mutex) {
    lock_shared_or_unique(mutex);
  }
  template <class Mutex, class Rep, class Period>
  static bool try_lock_for(
      Mutex& mutex,
      const std::chrono::duration<Rep, Period>& timeout) {
    return try_lock_shared_or_unique_for(mutex, timeout);
  }
  template <class Mutex>
  static void unlock(Mutex& mutex) {
    unlock_shared_or_unique(mutex);
  }
};

} // folly
