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

#pragma once

#include <folly/detail/SpinLockImpl.h>

namespace folly {

#if __x86_64__
typedef SpinLockMslImpl SpinLock;
#elif __APPLE__
typedef SpinLockAppleImpl SpinLock;
#elif FOLLY_HAVE_PTHREAD_SPINLOCK_T
typedef SpinLockPthreadImpl SpinLock;
#else
typedef SpinLockPthreadMutexImpl SpinLock;
#endif

template <typename LOCK>
class SpinLockGuardImpl : private boost::noncopyable {
 public:
  FOLLY_ALWAYS_INLINE explicit SpinLockGuardImpl(LOCK& lock) :
    lock_(lock) {
    lock_.lock();
  }
  FOLLY_ALWAYS_INLINE ~SpinLockGuardImpl() {
    lock_.unlock();
  }
 private:
  LOCK& lock_;
};

typedef SpinLockGuardImpl<SpinLock> SpinLockGuard;

}
