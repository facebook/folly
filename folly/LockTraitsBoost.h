/*
 * Copyright 2016 Facebook, Inc.
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

/**
 * This file contains LockTraits specializations for boost mutex types.
 *
 * These need to be specialized simply due to the fact that the timed
 * methods take boost::chrono arguments instead of std::chrono.
 */
#pragma once

#include <folly/LockTraits.h>

#if FOLLY_LOCK_TRAITS_HAVE_TIMED_MUTEXES

#include <boost/thread.hpp>

namespace folly {

/**
 * LockTraits specialization for boost::shared_mutex
 */
template <>
struct LockTraits<boost::shared_mutex>
    : public folly::detail::LockTraitsSharedBase<boost::shared_mutex> {
  static constexpr bool is_shared = true;
  static constexpr bool has_timed_lock = true;

  static bool try_lock_for(
      boost::shared_mutex& mutex,
      std::chrono::milliseconds timeout) {
    // Convert the std::chrono argument to boost::chrono
    return mutex.try_lock_for(boost::chrono::milliseconds(timeout.count()));
  }

  static bool try_lock_shared_for(
      boost::shared_mutex& mutex,
      std::chrono::milliseconds timeout) {
    // Convert the std::chrono argument to boost::chrono
    return mutex.try_lock_shared_for(
        boost::chrono::milliseconds(timeout.count()));
  }
};

/**
 * LockTraits specialization for boost::timed_mutex
 */
template <>
struct LockTraits<boost::timed_mutex>
    : public folly::detail::LockTraitsUniqueBase<boost::timed_mutex> {
  static constexpr bool is_shared = false;
  static constexpr bool has_timed_lock = true;

  static bool try_lock_for(
      boost::timed_mutex& mutex,
      std::chrono::milliseconds timeout) {
    // Convert the std::chrono argument to boost::chrono
    return mutex.try_lock_for(boost::chrono::milliseconds(timeout.count()));
  }
};

/**
 * LockTraits specialization for boost::recursive_timed_mutex
 */
template <>
struct LockTraits<boost::recursive_timed_mutex>
    : public folly::detail::LockTraitsUniqueBase<boost::recursive_timed_mutex> {
  static constexpr bool is_shared = false;
  static constexpr bool has_timed_lock = true;

  static bool try_lock_for(
      boost::recursive_timed_mutex& mutex,
      std::chrono::milliseconds timeout) {
    // Convert the std::chrono argument to boost::chrono
    return mutex.try_lock_for(boost::chrono::milliseconds(timeout.count()));
  }
};
} // folly

#endif // FOLLY_LOCK_TRAITS_HAVE_TIMED_MUTEXES
