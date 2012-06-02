/*
 * Copyright 2012 Facebook, Inc.
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

#ifndef FOLLY_SMALLLOCKS_H_
#define FOLLY_SMALLLOCKS_H_

/*
 * This header defines a few very small mutex types.  These are useful
 * in highly memory-constrained environments where contention is
 * unlikely.
 *
 * Note: these locks are for use when you aren't likely to contend on
 * the critical section, or when the critical section is incredibly
 * small.  Given that, both of the locks defined in this header are
 * inherently unfair: that is, the longer a thread is waiting, the
 * longer it waits between attempts to acquire, so newer waiters are
 * more likely to get the mutex.  For the intended use-case this is
 * fine.
 *
 * @author Keith Adams <kma@fb.com>
 * @author Jordan DeLong <delong.j@fb.com>
 */

#include <cinttypes>
#include <type_traits>
#include <ctime>
#include <boost/noncopyable.hpp>
#include <cstdlib>
#include <pthread.h>
#include <mutex>

#include <glog/logging.h>

#ifndef __x86_64__
# error "SmallLocks.h is currently x64-only."
#endif

namespace folly {

//////////////////////////////////////////////////////////////////////

namespace detail {

  /*
   * A helper object for the condended case. Starts off with eager
   * spinning, and falls back to sleeping for small quantums.
   */
  class Sleeper {
    static const int32_t kMaxActiveSpin = 4000;

    uint32_t spinCount;

  public:
    Sleeper() : spinCount(0) {}

    void wait() {
      if (spinCount < kMaxActiveSpin) {
        ++spinCount;
        asm volatile("pause");
      } else {
        /*
         * Always sleep 0.5ms, assuming this will make the kernel put
         * us down for whatever its minimum timer resolution is (in
         * linux this varies by kernel version from 1ms to 10ms).
         */
        struct timespec ts = { 0, 500000 };
        nanosleep(&ts, NULL);
      }
    }
  };

}

//////////////////////////////////////////////////////////////////////

/*
 * A really, *really* small spinlock for fine-grained locking of lots
 * of teeny-tiny data.
 *
 * Zero initializing these is guaranteed to be as good as calling
 * init(), since the free state is guaranteed to be all-bits zero.
 *
 * This class should be kept a POD, so we can used it in other packed
 * structs (gcc does not allow __attribute__((packed)) on structs that
 * contain non-POD data).  This means avoid adding a constructor, or
 * making some members private, etc.
 */
struct MicroSpinLock {
  enum { FREE = 0, LOCKED = 1 };
  uint8_t lock_;

  /*
   * Atomically move lock_ from "compare" to "newval". Return boolean
   * success. Do not play on or around.
   */
  bool cas(uint8_t compare, uint8_t newVal) {
    bool out;
    asm volatile("lock; cmpxchgb %2, (%3);"
                 "setz %0;"
                 : "=r" (out)
                 : "a" (compare), // cmpxchgb constrains this to be in %al
                   "q" (newVal),  // Needs to be byte-accessible
                   "r" (&lock_)
                 : "memory", "flags");
    return out;
  }

  // Initialize this MSL.  It is unnecessary to call this if you
  // zero-initialize the MicroSpinLock.
  void init() {
    lock_ = FREE;
  }

  bool try_lock() {
    return cas(FREE, LOCKED);
  }

  void lock() {
    detail::Sleeper sleeper;
    do {
      while (lock_ != FREE) {
        asm volatile("" : : : "memory");
        sleeper.wait();
      }
    } while (!try_lock());
    DCHECK(lock_ == LOCKED);
  }

