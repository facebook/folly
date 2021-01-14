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
 * Tiny exclusive lock that packs four lock slots into a single
 * byte. Each slot is an independent real, sleeping lock.  The default
 * lock and unlock functions operate on slot zero, which modifies only
 * the low two bits of the host byte.
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
 * as a lock, you can put a MicroLock in a union with the pointer and
 * limit yourself to MicroLock slot zero, which will use the two
 * least-significant bits in the bottom byte.
 *
 * (Note that such a union is safe only because MicroLock is based on
 * a character type, and even under a strict interpretation of C++'s
 * aliasing rules, character types may alias anything.)
 *
 * Unused slots in the lock can be used to store user data via
 * lockAndLoad() and unlockAndStore(), or LockGuardWithDataSlots.
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
  inline detail::Futex<>* word() const; // Well, halfword on 64-bit systems
  inline uint32_t baseShift(unsigned slot) const;
  inline uint32_t heldBit(unsigned slot) const;
  inline uint32_t waitBit(unsigned slot) const;
  static uint8_t lockSlowPath(
      uint32_t oldWord,
      detail::Futex<>* wordPtr,
      uint32_t slotHeldBit,
      unsigned maxSpins,
      unsigned maxYields);

  FOLLY_ALWAYS_INLINE constexpr static uint8_t byteFromWord(uint32_t word);

  template <typename Func>
  FOLLY_DISABLE_ADDRESS_SANITIZER inline void unlockAndStoreWithModifier(
      unsigned slot,
      Func modifier);

 public:
  static constexpr unsigned kBitsPerSlot = 2;

  template <unsigned Slot>
  static constexpr uint8_t slotMask() {
    static_assert(
        Slot < CHAR_BIT / kBitsPerSlot, "slot is out of range of uint8_t");
    return 0b11 << (Slot * kBitsPerSlot);
  }

  /**
   * Loads the state of this lock atomically. This is useful for introspecting
   * any user data that may be placed in unused slots.
   */
  FOLLY_DISABLE_ADDRESS_SANITIZER inline uint8_t load(
      std::memory_order order = std::memory_order_seq_cst) const {
    return byteFromWord(word()->load(order));
  }

  inline void unlock(unsigned slot);
  /**
   * Unlocks the selected slot and stores the bits of the provided value in the
   * other slots. The two bits of the selected slot will be automatically masked
   * out from the provided value.
   *
   * For example, the following usage unlocks slot1 preserves the state of
   * slot3. This indicates that slot1 and slot3 are used for locking while slot0
   * and slot2 are used as data:
   *
   *    lock.unlockAndStore(1, ~MicroLock::slotMask<3>(), value);
   */
  inline void unlockAndStore(unsigned slot, uint8_t dataMask, uint8_t value);
  inline void unlock() { unlock(0); }

  // Initializes all the slots.
  inline void init() { lock_ = 0; }
};

inline detail::Futex<>* MicroLockCore::word() const {
  uintptr_t lockptr = (uintptr_t)&lock_;
  lockptr &= ~(sizeof(uint32_t) - 1);
  return (detail::Futex<>*)lockptr;
}

inline unsigned MicroLockCore::baseShift(unsigned slot) const {
  assert(slot < CHAR_BIT / kBitsPerSlot);

  unsigned offset_bytes = (unsigned)((uintptr_t)&lock_ - (uintptr_t)word());

  return static_cast<unsigned>(
      kIsLittleEndian ? offset_bytes * CHAR_BIT + slot * kBitsPerSlot
                      : CHAR_BIT * (sizeof(uint32_t) - offset_bytes - 1) +
              slot * kBitsPerSlot);
}

inline uint32_t MicroLockCore::heldBit(unsigned slot) const {
  return 1U << (baseShift(slot) + 0);
}

inline uint32_t MicroLockCore::waitBit(unsigned slot) const {
  return 1U << (baseShift(slot) + 1);
}

constexpr inline uint8_t MicroLockCore::byteFromWord(uint32_t word) {
  return kIsLittleEndian ? static_cast<uint8_t>(word & 0xff)
                         : static_cast<uint8_t>((word >> 24) & 0xff);
}

template <typename Func>
void MicroLockCore::unlockAndStoreWithModifier(unsigned slot, Func modifier) {
  detail::Futex<>* wordPtr = word();
  uint32_t oldWord;
  uint32_t newWord;

  oldWord = wordPtr->load(std::memory_order_relaxed);
  do {
    assert(oldWord & heldBit(slot));
    newWord = modifier(oldWord) & ~(heldBit(slot) | waitBit(slot));
  } while (!wordPtr->compare_exchange_weak(
      oldWord, newWord, std::memory_order_release, std::memory_order_relaxed));

  if (oldWord & waitBit(slot)) {
    detail::futexWake(wordPtr, 1, heldBit(slot));
  }
}

