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

#include <atomic>
#include <limits>
#include <assert.h>
#include <errno.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <boost/noncopyable.hpp>

namespace folly { namespace detail {

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

  /** Wakens up to count waiters where (waitMask & wakeMask) != 0,
   *  returning the number of awoken threads. */
  int futexWake(int count = std::numeric_limits<int>::max(),
                uint32_t wakeMask = -1);
};

template <>
inline bool Futex<std::atomic>::futexWait(uint32_t expected,
                                          uint32_t waitMask) {
  assert(sizeof(*this) == sizeof(int));
  int rv = syscall(SYS_futex,
                   this, /* addr1 */
                   FUTEX_WAIT_BITSET | FUTEX_PRIVATE_FLAG, /* op */
                   expected, /* val */
                   nullptr, /* timeout */
                   nullptr, /* addr2 */
                   waitMask); /* val3 */
  assert(rv == 0 || (errno == EWOULDBLOCK || errno == EINTR));
  return rv == 0;
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

}}
