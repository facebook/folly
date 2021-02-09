/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cassert>
#include <climits>
#include <cstdint>
#include <utility>

#include <folly/Optional.h>
#include <folly/Portability.h>
#include <folly/detail/Futex.h>

namespace folly {

/**
 * Tiny exclusive lock that uses 2 bits. It is stored as 1 byte and
 * has APIs for using the remaining 6 bits for storing user data.
 *
 * You should zero-initialize the bits of a MicroLock that you intend
 * to use.
 *
 * If you're not space-constrained, prefer std::mutex, which will
 * likely be faster, since it has more than two bits of information to
 * work with.
 *
 * You are free to put a MicroLock in a union with some other object.
 * If, for example, you want to use the bottom two bits of a pointer
 * as a lock, you can put a MicroLock in a union with the pointer,
 * which will use the two least-significant bits in the bottom byte.
 *
 * (Note that such a union is safe only because MicroLock is based on
 * a character type, and even under a strict interpretation of C++'s
 * aliasing rules, character types may alias anything.)
 *
 * Unused bits in the lock can be used to store user data via
 * lockAndLoad() and unlockAndStore(), or LockGuardWithData.
 *
 * MicroLock uses a dirty trick: it actually operates on the full
 * 32-bit, four-byte-aligned bit of memory into which it is embedded.
 * It never modifies bits outside the ones it's defined to modify, but
 * it _accesses_ all the bits in the 32-bit memory location for
 * purposes of futex management.
 *
 * The MaxSpins template parameter controls the number of times we
 * spin trying to acquire the lock.  MaxYields controls the number of
 * times we call sched_yield; once we've tried to acquire the lock
 * MaxSpins + MaxYields times, we sleep on the lock futex.
 * By adjusting these parameters, you can make MicroLock behave as
 * much or as little like a conventional spinlock as you'd like.
 *
 * Performance
 * -----------
 *
 * With the default template options, the timings for uncontended
 * acquire-then-release come out as follows on Intel(R) Xeon(R) CPU
 * E5-2660 0 @ 2.20GHz, in @mode/opt, as of the master tree at Tue, 01
 * Mar 2016 19:48:15.
 *
 * ========================================================================
 * folly/test/SmallLocksBenchmark.cpp          relative  time/iter  iters/s
 * ========================================================================
 * MicroSpinLockUncontendedBenchmark                       13.46ns   74.28M
 * PicoSpinLockUncontendedBenchmark                        14.99ns   66.71M
 * MicroLockUncontendedBenchmark                           27.06ns   36.96M
 * StdMutexUncontendedBenchmark                            25.18ns   39.72M
 * VirtualFunctionCall                                      1.72ns  579.78M
 * ========================================================================
 *
 * (The virtual dispatch benchmark is provided for scale.)
 *
 * While the uncontended case for MicroLock is competitive with the
 * glibc 2.2.0 implementation of std::mutex, std::mutex is likely to be
 * faster in the contended case, because we need to wake up all waiters
 * when we release.
 *
 * Make sure to benchmark your particular workload.
 *
 */

class MicroLockCore {
 protected:
  uint8_t lock_;
  /**
   * Arithmetic shift required to get to the byte from the word.
   */
  unsigned baseShift() const noexcept;
  /**
   * Mask for bit indicating that the flag is held.
   */
  unsigned heldBit() const noexcept;
  /**
   * Mask for bit indicating that there is a waiter that should be woken up.
   */
  unsigned waitBit() const noexcept;

  static uint8_t lockSlowPath(
      uint32_t oldWord,
      detail::Futex<>* wordPtr,
      unsigned baseShift,
      unsigned maxSpins,
      unsigned maxYields) noexcept;

  /**
   * The word (halfword on 64-bit systems) that this lock atomically operates
   * on. Although the atomic operations access 4 bytes, only the byte used by
   * the lock will be modified.
   */
  detail::Futex<>* word() const noexcept;

  static constexpr unsigned kNumLockBits = 2;
  static constexpr uint8_t kLockBits =
      static_cast<uint8_t>((1 << kNumLockBits) - 1);
  static constexpr uint8_t kDataBits = static_cast<uint8_t>(~kLockBits);
  /**
   * Decodes the value stored in the unused bits of the lock.
   */
  static constexpr uint8_t decodeDataFromByte(uint8_t lockByte) noexcept {
    return static_cast<uint8_t>(lockByte >> kNumLockBits);
  }
  /**
   * Encodes the value for the unused bits of the lock.
   */
  static constexpr uint8_t encodeDataToByte(uint8_t data) noexcept {
    return static_cast<uint8_t>(data << kNumLockBits);
  }

