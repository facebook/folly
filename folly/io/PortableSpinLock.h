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

#include <boost/noncopyable.hpp>

// This is a wrapper SpinLock implementation that works around the
// x64 limitation of the base folly MicroSpinLock. If that is available, this
// simply thinly wraps it. Otherwise, it uses the simplest analog available on
// iOS (or 32-bit Mac) or, failing that, POSIX (on Android et. al.)

#if __x86_64__
#include <folly/SmallLocks.h>

namespace folly { namespace io {

class PortableSpinLock {
 public:
  FOLLY_ALWAYS_INLINE PortableSpinLock() {
    lock_.init();
  }
  FOLLY_ALWAYS_INLINE void lock() const {
    lock_.lock();
  }
  FOLLY_ALWAYS_INLINE void unlock() const {
    lock_.unlock();
  }
  FOLLY_ALWAYS_INLINE bool trylock() const {
    return lock_.try_lock();
  }
 private:
  mutable folly::MicroSpinLock lock_;
};

}}

#elif __APPLE__
#include <libkern/OSAtomic.h>

namespace folly { namespace io {

class PortableSpinLock {
 public:
  FOLLY_ALWAYS_INLINE PortableSpinLock() : lock_(0) {}
  FOLLY_ALWAYS_INLINE void lock() const {
    OSSpinLockLock(&lock_);
  }
  FOLLY_ALWAYS_INLINE void unlock() const {
    OSSpinLockUnlock(&lock_);
  }
  FOLLY_ALWAYS_INLINE bool trylock() const {
    return OSSpinLockTry(&lock_);
  }
 private:
  mutable OSSpinLock lock_;
};

}}

#else
#include <pthread.h>

namespace folly { namespace io {

class PortableSpinLock {
 public:
  FOLLY_ALWAYS_INLINE PortableSpinLock() {
    pthread_mutex_init(&lock_, nullptr);
  }
  void lock() const {
    int rc = pthread_mutex_lock(&lock_);
    CHECK_EQ(0, rc);
  }
  FOLLY_ALWAYS_INLINE void unlock() const {
    int rc = pthread_mutex_unlock(&lock_);
    CHECK_EQ(0, rc);
  }
  FOLLY_ALWAYS_INLINE bool trylock() const {
    int rc = pthread_mutex_trylock(&lock_);
    CHECK_GE(rc, 0);
    return rc == 0;
  }
 private:
  mutable pthread_mutex_t lock_;
};

}}

#endif

namespace folly { namespace io {

class PortableSpinLockGuard : private boost::noncopyable {
 public:
  FOLLY_ALWAYS_INLINE explicit PortableSpinLockGuard(PortableSpinLock& lock) :
    lock_(lock) {
    lock_.lock();
  }
  FOLLY_ALWAYS_INLINE ~PortableSpinLockGuard() {
    lock_.unlock();
  }
 private:
  PortableSpinLock& lock_;
};

}}
