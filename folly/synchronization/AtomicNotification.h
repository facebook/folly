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
#include <condition_variable>

#include <folly/Portability.h>

namespace folly {

/**
 * The behavior of the atomic_wait() family of functions is semantically
 * identical to futex().  Correspondingly, calling atomic_notify_one(),
 * atomic_notify_all() is identical to futexWake() with 1 and
 * std::numeric_limits<int>::max() respectively
 *
 * The difference here compared to the futex API above is that it works with
 * all types of atomic widths.  When a 32 bit atomic integer is used, the
 * implementation falls back to using futex() if possible, and the
 * compatibility implementation for non-linux systems otherwise.  For all
 * other integer widths, the compatibility implementation is used
 *
 * The templating of this API is changed from the standard in the following
 * ways
 *
 * - At the time of writing, libstdc++'s implementation of std::atomic<> does
 *   not include the value_type alias.  So we rely on the atomic type being a
 *   template class such that the first type is the underlying value type
 * - The Atom parameter allows this API to be compatible with
 *   DeterministicSchedule testing.
 * - atomic_wait_until() does not exist in the linked paper, the version here
 *   is identical to futexWaitUntil() and returns std::cv_status
 */
//  mimic: std::atomic_wait, p1135r0
namespace detail {
namespace atomic_notification {
struct atomic_wait_fn {
 public:
  template <typename Atomic, typename Integer>
  constexpr void operator()(const Atomic* atomic, Integer integer) const {
    tag_invoke(*this, atomic, integer);
  }
};
} // namespace atomic_notification
} // namespace detail
FOLLY_INLINE_VARIABLE constexpr auto atomic_wait =
    detail::atomic_notification::atomic_wait_fn{};

//  mimic: std::atomic_wait_until, p1135r0
namespace detail {
namespace atomic_notification {
struct atomic_wait_until_fn {
 public:
  template <typename Atomic, typename Integer, typename Clock, typename Dur>
  constexpr std::cv_status operator()(
      const Atomic* atomic,
      Integer old,
      const std::chrono::time_point<Clock, Dur>& deadline) const {
    return tag_invoke(*this, atomic, old, deadline);
  }
};
} // namespace atomic_notification
} // namespace detail
FOLLY_INLINE_VARIABLE constexpr auto atomic_wait_until =
    detail::atomic_notification::atomic_wait_until_fn{};

//  mimic: std::atomic_notify_one, p1135r0
namespace detail {
namespace atomic_notification {
struct atomic_notify_one_fn {
 public:
  template <typename Atomic>
  constexpr void operator()(const Atomic* atomic) const {
    tag_invoke(*this, atomic);
  }
};
} // namespace atomic_notification
} // namespace detail
FOLLY_INLINE_VARIABLE constexpr auto atomic_notify_one =
    detail::atomic_notification::atomic_notify_one_fn{};

//  mimic: std::atomic_notify_all, p1135r0
namespace detail {
namespace atomic_notification {
struct atomic_notify_all_fn {
 public:
  template <typename Atomic>
  constexpr void operator()(Atomic* atomic) const {
    tag_invoke(*this, atomic);
  }
};
} // namespace atomic_notification
} // namespace detail
FOLLY_INLINE_VARIABLE constexpr auto atomic_notify_all =
    detail::atomic_notification::atomic_notify_all_fn{};

//  mimic: std::atomic_uint_fast_wait_t, p1135r0
using atomic_uint_fast_wait_t = std::atomic<std::uint32_t>;

} // namespace folly

#include <folly/synchronization/AtomicNotification-inl.h>
