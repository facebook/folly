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

// Functions to provide smarter use of jemalloc, if jemalloc is being used.
// http://www.canonware.com/download/jemalloc/jemalloc-latest/doc/jemalloc.html

#pragma once

#include <folly/CPortability.h>
#include <folly/portability/Config.h>
#include <folly/portability/Malloc.h>

/**
 * Define various MALLOCX_* macros normally provided by jemalloc.  We define
 * them so that we don't have to include jemalloc.h, in case the program is
 * built without jemalloc support.
 */
#if (defined(USE_JEMALLOC) || defined(FOLLY_USE_JEMALLOC)) && \
    !defined(FOLLY_SANITIZE)
// We have JEMalloc, so use it.
#else
#ifndef MALLOCX_LG_ALIGN
#define MALLOCX_LG_ALIGN(la) (la)
#endif
#ifndef MALLOCX_ZERO
#define MALLOCX_ZERO (static_cast<int>(0x40))
#endif
#endif

#include <folly/lang/Exception.h> /* nolint */
#include <folly/memory/detail/MallocImpl.h> /* nolint */

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <atomic>
#include <new>

// clang-format off

namespace folly {

#if defined(__GNUC__)
// This is for checked malloc-like functions (returns non-null pointer
// which cannot alias any outstanding pointer).
#define FOLLY_MALLOC_CHECKED_MALLOC \
  __attribute__((__returns_nonnull__, __malloc__))
#else
#define FOLLY_MALLOC_CHECKED_MALLOC
#endif

/**
 * Determine if we are using jemalloc or not.
 */
#if defined(FOLLY_ASSUME_NO_JEMALLOC) || defined(FOLLY_SANITIZE)
  inline bool usingJEMalloc() noexcept {
    return false;
  }
#elif defined(USE_JEMALLOC) && !defined(FOLLY_SANITIZE)
  inline bool usingJEMalloc() noexcept {
    return true;
  }
#else
FOLLY_NOINLINE inline bool usingJEMalloc() noexcept {
  // Checking for rallocx != nullptr is not sufficient; we may be in a
  // dlopen()ed module that depends on libjemalloc, so rallocx is resolved, but
  // the main program might be using a different memory allocator.
  // How do we determine that we're using jemalloc? In the hackiest
  // way possible. We allocate memory using malloc() and see if the
  // per-thread counter of allocated memory increases. This makes me
  // feel dirty inside. Also note that this requires jemalloc to have
  // been compiled with --enable-stats.
  static const bool result = []() noexcept {
    // Some platforms (*cough* OSX *cough*) require weak symbol checks to be
    // in the form if (mallctl != nullptr). Not if (mallctl) or if (!mallctl)
    // (!!). http://goo.gl/xpmctm
    if (mallocx == nullptr || rallocx == nullptr || xallocx == nullptr ||
        sallocx == nullptr || dallocx == nullptr || sdallocx == nullptr ||
        nallocx == nullptr || mallctl == nullptr ||
        mallctlnametomib == nullptr || mallctlbymib == nullptr) {
      return false;
    }

    // "volatile" because gcc optimizes out the reads from *counter, because
    // it "knows" malloc doesn't modify global state...
    /* nolint */ volatile uint64_t* counter;
    size_t counterLen = sizeof(uint64_t*);

    if (mallctl(
            "thread.allocatedp",
            static_cast<void*>(&counter),
            &counterLen,
            nullptr,
            0) != 0) {
      return false;
    }

    if (counterLen != sizeof(uint64_t*)) {
      return false;
    }

    uint64_t origAllocated = *counter;

    static void* volatile ptr = malloc(1);
    if (!ptr) {
      // wtf, failing to allocate 1 byte
      return false;
    }

    free(ptr);

    return (origAllocated != *counter);
  }
  ();

  return result;
}
#endif

inline bool getTCMallocNumericProperty(const char* name, size_t* out) noexcept {
  return MallocExtension_Internal_GetNumericProperty(name, strlen(name), out);
}

#if defined(FOLLY_ASSUME_NO_TCMALLOC) || defined(FOLLY_SANITIZE)
  inline bool usingTCMalloc() noexcept {
    return false;
  }
#elif defined(USE_TCMALLOC) && !defined(FOLLY_SANITIZE)
  inline bool usingTCMalloc() noexcept {
    return true;
  }
#else
FOLLY_NOINLINE inline bool usingTCMalloc() noexcept {
  static const bool result = []() noexcept {
    // Some platforms (*cough* OSX *cough*) require weak symbol checks to be
    // in the form if (mallctl != nullptr). Not if (mallctl) or if (!mallctl)
    // (!!). http://goo.gl/xpmctm
    if (MallocExtension_Internal_GetNumericProperty == nullptr ||
        sdallocx == nullptr || nallocx == nullptr) {
      return false;
    }
    static const char kAllocBytes[] = "generic.current_allocated_bytes";

    size_t before_bytes = 0;
    getTCMallocNumericProperty(kAllocBytes, &before_bytes);

    static void* volatile ptr = malloc(1);
    if (!ptr) {
      // wtf, failing to allocate 1 byte
      return false;
    }

    size_t after_bytes = 0;
    getTCMallocNumericProperty(kAllocBytes, &after_bytes);
    
    free(ptr);

    return (before_bytes != after_bytes);
  }
  ();

  return result;
}
#endif

FOLLY_NOINLINE inline bool canSdallocx() noexcept {
  static bool rv = usingJEMalloc() || usingTCMalloc();
  return rv;
}

FOLLY_NOINLINE inline bool canNallocx() noexcept {
  static bool rv = usingJEMalloc() || usingTCMalloc();
  return rv;
}

inline size_t goodMallocSize(size_t minSize) noexcept {
  if (minSize == 0) {
    return 0;
  }

  if (!canNallocx()) {
    // No nallocx - no smarts
    return minSize;
  }

  // nallocx returns 0 if minSize can't succeed, but 0 is not actually
  // a goodMallocSize if you want minSize
  auto rv = nallocx(minSize, 0);
  return rv ? rv : minSize;
}

// We always request "good" sizes for allocation, so jemalloc can
// never grow in place small blocks; they're already occupied to the
// brim.  Blocks larger than or equal to 4096 bytes can in fact be
// expanded in place, and this constant reflects that.
static const size_t jemallocMinInPlaceExpandable = 4096;

/**
 * Trivial wrappers around malloc, calloc, realloc that check for allocation
 * failure and throw std::bad_alloc in that case.
 */
inline void* checkedMalloc(size_t size) {
  void* p = malloc(size);
  if (!p) {
    throw_exception<std::bad_alloc>();
  }
  return p;
}

inline void* checkedCalloc(size_t n, size_t size) {
  void* p = calloc(n, size);
  if (!p) {
    throw_exception<std::bad_alloc>();
  }
  return p;
}

inline void* checkedRealloc(void* ptr, size_t size) {
  void* p = realloc(ptr, size);
  if (!p) {
    throw_exception<std::bad_alloc>();
  }
  return p;
}

inline void sizedFree(void* ptr, size_t size) {
  if (canSdallocx()) {
    sdallocx(ptr, size, 0);
  } else {
    free(ptr);
  }
}

/**
 * This function tries to reallocate a buffer of which only the first
 * currentSize bytes are used. The problem with using realloc is that
 * if currentSize is relatively small _and_ if realloc decides it
 * needs to move the memory chunk to a new buffer, then realloc ends
 * up copying data that is not used. It's generally not a win to try
 * to hook in to realloc() behavior to avoid copies - at least in
 * jemalloc, realloc() almost always ends up doing a copy, because
 * there is little fragmentation / slack space to take advantage of.
 */
FOLLY_MALLOC_CHECKED_MALLOC FOLLY_NOINLINE inline void* smartRealloc(
    void* p,
    const size_t currentSize,
    const size_t currentCapacity,
    const size_t newCapacity) {
  assert(p);
  assert(currentSize <= currentCapacity &&
         currentCapacity < newCapacity);

  auto const slack = currentCapacity - currentSize;
  if (slack * 2 > currentSize) {
    // Too much slack, malloc-copy-free cycle:
    auto const result = checkedMalloc(newCapacity);
    std::memcpy(result, p, currentSize);
    free(p);
    return result;
  }
  // If there's not too much slack, we realloc in hope of coalescing
  return checkedRealloc(p, newCapacity);
}

} // namespace folly

// clang-format on