  static constexpr uint8_t decodeDataFromWord(
      uint32_t word, unsigned baseShift) noexcept {
    return static_cast<uint8_t>(
        static_cast<uint8_t>(word >> baseShift) >> kNumLockBits);
  }
  uint8_t decodeDataFromWord(uint32_t word) const noexcept {
    return decodeDataFromWord(word, baseShift());
  }
  static constexpr uint32_t encodeDataToWord(
      uint32_t word, unsigned shiftToByte, uint8_t value) noexcept {
    const uint32_t preservedBits = word & ~(kDataBits << shiftToByte);
    const uint32_t newBits = encodeDataToByte(value) << shiftToByte;
    return preservedBits | newBits;
  }

  template <typename Func>
  FOLLY_DISABLE_ADDRESS_SANITIZER FOLLY_DISABLE_MEMORY_SANITIZER void
  unlockAndStoreWithModifier(Func modifier) noexcept;

 public:
  /**
   * Loads the data stored in the unused bits of the lock atomically.
   */
  FOLLY_DISABLE_ADDRESS_SANITIZER FOLLY_DISABLE_MEMORY_SANITIZER uint8_t
  load(std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return decodeDataFromWord(word()->load(order));
  }

  /**
   * Stores the data in the unused bits of the lock atomically. Since 2 bits are
   * used by the lock, the most significant 2 bits of the provided value will be
   * ignored.
   */
  FOLLY_DISABLE_ADDRESS_SANITIZER FOLLY_DISABLE_MEMORY_SANITIZER void store(
      uint8_t value,
      std::memory_order order = std::memory_order_seq_cst) noexcept;

  /**
   * Unlocks the lock and stores the bits of the provided value into the data
   * bits. Since 2 bits are used by the lock, the most significant 2 bits of the
   * provided value will be ignored.
   */
  void unlockAndStore(uint8_t value) noexcept;
  void unlock() noexcept;

  /**
   * Initializes the lock state and sets the data bits to 0.
   */
  void init() noexcept { lock_ = 0; }
};

inline detail::Futex<>* MicroLockCore::word() const noexcept {
  uintptr_t lockptr = (uintptr_t)&lock_;
  lockptr &= ~(sizeof(uint32_t) - 1);
  return (detail::Futex<>*)lockptr;
}

inline unsigned MicroLockCore::baseShift() const noexcept {
  unsigned offset_bytes = (unsigned)((uintptr_t)&lock_ - (uintptr_t)word());

  return static_cast<unsigned>(
      kIsLittleEndian ? CHAR_BIT * offset_bytes
                      : CHAR_BIT * (sizeof(uint32_t) - offset_bytes - 1));
}

inline unsigned MicroLockCore::heldBit() const noexcept {
  return 1U << (baseShift() + 0);
}

inline unsigned MicroLockCore::waitBit() const noexcept {
  return 1U << (baseShift() + 1);
}

inline void MicroLockCore::store(
    uint8_t value, std::memory_order order) noexcept {
  detail::Futex<>* wordPtr = word();

  const auto shiftToByte = baseShift();
  auto oldWord = wordPtr->load(std::memory_order_relaxed);
  while (true) {
    auto newWord = encodeDataToWord(oldWord, shiftToByte, value);
    if (wordPtr->compare_exchange_weak(
            oldWord, newWord, order, std::memory_order_relaxed)) {
      break;
    }
  }
}

template <typename Func>
void MicroLockCore::unlockAndStoreWithModifier(Func modifier) noexcept {
  detail::Futex<>* wordPtr = word();
  uint32_t oldWord;
  uint32_t newWord;

  oldWord = wordPtr->load(std::memory_order_relaxed);
  do {
    assert(oldWord & heldBit());
    newWord = modifier(oldWord) & ~(heldBit() | waitBit());
  } while (!wordPtr->compare_exchange_weak(
      oldWord, newWord, std::memory_order_release, std::memory_order_relaxed));

  if (oldWord & waitBit()) {
    detail::futexWake(wordPtr, 1, heldBit());
  }
}

inline void MicroLockCore::unlockAndStore(uint8_t value) noexcept {
  unlockAndStoreWithModifier(
      [value, shiftToByte = baseShift()](uint32_t oldWord) {
        return encodeDataToWord(oldWord, shiftToByte, value);
      });
}

inline void MicroLockCore::unlock() noexcept {
  unlockAndStoreWithModifier([](uint32_t oldWord) { return oldWord; });
}

template <unsigned MaxSpins = 1000, unsigned MaxYields = 0>
class MicroLockBase : public MicroLockCore {
 public:
  /**
   * Locks the lock and returns the data stored in the unused bits of the lock.
   * This is useful when you want to use the unused bits of the lock to store
   * data, in which case reading and locking should be done in one atomic
   * operation.
   */
  FOLLY_DISABLE_ADDRESS_SANITIZER FOLLY_DISABLE_MEMORY_SANITIZER uint8_t
  lockAndLoad() noexcept;
  void lock() noexcept { lockAndLoad(); }
  FOLLY_DISABLE_ADDRESS_SANITIZER FOLLY_DISABLE_MEMORY_SANITIZER bool
  try_lock() noexcept;

