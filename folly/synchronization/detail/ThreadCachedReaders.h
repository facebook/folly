/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
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

#include <atomic>
#include <cstdint>
#include <limits>
#include <folly/Function.h>
#include <folly/ThreadLocal.h>
#include <folly/synchronization/AsymmetricMemoryBarrier.h>

namespace folly {

namespace detail {

// Use memory_order_seq_cst for accesses to increments/decrements if we're
// running under TSAN, because TSAN ignores barriers completely.
constexpr std::memory_order kReadersMemoryOrder =
    kIsSanitizeThread ? std::memory_order_seq_cst : std::memory_order_relaxed;

// A data structure that keeps a per-thread cache of a bitfield that contains
// the current active epoch for readers in the thread, and the number of active
// readers:
//
//                _______________________________________
//                |   Current Epoch    |    # Readers   |
// epoch_readers: | 63 62 ... 34 33 32 | 31 30 ... 2 1 0|
//                o--------------------|----------------o
//
// There are several important implications with this data structure:
//
// 1. Read regions must be entered and exited on the same thread.
// 2. Read regions are fully nested. That is, two read regions in a single
//    thread may not overlap across two epochs.
//
// These implications afford us debugging opportunities, such
// as being able to detect long-running readers (T113951078).
template <typename Tag>
class ThreadCachedReaders {
  std::atomic<uint64_t> orphan_epoch_readers_ = std::atomic<uint64_t>(0);
  folly::detail::Futex<> waiting_{0};

  class EpochCount {
   public:
    ThreadCachedReaders* readers_;
    explicit constexpr EpochCount(ThreadCachedReaders* readers) noexcept
        : readers_(readers), epoch_readers_{}, cache_(readers->tls_cache_) {}
    std::atomic<uint64_t> epoch_readers_;
    EpochCount*& cache_; // reference to the cached ptr
    ~EpochCount() noexcept {
      // Set the cached epoch and readers.
      readers_->orphan_epoch_readers_.store(
          epoch_readers_.load(kReadersMemoryOrder), kReadersMemoryOrder);
      folly::asymmetricLightBarrier(); // B
      detail::futexWake(&readers_->waiting_);
      // reset the cache_ on destructor so we can handle the delete/recreate.
      cache_ = nullptr;
    }
  };
  folly::ThreadLocalPtr<EpochCount, Tag> cs_;

  // Cache the count of nested readers and their epochs in a thread local.
  static thread_local EpochCount* tls_cache_;

  void init() {
    auto ret = new EpochCount(this);
    cs_.reset(ret);
    tls_cache_ = ret;
  }

  static uint32_t readers_from_epoch_reader(uint64_t epoch_reader) {
    return static_cast<uint32_t>(epoch_reader);
  }

  static uint32_t epoch_from_epoch_reader(uint64_t epoch_reader) {
    return static_cast<uint32_t>(epoch_reader >> 32);
  }

  static uint64_t create_epoch_reader(uint64_t epoch, uint32_t readers) {
    return (epoch << 32) + readers;
  }

 public:
  FOLLY_ALWAYS_INLINE void increment(uint64_t epoch) {
    if (tls_cache_ == nullptr) {
      init();
    }
    uint64_t epoch_reader =
        tls_cache_->epoch_readers_.load(kReadersMemoryOrder);
    if (readers_from_epoch_reader(epoch_reader) != 0) {
      DCHECK(
          readers_from_epoch_reader(epoch_reader) <
          std::numeric_limits<uint32_t>::max());
      tls_cache_->epoch_readers_.store(epoch_reader + 1, kReadersMemoryOrder);
    } else {
      tls_cache_->epoch_readers_.store(
          create_epoch_reader(epoch, 1), kReadersMemoryOrder);
    }
    folly::asymmetricLightBarrier(); // A
  }

  FOLLY_ALWAYS_INLINE void decrement() {
    folly::asymmetricLightBarrier(); // B

    DCHECK(tls_cache_ != nullptr);
    uint64_t epoch_reader =
        tls_cache_->epoch_readers_.load(kReadersMemoryOrder);
    DCHECK(readers_from_epoch_reader(epoch_reader) > 0);
    tls_cache_->epoch_readers_.store(epoch_reader - 1, kReadersMemoryOrder);

    folly::asymmetricLightBarrier(); // C
    if (waiting_.load(std::memory_order_acquire)) {
      waiting_.store(0, std::memory_order_release);
      detail::futexWake(&waiting_);
    }
  }

  static bool epochHasReaders(uint8_t epoch, uint64_t epoch_readers) {
    bool has_readers = readers_from_epoch_reader(epoch_readers) != 0;
    bool same_epoch = (epoch_from_epoch_reader(epoch_readers) & 0x1) == epoch;
    return has_readers && same_epoch;
  }

  bool epochIsClear(uint8_t epoch) {
    uint64_t orphaned = orphan_epoch_readers_.load(kReadersMemoryOrder);
    if (epochHasReaders(epoch, orphaned)) {
      return false;
    }

    // Matches A and B - ensure all threads have seen new value of version,
    // *and* that we see current values of readers for every thread below.
    //
    // Note that in lock_shared if a reader is currently between the
    // version load and counter increment, they may update the wrong
    // epoch.  However, this is ok - they started concurrently *after*
    // any callbacks that will run, and therefore it is safe to run
    // the callbacks.
    folly::asymmetricHeavyBarrier();
    auto access = cs_.accessAllThreads();
    return !std::any_of(access.begin(), access.end(), [&](auto& i) {
      return epochHasReaders(epoch, i.epoch_readers_.load(kReadersMemoryOrder));
    });
  }

  void waitForZero(uint8_t epoch) {
    // Try reading before futex sleeping.
    if (epochIsClear(epoch)) {
      return;
    }

    while (true) {
      // Matches C.  Ensure either decrement sees waiting_,
      // or we see their decrement and can safely sleep.
      waiting_.store(1, std::memory_order_release);
      folly::asymmetricHeavyBarrier();
      if (epochIsClear(epoch)) {
        break;
      }
      detail::futexWait(&waiting_, 1);
    }
    waiting_.store(0, std::memory_order_relaxed);
  }

  // We are guaranteed to be called while StaticMeta lock is still
  // held because of ordering in AtForkList.  We can therefore safely
  // touch orphan_ and clear out all counts.
  void resetAfterFork() {
    if (tls_cache_) {
      tls_cache_->epoch_readers_.store(0, kReadersMemoryOrder);
    }
    orphan_epoch_readers_.store(0, kReadersMemoryOrder);
    folly::asymmetricLightBarrier();
  }
};

template <typename Tag>
thread_local typename detail::ThreadCachedReaders<Tag>::EpochCount*
    detail::ThreadCachedReaders<Tag>::tls_cache_ = nullptr;

} // namespace detail
} // namespace folly
