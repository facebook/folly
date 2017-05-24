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
#include <folly/detail/CacheLocality.h>

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
    for (auto& slot : slots_) {
      auto holder = std::make_shared<Holder>(p);
      slot = std::shared_ptr<T>(holder, p.get());
    }
  }

  std::shared_ptr<T> get() const {
    return slots_[detail::AccessSpreader<>::current(kNumSlots)];
  }

 private:
  template <class, size_t>
  friend class CoreCachedWeakPtr;

  // Space the Holders by a cache line, so their control blocks (which
  // are adjacent to the slots thanks to make_shared()) will also be
  // spaced.
  struct FOLLY_ALIGN_TO_AVOID_FALSE_SHARING Holder {
    explicit Holder(std::shared_ptr<T> p) : ptr(std::move(p)) {}
    std::shared_ptr<T> ptr;
  };

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
    return slots_[detail::AccessSpreader<>::current(kNumSlots)];
  }

 private:
  std::array<std::weak_ptr<T>, kNumSlots> slots_;
};

} // namespace
