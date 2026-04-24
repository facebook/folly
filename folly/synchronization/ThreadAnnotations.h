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

// Portable macros for Clang's thread safety analysis, plus a capability-
// annotated `folly::AnnotatedMutex` and matching `folly::AnnotatedLockGuard`.
//
// Clang's `-Wthread-safety` analysis only fires on mutex types that carry the
// `capability("mutex")` attribute. `std::mutex` (and folly's existing mutex
// types) do not, so direct uses of `FOLLY_TS_GUARDED_BY(myMutex)` /
// `FOLLY_TS_EXCLUDES(myMutex)` are silently rejected with a
// `-Wthread-safety-attributes` diagnostic. `folly::AnnotatedMutex` wraps
// `std::mutex` and adds the necessary attribute; `folly::AnnotatedLockGuard`
// is the matching scoped lock (`std::lock_guard` is not capability-aware
// either, so it can't be used with `AnnotatedMutex` for static analysis).
//
// The macros expand to no-ops on compilers that don't support the
// annotations (MSVC, GCC), so the wrappers are zero-cost there.
//
// Usage:
//   folly::AnnotatedMutex mu_;
//   int data_ FOLLY_TS_GUARDED_BY(mu_);
//
//   void write(int v) FOLLY_TS_REQUIRES(!mu_) {
//     folly::AnnotatedLockGuard lock(mu_);
//     data_ = v;  // OK: mu_ is held
//   }

#pragma once

#include <mutex>

#if defined(__clang__)
#define FOLLY_TS_CAPABILITY(x) __attribute__((capability(x)))
#define FOLLY_TS_SCOPED_CAPABILITY __attribute__((scoped_lockable))
#define FOLLY_TS_GUARDED_BY(x) __attribute__((guarded_by(x)))
#define FOLLY_TS_PT_GUARDED_BY(x) __attribute__((pt_guarded_by(x)))
#define FOLLY_TS_REQUIRES(...) __attribute__((requires_capability(__VA_ARGS__)))
#define FOLLY_TS_REQUIRES_SHARED(...) \
  __attribute__((requires_shared_capability(__VA_ARGS__)))
#define FOLLY_TS_ACQUIRE(...) __attribute__((acquire_capability(__VA_ARGS__)))
#define FOLLY_TS_ACQUIRE_SHARED(...) \
  __attribute__((acquire_shared_capability(__VA_ARGS__)))
#define FOLLY_TS_RELEASE(...) __attribute__((release_capability(__VA_ARGS__)))
#define FOLLY_TS_RELEASE_SHARED(...) \
  __attribute__((release_shared_capability(__VA_ARGS__)))
#define FOLLY_TS_TRY_ACQUIRE(...) \
  __attribute__((try_acquire_capability(__VA_ARGS__)))
#define FOLLY_TS_TRY_ACQUIRE_SHARED(...) \
  __attribute__((try_acquire_shared_capability(__VA_ARGS__)))
#define FOLLY_TS_EXCLUDES(...) __attribute__((locks_excluded(__VA_ARGS__)))
#define FOLLY_TS_ASSERT_CAPABILITY(x) __attribute__((assert_capability(x)))
#define FOLLY_TS_NO_THREAD_SAFETY_ANALYSIS \
  __attribute__((no_thread_safety_analysis))
#else
#define FOLLY_TS_CAPABILITY(x)
#define FOLLY_TS_SCOPED_CAPABILITY
#define FOLLY_TS_GUARDED_BY(x)
#define FOLLY_TS_PT_GUARDED_BY(x)
#define FOLLY_TS_REQUIRES(...)
#define FOLLY_TS_REQUIRES_SHARED(...)
#define FOLLY_TS_ACQUIRE(...)
#define FOLLY_TS_ACQUIRE_SHARED(...)
#define FOLLY_TS_RELEASE(...)
#define FOLLY_TS_RELEASE_SHARED(...)
#define FOLLY_TS_TRY_ACQUIRE(...)
#define FOLLY_TS_TRY_ACQUIRE_SHARED(...)
#define FOLLY_TS_EXCLUDES(...)
#define FOLLY_TS_ASSERT_CAPABILITY(x)
#define FOLLY_TS_NO_THREAD_SAFETY_ANALYSIS
#endif

namespace folly {

#if defined(__clang__)

// `std::mutex` wrapper carrying the `capability("mutex")` attribute so
// Clang's `-Wthread-safety` analysis can verify `FOLLY_TS_GUARDED_BY` /
// `FOLLY_TS_EXCLUDES` claims involving it. Behaves exactly like `std::mutex`
// at runtime.
class FOLLY_TS_CAPABILITY("mutex") AnnotatedMutex {
 public:
  AnnotatedMutex() = default;
  ~AnnotatedMutex() = default;
  AnnotatedMutex(const AnnotatedMutex&) = delete;
  AnnotatedMutex& operator=(const AnnotatedMutex&) = delete;
  AnnotatedMutex(AnnotatedMutex&&) = delete;
  AnnotatedMutex& operator=(AnnotatedMutex&&) = delete;

  void lock() FOLLY_TS_ACQUIRE() { mu_.lock(); }
  void unlock() FOLLY_TS_RELEASE() { mu_.unlock(); }
  bool try_lock() FOLLY_TS_TRY_ACQUIRE(true) { return mu_.try_lock(); }

  // Enables FOLLY_TS_REQUIRES(!mu) syntax for negative capabilities.
  const AnnotatedMutex& operator!() const { return *this; }

 private:
  std::mutex mu_;
};

// Scoped lock for any capability-annotated mutex. `std::lock_guard` cannot
// be used in its place because it lacks the `scoped_lockable` attribute, so
// the analyzer would not understand that the wrapped capability is held
// inside the scope. Templated so it works with `AnnotatedMutex` today and
// with any future capability-annotated mutex (e.g. an annotated
// `folly::SharedMutex`) without needing a parallel guard per type. Use CTAD:
//   folly::AnnotatedLockGuard lock(myMutex);
template <typename Mutex>
class FOLLY_TS_SCOPED_CAPABILITY AnnotatedLockGuard {
 public:
  explicit AnnotatedLockGuard(Mutex& mu) FOLLY_TS_ACQUIRE(mu) : mu_(mu) {
    mu_.lock();
  }
  ~AnnotatedLockGuard() FOLLY_TS_RELEASE() { mu_.unlock(); }

  AnnotatedLockGuard(const AnnotatedLockGuard&) = delete;
  AnnotatedLockGuard& operator=(const AnnotatedLockGuard&) = delete;
  AnnotatedLockGuard(AnnotatedLockGuard&&) = delete;
  AnnotatedLockGuard& operator=(AnnotatedLockGuard&&) = delete;

 private:
  Mutex& mu_;
};

#else

// On compilers without `-Wthread-safety` support (MSVC, GCC) the wrappers
// would only add an extra indirection with no analysis benefit, so collapse
// them to the plain standard-library types.
using AnnotatedMutex = std::mutex;
template <typename Mutex>
using AnnotatedLockGuard = std::lock_guard<Mutex>;

#endif

} // namespace folly