inline void
MicroLockCore::unlockAndStore(unsigned slot, uint8_t dataMask, uint8_t value) {
  unlockAndStoreWithModifier(
      slot, [dataMask, value, shiftToByte = baseShift(0)](uint32_t oldWord) {
        const uint32_t preservedBits = oldWord & ~(dataMask << shiftToByte);
        const uint32_t newBits = (value & dataMask) << shiftToByte;
        return preservedBits | newBits;
      });
}

inline void MicroLockCore::unlock(unsigned slot) {
  unlockAndStoreWithModifier(slot, [](uint32_t oldWord) { return oldWord; });
}

template <unsigned MaxSpins = 1000, unsigned MaxYields = 0>
class MicroLockBase : public MicroLockCore {
 public:
  /**
   * Locks the selected slot and returns the entire byte that represents the
   * state of this lock. This is useful when you want to use some of the slots
   * to store data, in which case reading and locking should be done in one
   * atomic operation.
   */
  FOLLY_DISABLE_ADDRESS_SANITIZER inline uint8_t lockAndLoad(unsigned slot);
  inline void lock(unsigned slot) { lockAndLoad(slot); }
  inline void lock() { lock(0); }
  FOLLY_DISABLE_ADDRESS_SANITIZER inline bool try_lock(unsigned slot);
  inline bool try_lock() { return try_lock(0); }

  /**
   * A lock guard which allows reading and writing to the unused slots as data.
   * The template parameters are used to select the slot indices which represent
   * data slots. The bits representing all other slots will be masked out when
   * storing the user data.
   *
   * Example:
   *
   *   LockGuardWithDataSlots<1, 2> guard(lock, 0);
   *   guard.loadedValue(); // bits of slot1 and slot2 (lock_ & 0b00111100)
   *   guard.storeValue(0b10101010); // stored as 0bxx1010xx (x means unchanged)
   */
  template <unsigned... Slots>
  struct LockGuardWithDataSlots {
    explicit LockGuardWithDataSlots(
        MicroLockBase<MaxSpins, MaxYields>& lock,
        unsigned slot = 0)
        : lock_(lock), slot_(slot) {
      loadedValue_ = lock_.lockAndLoad(slot_) & dataMask();
    }

    ~LockGuardWithDataSlots() noexcept {
      if (storedValue_) {
        lock_.unlockAndStore(slot_, dataMask(), *storedValue_);
      } else {
        lock_.unlock(slot_);
      }
    }

    /**
     * The stored data with the non-data slot bits masked out (which will be 0).
     */
    uint8_t loadedValue() const { return loadedValue_; }

    /**
     * The value that will be stored back into data lock slots when it is
     * unlocked. The non-data slot bits in the provided value will be ignored.
     */
    void storeValue(uint8_t value) { storedValue_ = value; }

   private:
    // C++17 fold expressions would be handy here
    FOLLY_ALWAYS_INLINE constexpr static uint8_t bitOr(uint8_t value) {
      return value;
    }
    template <typename... Others>
    FOLLY_ALWAYS_INLINE constexpr static uint8_t bitOr(
        uint8_t value,
        Others... others) {
      return value | bitOr(others...);
    }
    constexpr static uint8_t dataMask() { return bitOr(slotMask<Slots>()...); }

    MicroLockBase<MaxSpins, MaxYields>& lock_;
    unsigned slot_;
    uint8_t loadedValue_;
    folly::Optional<uint8_t> storedValue_;
  };
};

template <unsigned MaxSpins, unsigned MaxYields>
bool MicroLockBase<MaxSpins, MaxYields>::try_lock(unsigned slot) {
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
    if (oldWord & heldBit(slot)) {
      return false;
    }
  } while (!wordPtr->compare_exchange_weak(
      oldWord,
      oldWord | heldBit(slot),
      std::memory_order_acquire,
      std::memory_order_relaxed));

  return true;
}

template <unsigned MaxSpins, unsigned MaxYields>
uint8_t MicroLockBase<MaxSpins, MaxYields>::lockAndLoad(unsigned slot) {
  static_assert(MaxSpins + MaxYields < (unsigned)-1, "overflow");

  detail::Futex<>* wordPtr = word();
  uint32_t oldWord;
  oldWord = wordPtr->load(std::memory_order_relaxed);
  if ((oldWord & heldBit(slot)) == 0 &&
      wordPtr->compare_exchange_weak(
          oldWord,
          oldWord | heldBit(slot),
          std::memory_order_acquire,
          std::memory_order_relaxed)) {
    // Fast uncontended case: memory_order_acquire above is our barrier
    return byteFromWord(oldWord | heldBit(slot));
  } else {
    // lockSlowPath doesn't have any slot-dependent computation; it
    // just shifts the input bit.  Make sure its shifting produces the
    // same result a call to waitBit for our slot would.
    assert(heldBit(slot) << 1 == waitBit(slot));
    // lockSlowPath emits its own memory barrier
    return lockSlowPath(oldWord, wordPtr, heldBit(slot), MaxSpins, MaxYields);
  }
}

typedef MicroLockBase<> MicroLock;
} // namespace folly