  void unlock() {
    CHECK(lock_ == LOCKED);
    asm volatile("" : : : "memory");
    lock_ = FREE; // release barrier on x86
  }
};

//////////////////////////////////////////////////////////////////////

/*
 * Spin lock on a single bit in an integral type.  You can use this
 * with 16, 32, or 64-bit integral types.
 *
 * This is useful if you want a small lock and already have an int
 * with a bit in it that you aren't using.  But note that it can't be
 * as small as MicroSpinLock (1 byte), if you don't already have a
 * convenient int with an unused bit lying around to put it on.
 *
 * To construct these, either use init() or zero initialize.  We don't
 * have a real constructor because we want this to be a POD type so we
 * can put it into packed structs.
 */
template<class IntType, int Bit = sizeof(IntType) * 8 - 1>
struct PicoSpinLock {
  // Internally we deal with the unsigned version of the type.
  typedef typename std::make_unsigned<IntType>::type UIntType;

  static_assert(std::is_integral<IntType>::value,
                "PicoSpinLock needs an integral type");
  static_assert(sizeof(IntType) == 2 || sizeof(IntType) == 4 ||
                  sizeof(IntType) == 8,
                "PicoSpinLock can't work on integers smaller than 2 bytes");

public:
  static const UIntType kLockBitMask_ = UIntType(1) << Bit;
  UIntType lock_;

  /*
   * You must call this function before using this class, if you
   * default constructed it.  If you zero-initialized it you can
   * assume the PicoSpinLock is in a valid unlocked state with
   * getData() == 0.
   *
   * (This doesn't use a constructor because we want to be a POD.)
   */
  void init(IntType initialValue = 0) {
    CHECK(!(initialValue & kLockBitMask_));
    lock_ = initialValue;
  }

  /*
   * Returns the value of the integer we using for our lock, except
   * with the bit we are using as a lock cleared, regardless of
   * whether the lock is held.
   *
   * It is 'safe' to call this without holding the lock.  (As in: you
   * get the same guarantees for simultaneous accesses to an integer
   * as you normally get.)
   */
  IntType getData() const {
    return static_cast<IntType>(lock_ & ~kLockBitMask_);
  }

  /*
   * Set the value of the other bits in our integer.
   *
   * Don't use this when you aren't holding the lock, unless it can be
   * guaranteed that no other threads may be trying to use this.
   */
  void setData(IntType w) {
    CHECK(!(w & kLockBitMask_));
    lock_ = (lock_ & kLockBitMask_) | w;
  }

  /*
   * Try to get the lock without blocking: returns whether or not we
   * got it.
   */
  bool try_lock() const {
    bool ret = false;

#define FB_DOBTS(size)                                  \
  asm volatile("lock; bts" #size " %1, (%2); setnc %0"  \
               : "=r" (ret)                             \
               : "i" (Bit),                             \
                 "r" (&lock_)                           \
               : "memory", "flags")

    switch (sizeof(IntType)) {
    case 2: FB_DOBTS(w); break;
    case 4: FB_DOBTS(l); break;
    case 8: FB_DOBTS(q); break;
    }

#undef FB_DOBTS

    return ret;
  }

  /*
   * Block until we can acquire the lock.  Uses Sleeper to wait.
   */
  void lock() const {
    detail::Sleeper sleeper;
    while (!try_lock()) {
      sleeper.wait();
    }
  }

  /*
   * Release the lock, without changing the value of the rest of the
   * integer.
   */
  void unlock() const {
#define FB_DOBTR(size)                          \
  asm volatile("lock; btr" #size " %0, (%1)"    \
               :                                \
               : "i" (Bit),                     \
                 "r" (&lock_)                   \
               : "memory", "flags")


    // Reads and writes can not be reordered wrt locked instructions,
    // so we don't need a memory fence here.
    switch (sizeof(IntType)) {
    case 2: FB_DOBTR(w); break;
    case 4: FB_DOBTR(l); break;
    case 8: FB_DOBTR(q); break;
    }

#undef FB_DOBTR
  }
};

//////////////////////////////////////////////////////////////////////

typedef std::lock_guard<MicroSpinLock> MSLGuard;

//////////////////////////////////////////////////////////////////////

}

#endif
