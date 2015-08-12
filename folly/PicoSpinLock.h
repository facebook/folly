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

/*
 * @author Keith Adams <kma@fb.com>
 * @author Jordan DeLong <delong.j@fb.com>
 */

#include <array>
#include <cinttypes>
#include <type_traits>
#include <cstdlib>
#include <pthread.h>
#include <mutex>
#include <atomic>

#include <glog/logging.h>
#include <folly/detail/Sleeper.h>
#include <folly/Portability.h>

#if !FOLLY_X64 && !FOLLY_A64 && !FOLLY_PPC64
# error "PicoSpinLock.h is currently x64, aarch64 and ppc64 only."
#endif

namespace folly {

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

#if FOLLY_X64
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
#elif FOLLY_A64
    ret = __atomic_fetch_or(&lock_, 1 << Bit, __ATOMIC_SEQ_CST);
#elif FOLLY_PPC64
#define FB_DOBTS(size)                                 \
    asm volatile("\teieio\n"                           \
                 "\tl" #size "arx 14,0,%[lockPtr]\n"   \
                 "\tli 15,1\n"                         \
                 "\tsldi 15,15,%[bit]\n"               \
                 "\tand. 16,15,14\n"                   \
                 "\tbne 0f\n"                          \
                 "\tor 14,14,15\n"                     \
                 "\tst" #size "cx. 14,0,%[lockPtr]\n"  \
                 "\tbne 0f\n"                          \
                 "\tori %[output],%[output],1\n"       \
                 "\tisync\n"                           \
                 "0:\n"                                \
                 : [output] "+r" (ret)                 \
                 : [lockPtr] "r"(&lock_),              \
                   [bit] "i" (Bit)                     \
                 : "cr0", "memory", "r14", "r15", "r16")

    switch (sizeof(IntType)) {
    case 2: FB_DOBTS(h); break;
    case 4: FB_DOBTS(w); break;
    case 8: FB_DOBTS(d); break;
    }

#undef FB_DOBTS
#else
#error "x86 aarch64 ppc64 only"
#endif

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
#if FOLLY_X64
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
#elif FOLLY_A64
    __atomic_fetch_and(&lock_, ~(1 << Bit), __ATOMIC_SEQ_CST);
#elif FOLLY_PPC64
#define FB_DOBTR(size)                                 \
    asm volatile("\teieio\n"                           \
                 "0:  l" #size "arx 14,0,%[lockPtr]\n" \
                 "\tli 15,1\n"                         \
                 "\tsldi 15,15,%[bit]\n"               \
                 "\txor 14,14,15\n"                    \
                 "\tst" #size "cx. 14,0,%[lockPtr]\n"  \
                 "\tbne 0b\n"                          \
                 "\tisync\n"                           \
                 :                                     \
                 : [lockPtr] "r"(&lock_),              \
                   [bit] "i" (Bit)                     \
                 : "cr0", "memory", "r14", "r15")

    switch (sizeof(IntType)) {
    case 2: FB_DOBTR(h); break;
    case 4: FB_DOBTR(w); break;
    case 8: FB_DOBTR(d); break;
    }

#undef FB_DOBTR
#else
# error "x64 aarch64 ppc64 only"
#endif
  }
};

}