  /**
   * A lock guard which allows reading and writing to the unused bits of the
   * lock as data.
   */
  struct LockGuardWithData {
    explicit LockGuardWithData(MicroLockBase<MaxSpins, MaxYields>& lock)
        : lock_(lock) {
      loadedValue_ = lock_.lockAndLoad();
    }

    ~LockGuardWithData() noexcept {
      if (storedValue_) {
        lock_.unlockAndStore(*storedValue_);
      } else {
        lock_.unlock();
      }
    }

    /**
     * The stored data bits at the time of locking.
     */
    uint8_t loadedValue() const noexcept { return loadedValue_; }

    /**
     * The value that will be stored back into data bits when it is unlocked.
     */
    void storeValue(uint8_t value) noexcept { storedValue_ = value; }

   private:
    MicroLockBase<MaxSpins, MaxYields>& lock_;
    uint8_t loadedValue_;
    folly::Optional<uint8_t> storedValue_;
  };
};

template <unsigned MaxSpins, unsigned MaxYields>
bool MicroLockBase<MaxSpins, MaxYields>::try_lock() noexcept {
  // N.B. You might think that try_lock is just the fast path of lock,
  // but you'd be wrong.  Keep in mind that other parts of our host
  // word might be changing while we take the lock!  We're not allowed
  // to fail spuriously if the lock is in fact not held, even if other
  // people are concurrently modifying other parts of the word.
  //
  // We need to loop until we either see firm evidence that somebody
  // else has the lock (by looking at heldBit) or see our CAS succeed.
  // A failed CAS by itself does not indicate lock-acquire failure.

  detail::Futex<>* wordPtr = word();
  uint32_t oldWord = wordPtr->load(std::memory_order_relaxed);
  do {
    if (oldWord & heldBit()) {
      return false;
    }
  } while (!wordPtr->compare_exchange_weak(
      oldWord,
      oldWord | heldBit(),
      std::memory_order_acquire,
      std::memory_order_relaxed));

  return true;
}

template <unsigned MaxSpins, unsigned MaxYields>
uint8_t MicroLockBase<MaxSpins, MaxYields>::lockAndLoad() noexcept {
  static_assert(MaxSpins + MaxYields < (unsigned)-1, "overflow");

  detail::Futex<>* wordPtr = word();
  uint32_t oldWord;
  oldWord = wordPtr->load(std::memory_order_relaxed);
  if ((oldWord & heldBit()) == 0 &&
      wordPtr->compare_exchange_weak(
          oldWord,
          oldWord | heldBit(),
          std::memory_order_acquire,
          std::memory_order_relaxed)) {
    // Fast uncontended case: memory_order_acquire above is our barrier
    return decodeDataFromWord(oldWord | heldBit());
  } else {
    // lockSlowPath doesn't call waitBit(); it just shifts the input bit.  Make
    // sure its shifting produces the same result a call to waitBit would.
    assert(heldBit() << 1 == waitBit());
    // lockSlowPath emits its own memory barrier
    return lockSlowPath(oldWord, wordPtr, baseShift(), MaxSpins, MaxYields);
  }
}

typedef MicroLockBase<> MicroLock;
} // namespace folly
