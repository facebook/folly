/*
 * Copyright 2014 Facebook, Inc.
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
#include <limits>
#include <assert.h>
#include <unistd.h>
#include <boost/noncopyable.hpp>

namespace folly { namespace detail {

enum class FutexResult {
  VALUE_CHANGED, /* Futex value didn't match expected */
  AWOKEN,        /* futex wait matched with a futex wake */
  INTERRUPTED,   /* Spurious wake-up or signal caused futex wait failure */
  TIMEDOUT
};

/**
 * Futex is an atomic 32 bit unsigned integer that provides access to the
 * futex() syscall on that value.  It is templated in such a way that it
 * can interact properly with DeterministicSchedule testing.
 *
 * If you don't know how to use futex(), you probably shouldn't be using
 * this class.  Even if you do know how, you should have a good reason
 * (and benchmarks to back you up).
 */
template <template <typename> class Atom = std::atomic>
struct Futex : Atom<uint32_t>, boost::noncopyable {

  explicit Futex(uint32_t init = 0) : Atom<uint32_t>(init) {}

  /** Puts the thread to sleep if this->load() == expected.  Returns true when
   *  it is returning because it has consumed a wake() event, false for any
   *  other return (signal, this->load() != expected, or spurious wakeup). */
  bool futexWait(uint32_t expected, uint32_t waitMask = -1) {
    auto rv = futexWaitImpl(expected, nullptr, nullptr, waitMask);
    assert(rv != FutexResult::TIMEDOUT);
    return rv == FutexResult::AWOKEN;
  }

  /** Similar to futexWait but also accepts a timeout that gives the time until
   *  when the call can block (time is the absolute time i.e time since epoch).
   *  Allowed clock types: std::chrono::system_clock, std::chrono::steady_clock.
   *  Returns one of FutexResult values.
   *
   *  NOTE: On some systems steady_clock is just an alias for system_clock,
   *  and is not actually steady.*/
  template <class Clock, class Duration = typename Clock::duration>
  FutexResult futexWaitUntil(
          uint32_t expected,
          const std::chrono::time_point<Clock, Duration>& absTime,
          uint32_t waitMask = -1) {
    using std::chrono::duration_cast;
    using std::chrono::nanoseconds;
    using std::chrono::seconds;
    using std::chrono::steady_clock;
    using std::chrono::system_clock;
    using std::chrono::time_point;

    static_assert(
        (std::is_same<Clock, system_clock>::value ||
         std::is_same<Clock, steady_clock>::value),
        "futexWaitUntil only knows std::chrono::{system_clock,steady_clock}");
    assert((std::is_same<Clock, system_clock>::value) || Clock::is_steady);

    auto duration = absTime.time_since_epoch();
    if (std::is_same<Clock, system_clock>::value) {
      time_point<system_clock> absSystemTime(duration);
      return futexWaitImpl(expected, &absSystemTime, nullptr, waitMask);
    } else {
      time_point<steady_clock> absSteadyTime(duration);
      return futexWaitImpl(expected, nullptr, &absSteadyTime, waitMask);
    }
  }

  /** Wakens up to count waiters where (waitMask & wakeMask) != 0,
   *  returning the number of awoken threads. */
  int futexWake(int count = std::numeric_limits<int>::max(),
                uint32_t wakeMask = -1);

 private:

  /** Underlying implementation of futexWait and futexWaitUntil.
   *  At most one of absSystemTime and absSteadyTime should be non-null.
   *  Timeouts are separated into separate parameters to allow the
   *  implementations to be elsewhere without templating on the clock
   *  type, which is otherwise complicated by the fact that steady_clock
   *  is the same as system_clock on some platforms. */
  FutexResult futexWaitImpl(
      uint32_t expected,
      std::chrono::time_point<std::chrono::system_clock>* absSystemTime,
      std::chrono::time_point<std::chrono::steady_clock>* absSteadyTime,
      uint32_t waitMask);
};

/** A std::atomic subclass that can be used to force Futex to emulate
 *  the underlying futex() syscall.  This is primarily useful to test or
 *  benchmark the emulated implementation on systems that don't need it. */
template <typename T>
struct EmulatedFutexAtomic : public std::atomic<T> {
  EmulatedFutexAtomic() noexcept = default;
  constexpr /* implicit */ EmulatedFutexAtomic(T init) noexcept
      : std::atomic<T>(init) {}
  EmulatedFutexAtomic(const EmulatedFutexAtomic& rhs) = delete;
};

/* Available specializations, with definitions elsewhere */

template<>
int Futex<std::atomic>::futexWake(int count, uint32_t wakeMask);

template<>
FutexResult Futex<std::atomic>::futexWaitImpl(
      uint32_t expected,
      std::chrono::time_point<std::chrono::system_clock>* absSystemTime,
      std::chrono::time_point<std::chrono::steady_clock>* absSteadyTime,
      uint32_t waitMask);

template<>
int Futex<EmulatedFutexAtomic>::futexWake(int count, uint32_t wakeMask);

template<>
FutexResult Futex<EmulatedFutexAtomic>::futexWaitImpl(
      uint32_t expected,
      std::chrono::time_point<std::chrono::system_clock>* absSystemTime,
      std::chrono::time_point<std::chrono::steady_clock>* absSteadyTime,
      uint32_t waitMask);

}}
