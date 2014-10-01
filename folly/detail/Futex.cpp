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

#include <folly/detail/Futex.h>
#include <stdint.h>
#include <string.h>
#include <condition_variable>
#include <mutex>
#include <boost/intrusive/list.hpp>
#include <folly/Hash.h>
#include <folly/ScopeGuard.h>

#ifdef __linux__
# include <errno.h>
# include <linux/futex.h>
# include <sys/syscall.h>
#endif

using namespace std::chrono;

namespace folly { namespace detail {

namespace {

////////////////////////////////////////////////////
// native implementation using the futex() syscall

#ifdef __linux__

int nativeFutexWake(void* addr, int count, uint32_t wakeMask) {
  int rv = syscall(SYS_futex,
                   addr, /* addr1 */
                   FUTEX_WAKE_BITSET | FUTEX_PRIVATE_FLAG, /* op */
                   count, /* val */
                   nullptr, /* timeout */
                   nullptr, /* addr2 */
                   wakeMask); /* val3 */

  assert(rv >= 0);

  return rv;
}

template <class Clock>
struct timespec
timeSpecFromTimePoint(time_point<Clock> absTime)
{
  auto duration = absTime.time_since_epoch();
  auto secs = duration_cast<seconds>(duration);
  auto nanos = duration_cast<nanoseconds>(duration - secs);
  struct timespec result = { secs.count(), nanos.count() };
  return result;
}

FutexResult nativeFutexWaitImpl(void* addr,
                                uint32_t expected,
                                time_point<system_clock>* absSystemTime,
                                time_point<steady_clock>* absSteadyTime,
                                uint32_t waitMask) {
  assert(absSystemTime == nullptr || absSteadyTime == nullptr);

  int op = FUTEX_WAIT_BITSET | FUTEX_PRIVATE_FLAG;
  struct timespec ts;
  struct timespec* timeout = nullptr;

  if (absSystemTime != nullptr) {
    op |= FUTEX_CLOCK_REALTIME;
    ts = timeSpecFromTimePoint(*absSystemTime);
    timeout = &ts;
  } else if (absSteadyTime != nullptr) {
    ts = timeSpecFromTimePoint(*absSteadyTime);
    timeout = &ts;
  }

  // Unlike FUTEX_WAIT, FUTEX_WAIT_BITSET requires an absolute timeout
  // value - http://locklessinc.com/articles/futex_cheat_sheet/
  int rv = syscall(SYS_futex,
                   addr, /* addr1 */
                   op, /* op */
                   expected, /* val */
                   timeout, /* timeout */
                   nullptr, /* addr2 */
                   waitMask); /* val3 */

  if (rv == 0) {
    return FutexResult::AWOKEN;
  } else {
    switch(errno) {
      case ETIMEDOUT:
        assert(timeout != nullptr);
        return FutexResult::TIMEDOUT;
      case EINTR:
        return FutexResult::INTERRUPTED;
      case EWOULDBLOCK:
        return FutexResult::VALUE_CHANGED;
      default:
        assert(false);
        // EACCESS, EFAULT, or EINVAL. All of these mean *addr point to
        // invalid memory (or I misunderstand the API).  We can either
        // crash, or return a value that lets the process continue for
        // a bit. We choose the latter. VALUE_CHANGED probably turns the
        // caller into a spin lock.
        return FutexResult::VALUE_CHANGED;
    }
  }
}

#endif // __linux__

///////////////////////////////////////////////////////
// compatibility implementation using standard C++ API

struct EmulatedFutexWaitNode : public boost::intrusive::list_base_hook<> {
  void* const addr_;
  const uint32_t waitMask_;
  bool hasTimeout_;
  bool signaled_;
  std::condition_variable cond_;

  EmulatedFutexWaitNode(void* addr, uint32_t waitMask, bool hasTimeout)
    : addr_(addr)
    , waitMask_(waitMask)
    , hasTimeout_(hasTimeout)
    , signaled_(false)
  {
  }
};

struct EmulatedFutexBucket {
  std::mutex mutex_;
  boost::intrusive::list<EmulatedFutexWaitNode> waiters_;

  static const size_t kNumBuckets = 4096;
  static EmulatedFutexBucket* gBuckets;
  static std::once_flag gBucketInit;

