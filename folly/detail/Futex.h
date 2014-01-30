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
#include <errno.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <boost/noncopyable.hpp>

using std::chrono::steady_clock;
using std::chrono::system_clock;
using std::chrono::time_point;

namespace folly { namespace detail {

enum class FutexResult {
  VALUE_CHANGED, /* Futex value didn't match expected */
  AWOKEN,        /* futex wait matched with a futex wake */
  INTERRUPTED,   /* Spurious wake-up or signal caused futex wait failure */
  TIMEDOUT
};

/* Converts return value and errno from a futex syscall to a FutexResult */
FutexResult futexErrnoToFutexResult(int returnVal, int futexErrno);

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
  bool futexWait(uint32_t expected, uint32_t waitMask = -1);

  /** Similar to futexWait but also accepts a timeout that gives the time until
   *  when the call can block (time is the absolute time i.e time since epoch).
   *  Allowed clock types: std::chrono::system_clock, std::chrono::steady_clock.
   *  Returns one of FutexResult values.
   *
   *  NOTE: On some systems steady_clock is just an alias for system_clock,
   *  and is not actually steady.*/
  template <class Clock, class Duration = typename Clock::duration>
  FutexResult futexWaitUntil(uint32_t expected,
                             const time_point<Clock, Duration>& absTime,
                             uint32_t waitMask = -1);

  /** Wakens up to count waiters where (waitMask & wakeMask) != 0,
   *  returning the number of awoken threads. */
  int futexWake(int count = std::numeric_limits<int>::max(),
                uint32_t wakeMask = -1);

  private:

  /** Futex wait implemented via syscall SYS_futex. absTimeout gives
   *  time till when the wait can block. If it is nullptr the call will
   *  block until a matching futex wake is received. extraOpFlags can be
   *  used to specify addtional flags to add to the futex operation (by
   *  default only FUTEX_WAIT_BITSET and FUTEX_PRIVATE_FLAG are included).
   *  Returns 0 on success or -1 on error, with errno set to one of the
   *  values listed in futex(2). */
  int futexWaitImpl(uint32_t expected,
                    const struct timespec* absTimeout,
                    int extraOpFlags,
                    uint32_t waitMask);
};

template <>
inline int
Futex<std::atomic>::futexWaitImpl(uint32_t expected,
                                  const struct timespec* absTimeout,
                                  int extraOpFlags,
                                  uint32_t waitMask) {
  assert(sizeof(*this) == sizeof(int));

  /* Unlike FUTEX_WAIT, FUTEX_WAIT_BITSET requires an absolute timeout
   * value - http://locklessinc.com/articles/futex_cheat_sheet/ */
  int rv = syscall(
      SYS_futex,
      this, /* addr1 */
      FUTEX_WAIT_BITSET | FUTEX_PRIVATE_FLAG | extraOpFlags, /* op */
      expected, /* val */
      absTimeout, /* timeout */
      nullptr, /* addr2 */
      waitMask); /* val3 */

  assert(rv == 0 ||
         errno == EWOULDBLOCK ||
         errno == EINTR ||
         (absTimeout != nullptr && errno == ETIMEDOUT));

  return rv;
}

template <>
inline bool Futex<std::atomic>::futexWait(uint32_t expected,
                                          uint32_t waitMask) {
  return futexWaitImpl(expected, nullptr, 0 /* extraOpFlags */, waitMask) == 0;
}

template <>
inline int Futex<std::atomic>::futexWake(int count, uint32_t wakeMask) {
  assert(sizeof(*this) == sizeof(int));
  int rv = syscall(SYS_futex,
                   this, /* addr1 */
                   FUTEX_WAKE_BITSET | FUTEX_PRIVATE_FLAG, /* op */
                   count, /* val */
                   nullptr, /* timeout */
                   nullptr, /* addr2 */
                   wakeMask); /* val3 */
  assert(rv >= 0);
  return rv;
}

/* Convert std::chrono::time_point to struct timespec */
template <class Clock, class Duration = typename Clock::Duration>
struct timespec timePointToTimeSpec(const time_point<Clock, Duration>& tp) {
  using std::chrono::nanoseconds;
  using std::chrono::seconds;
  using std::chrono::duration_cast;

  struct timespec ts;
  auto duration = tp.time_since_epoch();
  auto secs = duration_cast<seconds>(duration);
  auto nanos = duration_cast<nanoseconds>(duration - secs);
  ts.tv_sec = secs.count();
  ts.tv_nsec = nanos.count();
  return ts;
}

template <template<typename> class Atom> template<class Clock, class Duration>
inline FutexResult
Futex<Atom>::futexWaitUntil(
               uint32_t expected,
               const time_point<Clock, Duration>& absTime,
               uint32_t waitMask) {

  static_assert(std::is_same<Clock,system_clock>::value ||
                std::is_same<Clock,steady_clock>::value,
                "Only std::system_clock or std::steady_clock supported");

  struct timespec absTimeSpec = timePointToTimeSpec(absTime);
  int extraOpFlags = 0;

  /* We must use FUTEX_CLOCK_REALTIME flag if we are getting the time_point
   * from the system clock (CLOCK_REALTIME). This check also works correctly for
   * broken glibc in which steady_clock is a typedef to system_clock.*/
  if (std::is_same<Clock,system_clock>::value) {
    extraOpFlags = FUTEX_CLOCK_REALTIME;
  } else {
    assert(Clock::is_steady);
  }

  const int rv = futexWaitImpl(expected, &absTimeSpec, extraOpFlags, waitMask);
  return futexErrnoToFutexResult(rv, errno);
}

}}
