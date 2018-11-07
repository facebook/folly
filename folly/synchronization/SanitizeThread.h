/*
 * Copyright 2013-present Facebook, Inc.
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

#include <folly/Portability.h>

namespace folly {

enum class annotate_rwlock_level : long {
  rdlock = 0,
  wrlock = 1,
};

namespace detail {

void annotate_rwlock_create_impl(
    void const volatile* const addr,
    char const* const f,
    int const l);

void annotate_rwlock_create_static_impl(
    void const volatile* const addr,
    char const* const f,
    int const l);

void annotate_rwlock_destroy_impl(
    void const volatile* const addr,
    char const* const f,
    int const l);

void annotate_rwlock_acquired_impl(
    void const volatile* const addr,
    annotate_rwlock_level const w,
    char const* const f,
    int const l);

void annotate_rwlock_released_impl(
    void const volatile* const addr,
    annotate_rwlock_level const w,
    char const* const f,
    int const l);

void annotate_benign_race_sized_impl(
    const volatile void* addr,
    long size,
    const char* desc,
    const char* f,
    int l);

} // namespace detail

FOLLY_ALWAYS_INLINE static void annotate_rwlock_create(
    void const volatile* const addr,
    char const* const f,
    int const l) {
  if (kIsSanitizeThread) {
    detail::annotate_rwlock_create_impl(addr, f, l);
  }
}

FOLLY_ALWAYS_INLINE static void annotate_rwlock_create_static(
    void const volatile* const addr,
    char const* const f,
    int const l) {
  if (kIsSanitizeThread) {
    detail::annotate_rwlock_create_static_impl(addr, f, l);
  }
}

FOLLY_ALWAYS_INLINE static void annotate_rwlock_destroy(
    void const volatile* const addr,
    char const* const f,
    int const l) {
  if (kIsSanitizeThread) {
    detail::annotate_rwlock_destroy_impl(addr, f, l);
  }
}

FOLLY_ALWAYS_INLINE static void annotate_rwlock_acquired(
    void const volatile* const addr,
    annotate_rwlock_level const w,
    char const* const f,
    int const l) {
  if (kIsSanitizeThread) {
    detail::annotate_rwlock_acquired_impl(addr, w, f, l);
  }
}

FOLLY_ALWAYS_INLINE static void annotate_rwlock_try_acquired(
    void const volatile* const addr,
    annotate_rwlock_level const w,
    bool const result,
    char const* const f,
    int const l) {
  if (result) {
    annotate_rwlock_acquired(addr, w, f, l);
  }
}

FOLLY_ALWAYS_INLINE static void annotate_rwlock_released(
    void const volatile* const addr,
    annotate_rwlock_level const w,
    char const* const f,
    int const l) {
  if (kIsSanitizeThread) {
    detail::annotate_rwlock_released_impl(addr, w, f, l);
  }
}

FOLLY_ALWAYS_INLINE static void annotate_benign_race_sized(
    void const volatile* const addr,
    long const size,
    char const* const desc,
    char const* const f,
    int const l) {
  if (kIsSanitizeThread) {
    detail::annotate_benign_race_sized_impl(addr, size, desc, f, l);
  }
}

} // namespace folly
