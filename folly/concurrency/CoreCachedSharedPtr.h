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

#include <array>
#include <atomic>
#include <memory>

#include <folly/CppAttributes.h>
#include <folly/Portability.h>
#include <folly/Unit.h>
#include <folly/concurrency/CacheLocality.h>
#include <folly/synchronization/Hazptr.h>

namespace folly {

// On mobile we do not expect high concurrency, and memory is more important, so
// use more conservative caching.
constexpr size_t kCoreCachedSharedPtrDefaultMaxSlots = kIsMobile ? 4 : 64;

namespace core_cached_shared_ptr_detail {

template <size_t kMaxSlots>
class SlotsConfig {
 public:
  FOLLY_EXPORT static void initialize() {
    FOLLY_MAYBE_UNUSED static const Unit _ = [] {
      // We need at most as many slots as the number of L1 caches, so we can
      // avoid wasting memory if more slots are requested.
      const auto l1Caches = CacheLocality::system().numCachesByLevel.front();
      num_ = std::min(std::max<size_t>(1, l1Caches), kMaxSlots);
      return unit;
    }();
  }

  static size_t num() { return num_.load(std::memory_order_relaxed); }

 private:
  static std::atomic<size_t> num_;
};

// Initialize with a valid num so that get() always returns a valid stripe, even
// if initialize() has not been called yet.
template <size_t kMaxSlots>
std::atomic<size_t> SlotsConfig<kMaxSlots>::num_{1};

// Check whether a shared_ptr is equivalent to default-constructed. Because of
// aliasing constructors, there can be both nullptr with a managed object, and
// non-nullptr with no managed object, so we need to check both.
template <class T>
bool isDefault(const std::shared_ptr<T>& p) {
  return p == nullptr && p.use_count() == 0;
}

} // namespace core_cached_shared_ptr_detail

/**
 * This class creates core-local caches for a given shared_ptr, to
 * mitigate contention when acquiring/releasing it.
 *
 * It has the same thread-safety guarantees as shared_ptr: it is safe
 * to concurrently call get(), but reset()s must be synchronized with
 * reads and other reset()s.
 *
 * @author Giuseppe Ottaviano <ott@fb.com>
 */
template <class T, size_t kMaxSlots = kCoreCachedSharedPtrDefaultMaxSlots>
class CoreCachedSharedPtr {
  using SlotsConfig = core_cached_shared_ptr_detail::SlotsConfig<kMaxSlots>;

 public:
  CoreCachedSharedPtr() = default;
  explicit CoreCachedSharedPtr(const std::shared_ptr<T>& p) { reset(p); }

  void reset(const std::shared_ptr<T>& p = nullptr) {
    SlotsConfig::initialize();
    // Allocate each Holder in a different CoreRawAllocator stripe to
    // prevent false sharing. Their control blocks will be adjacent
    // thanks to allocate_shared().
    for (size_t i = 0; i < SlotsConfig::num(); ++i) {
      // Try freeing the control block before allocating a new one.
      slots_[i] = {};
      if (!core_cached_shared_ptr_detail::isDefault(p)) {
        auto alloc = getCoreAllocator<Holder, kMaxSlots>(i);
        auto holder = std::allocate_shared<Holder>(alloc, p);
        slots_[i] = std::shared_ptr<T>(holder, p.get());
      }
    }
  }

  std::shared_ptr<T> get() const {
    return slots_[AccessSpreader<>::cachedCurrent(SlotsConfig::num())];
  }

 private:
  using Holder = std::shared_ptr<T>;

  template <class, size_t>
  friend class CoreCachedWeakPtr;

  std::array<std::shared_ptr<T>, kMaxSlots> slots_;
};

template <class T, size_t kMaxSlots = kCoreCachedSharedPtrDefaultMaxSlots>
class CoreCachedWeakPtr {
  using SlotsConfig = core_cached_shared_ptr_detail::SlotsConfig<kMaxSlots>;

 public:
  CoreCachedWeakPtr() = default;
  explicit CoreCachedWeakPtr(const CoreCachedSharedPtr<T, kMaxSlots>& p) {
    reset(p);
  }

  void reset() { *this = {}; }
  void reset(const CoreCachedSharedPtr<T, kMaxSlots>& p) {
    SlotsConfig::initialize();
    for (size_t i = 0; i < SlotsConfig::num(); ++i) {
      slots_[i] = p.slots_[i];
    }
  }

  std::weak_ptr<T> get() const {
    return slots_[AccessSpreader<>::cachedCurrent(SlotsConfig::num())];
  }

  // Faster than get().lock(), as it avoid one weak count cycle.
  std::shared_ptr<T> lock() const {
    return slots_[AccessSpreader<>::cachedCurrent(SlotsConfig::num())].lock();
  }

 private:
  std::array<std::weak_ptr<T>, kMaxSlots> slots_;
};

/**
 * This class creates core-local caches for a given shared_ptr, to
 * mitigate contention when acquiring/releasing it.
 *
 * All methods are threadsafe.  Hazard pointers are used to avoid
 * use-after-free for concurrent reset() and get() operations.
 *
 * Concurrent reset()s are sequenced with respect to each other: the
 * sharded shared_ptrs will always all be set to the same value.
 * get()s will never see a newer pointer on one core, and an older
 * pointer on another after a subsequent thread migration.
 */
template <class T, size_t kMaxSlots = kCoreCachedSharedPtrDefaultMaxSlots>
class AtomicCoreCachedSharedPtr {
  using SlotsConfig = core_cached_shared_ptr_detail::SlotsConfig<kMaxSlots>;

 public:
  AtomicCoreCachedSharedPtr() = default;
  explicit AtomicCoreCachedSharedPtr(const std::shared_ptr<T>& p) { reset(p); }

  ~AtomicCoreCachedSharedPtr() {
    // Delete of AtomicCoreCachedSharedPtr must be synchronized, no
    // need for slots->retire().
    delete slots_.load(std::memory_order_acquire);
  }

  void reset(const std::shared_ptr<T>& p = nullptr) {
    SlotsConfig::initialize();
    std::unique_ptr<Slots> newslots;
    if (!core_cached_shared_ptr_detail::isDefault(p)) {
      newslots = std::make_unique<Slots>();
      // Allocate each Holder in a different CoreRawAllocator stripe to
      // prevent false sharing. Their control blocks will be adjacent
      // thanks to allocate_shared().
      for (size_t i = 0; i < SlotsConfig::num(); ++i) {
        auto alloc = getCoreAllocator<Holder, kMaxSlots>(i);
        auto holder = std::allocate_shared<Holder>(alloc, p);
        newslots->slots[i] = std::shared_ptr<T>(holder, p.get());
      }
    }

    if (auto oldslots = slots_.exchange(newslots.release())) {
      oldslots->retire();
    }
  }

  std::shared_ptr<T> get() const {
    folly::hazptr_local<1> hazptr;
    if (auto slots = hazptr[0].protect(slots_)) {
      return slots->slots[AccessSpreader<>::cachedCurrent(SlotsConfig::num())];
    } else {
      return nullptr;
    }
  }

 private:
  using Holder = std::shared_ptr<T>;
  struct Slots : folly::hazptr_obj_base<Slots> {
    std::array<std::shared_ptr<T>, kMaxSlots> slots;
  };
  std::atomic<Slots*> slots_{nullptr};
};

} // namespace folly