  static EmulatedFutexBucket& bucketFor(void* addr) {
    std::call_once(gBucketInit, [](){
      gBuckets = new EmulatedFutexBucket[kNumBuckets];
    });
    uint64_t mixedBits = folly::hash::twang_mix64(
        reinterpret_cast<uintptr_t>(addr));
    return gBuckets[mixedBits % kNumBuckets];
  }
};

EmulatedFutexBucket* EmulatedFutexBucket::gBuckets;
std::once_flag EmulatedFutexBucket::gBucketInit;

int emulatedFutexWake(void* addr, int count, uint32_t waitMask) {
  auto& bucket = EmulatedFutexBucket::bucketFor(addr);

  int numAwoken = 0;
  boost::intrusive::list<EmulatedFutexWaitNode> deferredWakeups;

  {
    std::unique_lock<std::mutex> lock(bucket.mutex_);

    for (auto iter = bucket.waiters_.begin();
         numAwoken < count && iter != bucket.waiters_.end(); ) {
      auto current = iter;
      auto& node = *iter++;
      if (node.addr_ == addr && (node.waitMask_ & waitMask) != 0) {
        // We unlink, but waiter destroys the node.  We must signal timed
        // waiters under the lock, to avoid a race where we release the lock,
        // the waiter times out and deletes the node, and then we try to
        // signal it.  This problem doesn't exist for unbounded waiters,
        // so for them we optimize their wakeup by releasing the lock first.
        bucket.waiters_.erase(current);
        if (node.hasTimeout_) {
          node.signaled_ = true;
          node.cond_.notify_one();
        } else {
          deferredWakeups.push_back(node);
        }
        ++numAwoken;
      }
    }
  }

  while (!deferredWakeups.empty()) {
    auto& node = deferredWakeups.front();
    deferredWakeups.pop_front();
    node.signaled_ = true;
    node.cond_.notify_one();
  }

  return numAwoken;
}

FutexResult emulatedFutexWaitImpl(
        void* addr,
        uint32_t expected,
        time_point<system_clock>* absSystemTime,
        time_point<steady_clock>* absSteadyTime,
        uint32_t waitMask) {
  bool hasTimeout = absSystemTime != nullptr || absSteadyTime != nullptr;
  EmulatedFutexWaitNode node(addr, waitMask, hasTimeout);

  auto& bucket = EmulatedFutexBucket::bucketFor(addr);
  std::unique_lock<std::mutex> lock(bucket.mutex_);

  uint32_t actual;
  memcpy(&actual, addr, sizeof(uint32_t));
  if (actual != expected) {
    return FutexResult::VALUE_CHANGED;
  }

  bucket.waiters_.push_back(node);
  while (!node.signaled_) {
    std::cv_status status = std::cv_status::no_timeout;
    if (absSystemTime != nullptr) {
      status = node.cond_.wait_until(lock, *absSystemTime);
    } else if (absSteadyTime != nullptr) {
      status = node.cond_.wait_until(lock, *absSteadyTime);
    } else {
      node.cond_.wait(lock);
    }

    if (status == std::cv_status::timeout) {
      bucket.waiters_.erase(bucket.waiters_.iterator_to(node));
      return FutexResult::TIMEDOUT;
    }
  }
  return FutexResult::AWOKEN;
}

} // anon namespace


/////////////////////////////////
// Futex<> specializations

template <>
int
Futex<std::atomic>::futexWake(int count, uint32_t wakeMask) {
#ifdef __linux__
  return nativeFutexWake(this, count, wakeMask);
#else
  return emulatedFutexWake(this, count, wakeMask);
#endif
}

template <>
int
Futex<EmulatedFutexAtomic>::futexWake(int count, uint32_t wakeMask) {
  return emulatedFutexWake(this, count, wakeMask);
}

template <>
FutexResult
Futex<std::atomic>::futexWaitImpl(uint32_t expected,
                                  time_point<system_clock>* absSystemTime,
                                  time_point<steady_clock>* absSteadyTime,
                                  uint32_t waitMask) {
#ifdef __linux__
  return nativeFutexWaitImpl(
      this, expected, absSystemTime, absSteadyTime, waitMask);
#else
  return emulatedFutexWaitImpl(
      this, expected, absSystemTime, absSteadyTime, waitMask);
#endif
}

template <>
FutexResult
Futex<EmulatedFutexAtomic>::futexWaitImpl(
        uint32_t expected,
        time_point<system_clock>* absSystemTime,
        time_point<steady_clock>* absSteadyTime,
        uint32_t waitMask) {
  return emulatedFutexWaitImpl(
      this, expected, absSystemTime, absSteadyTime, waitMask);
}

}} // namespace folly::detail
