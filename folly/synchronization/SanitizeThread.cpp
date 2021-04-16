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

#include <folly/synchronization/SanitizeThread.h>

#include <folly/lang/Extern.h>

// abseil uses size_t for size params while other FB code and libraries use
// long, so it is helpful to keep these declarations out of widely-included
// header files.

extern "C" void AnnotateRWLockCreate(
    const char* f, int l, const volatile void* addr);

extern "C" void AnnotateRWLockCreateStatic(
    const char* f, int l, const volatile void* addr);

extern "C" void AnnotateRWLockDestroy(
    const char* f, int l, const volatile void* addr);

extern "C" void AnnotateRWLockAcquired(
    const char* f, int l, const volatile void* addr, long w);

extern "C" void AnnotateRWLockReleased(
    const char* f, int l, const volatile void* addr, long w);

extern "C" void AnnotateBenignRaceSized(
    const char* f,
    int l,
    const volatile void* addr,
    long size,
    const char* desc);

extern "C" void AnnotateIgnoreReadsBegin(const char* f, int l);

extern "C" void AnnotateIgnoreReadsEnd(const char* f, int l);

extern "C" void AnnotateIgnoreWritesBegin(const char* f, int l);

extern "C" void AnnotateIgnoreWritesEnd(const char* f, int l);

extern "C" void AnnotateIgnoreSyncBegin(const char* f, int l);

extern "C" void AnnotateIgnoreSyncEnd(const char* f, int l);

namespace folly {
namespace detail {

FOLLY_CREATE_EXTERN_ACCESSOR(
    annotate_rwlock_create_access_v, AnnotateRWLockCreate);

FOLLY_CREATE_EXTERN_ACCESSOR(
    annotate_rwlock_create_static_access_v, AnnotateRWLockCreateStatic);

FOLLY_CREATE_EXTERN_ACCESSOR(
    annotate_rwlock_destroy_access_v, AnnotateRWLockDestroy);

FOLLY_CREATE_EXTERN_ACCESSOR(
    annotate_rwlock_acquired_access_v, AnnotateRWLockAcquired);

FOLLY_CREATE_EXTERN_ACCESSOR(
    annotate_rwlock_released_access_v, AnnotateRWLockReleased);

FOLLY_CREATE_EXTERN_ACCESSOR(
    annotate_benign_race_sized_access_v, AnnotateBenignRaceSized);

FOLLY_CREATE_EXTERN_ACCESSOR(
    annotate_ignore_reads_begin_access_v, AnnotateIgnoreReadsBegin);

FOLLY_CREATE_EXTERN_ACCESSOR(
    annotate_ignore_reads_end_access_v, AnnotateIgnoreReadsEnd);

FOLLY_CREATE_EXTERN_ACCESSOR(
    annotate_ignore_writes_begin_access_v, AnnotateIgnoreWritesBegin);

FOLLY_CREATE_EXTERN_ACCESSOR(
    annotate_ignore_writes_end_access_v, AnnotateIgnoreWritesEnd);

FOLLY_CREATE_EXTERN_ACCESSOR(
    annotate_ignore_sync_begin_access_v, AnnotateIgnoreSyncBegin);

FOLLY_CREATE_EXTERN_ACCESSOR(
    annotate_ignore_sync_end_access_v, AnnotateIgnoreSyncEnd);

void annotate_rwlock_create_impl(
    void const volatile* const addr, char const* const f, int const l) {
  constexpr auto fun = annotate_rwlock_create_access_v<kIsSanitizeThread>;
  return fun ? fun(f, l, addr) : void();
}

void annotate_rwlock_create_static_impl(
    void const volatile* const addr, char const* const f, int const l) {
  constexpr auto fun =
      annotate_rwlock_create_static_access_v<kIsSanitizeThread>;
  return fun ? fun(f, l, addr) : void();
}

void annotate_rwlock_destroy_impl(
    void const volatile* const addr, char const* const f, int const l) {
  constexpr auto fun = annotate_rwlock_destroy_access_v<kIsSanitizeThread>;
  return fun ? fun(f, l, addr) : void();
}

void annotate_rwlock_acquired_impl(
    void const volatile* const addr,
    annotate_rwlock_level const w,
    char const* const f,
    int const l) {
  constexpr auto fun = annotate_rwlock_acquired_access_v<kIsSanitizeThread>;
  return fun ? fun(f, l, addr, static_cast<long>(w)) : void();
}

void annotate_rwlock_try_acquired_impl(
    void const volatile* const addr,
    annotate_rwlock_level const w,
    bool const result,
    char const* const f,
    int const l) {
  if (result) {
    annotate_rwlock_acquired(addr, w, f, l);
  }
}

void annotate_rwlock_released_impl(
    void const volatile* const addr,
    annotate_rwlock_level const w,
    char const* const f,
    int const l) {
  constexpr auto fun = annotate_rwlock_released_access_v<kIsSanitizeThread>;
  return fun ? fun(f, l, addr, static_cast<long>(w)) : void();
}

void annotate_benign_race_sized_impl(
    const volatile void* addr,
    long size,
    const char* desc,
    const char* f,
    int l) {
  constexpr auto fun = annotate_benign_race_sized_access_v<kIsSanitizeThread>;
  return fun ? fun(f, l, addr, size, desc) : void();
}

void annotate_ignore_reads_begin_impl(const char* f, int l) {
  constexpr auto fun = annotate_ignore_reads_begin_access_v<kIsSanitizeThread>;
  return fun ? fun(f, l) : void();
}

void annotate_ignore_reads_end_impl(const char* f, int l) {
  constexpr auto fun = annotate_ignore_reads_end_access_v<kIsSanitizeThread>;
  return fun ? fun(f, l) : void();
}

void annotate_ignore_writes_begin_impl(const char* f, int l) {
  constexpr auto fun = annotate_ignore_writes_begin_access_v<kIsSanitizeThread>;
  return fun ? fun(f, l) : void();
}

void annotate_ignore_writes_end_impl(const char* f, int l) {
  constexpr auto fun = annotate_ignore_writes_end_access_v<kIsSanitizeThread>;
  return fun ? fun(f, l) : void();
}

void annotate_ignore_sync_begin_impl(const char* f, int l) {
  constexpr auto fun = annotate_ignore_sync_begin_access_v<kIsSanitizeThread>;
  return fun ? fun(f, l) : void();
}

void annotate_ignore_sync_end_impl(const char* f, int l) {
  constexpr auto fun = annotate_ignore_sync_end_access_v<kIsSanitizeThread>;
  return fun ? fun(f, l) : void();
}
} // namespace detail
} // namespace folly
