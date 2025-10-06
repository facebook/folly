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

#include <utility>

#if defined(__linux__)
#include <sched.h>
#endif

namespace folly {

#if defined(__linux__)

class cpu_set_state {
 private:
  cpu_set_t* set_{nullptr};
  size_t size_{0};
  bool alloc_{false};

  cpu_set_state(cpu_set_t* set, size_t size, bool alloc) noexcept
      : set_{set}, size_{size}, alloc_{alloc} {}

 public:
  /// ask
  ///
  /// Returns a cpu_set_state owning or referencing a cpu_set_t filled in by a
  /// successful call to sched_getaffinity.
  ///
  /// Takes an optional stack-allocated cpu_set_t, which must survive the
  /// returned object. This can avoid one allocation, depending on the host.
  ///
  /// Not async-signal-safe.
  static cpu_set_state ask(cpu_set_t* stackset = nullptr) noexcept;

  cpu_set_state() noexcept = default;

  cpu_set_state(cpu_set_state const& that) = delete;

  cpu_set_state(cpu_set_state&& that) noexcept
      : set_{std::exchange(that.set_, nullptr)},
        size_{std::exchange(that.size_, 0)},
        alloc_{std::exchange(that.alloc_, false)} {}

  cpu_set_state& operator=(cpu_set_state const& that) = delete;

  cpu_set_state& operator=(cpu_set_state&& that) noexcept {
    auto tmp = std::move(that);
    std::swap(set_, tmp.set_);
    std::swap(size_, tmp.size_);
    std::swap(alloc_, tmp.alloc_);
    return *this;
  }

  ~cpu_set_state();

  cpu_set_t* set_ptr() const noexcept { return set_; }
  size_t set_size() const noexcept { return size_; }

  // use alloc_size() for the CPU_..._S macros, like CPU_COUNT_S and CPU_ISSET_S
  size_t alloc_size() const noexcept { return CPU_ALLOC_SIZE(size_); }

  size_t cpu_count() const noexcept {
    return !set_ ? 0 : CPU_COUNT_S(CPU_ALLOC_SIZE(size_), set_);
  }

  bool cpu_isset(size_t idx) const noexcept {
    return set_ && idx < size_ && CPU_ISSET_S(idx, CPU_ALLOC_SIZE(size_), set_);
  }
};

#endif

unsigned int hardware_concurrency() noexcept;

} // namespace folly
