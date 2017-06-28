/*
 * Copyright 2017 Facebook, Inc.
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

#include <array>
#include <memory>

#include <folly/Enumerate.h>
#include <folly/concurrency/CacheLocality.h>

namespace folly {

/**
 * This class creates core-local caches for a given shared_ptr, to
 * mitigate contention when acquiring/releasing it.
 *
 * It has the same thread-safety guarantees as shared_ptr: it is safe
 * to concurrently call get(), but reset()s must be synchronized with
 * reads and other resets().
 *
 * @author Giuseppe Ottaviano <ott@fb.com>
 */
template <class T, size_t kNumSlots = 64>
class CoreCachedSharedPtr {
 public:
  explicit CoreCachedSharedPtr(const std::shared_ptr<T>& p = nullptr) {
    reset(p);
  }

  void reset(const std::shared_ptr<T>& p = nullptr) {
    // Allocate each Holder in a different CoreAllocator stripe to
    // prevent false sharing. Their control blocks will be adjacent
    // thanks to allocate_shared().
    for (auto slot : folly::enumerate(slots_)) {
      auto alloc = getCoreAllocatorStl<Holder, kNumSlots>(slot.index);
      auto holder = std::allocate_shared<Holder>(alloc, p);
      *slot = std::shared_ptr<T>(holder, p.get());
    }
  }

  std::shared_ptr<T> get() const {
    return slots_[AccessSpreader<>::current(kNumSlots)];
  }

 private:
  using Holder = std::shared_ptr<T>;

  template <class, size_t>
  friend class CoreCachedWeakPtr;

  std::array<std::shared_ptr<T>, kNumSlots> slots_;
};

template <class T, size_t kNumSlots = 64>
class CoreCachedWeakPtr {
 public:
  explicit CoreCachedWeakPtr(const CoreCachedSharedPtr<T, kNumSlots>& p) {
    for (auto slot : folly::enumerate(slots_)) {
      *slot = p.slots_[slot.index];
    }
  }

  std::weak_ptr<T> get() const {
    return slots_[AccessSpreader<>::current(kNumSlots)];
  }

 private:
  std::array<std::weak_ptr<T>, kNumSlots> slots_;
};

} // namespace
