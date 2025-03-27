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

//
// Docs: https://fburl.com/fbcref_malloc
//

// Functions to provide smarter use of jemalloc, if jemalloc is being used.
// http://www.canonware.com/download/jemalloc/jemalloc-latest/doc/jemalloc.html

#pragma once

#include <stdexcept>

#include <folly/Portability.h>
#include <folly/lang/Exception.h>
#include <folly/portability/Malloc.h>

/**
 * Define various MALLOCX_* macros normally provided by jemalloc.  We define
 * them so that we don't have to include jemalloc.h, in case the program is
 * built without jemalloc support.
 *
 * @file memory/Malloc.h
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

namespace folly {

namespace detail {

#if FOLLY_CPLUSPLUS >= 202002L
// Faster "static bool" using a tri-state atomic. The flag is identified by the
// Initializer functor argument.
template <class Initializer>
class FastStaticBool {
 public:
  // std::memory_order_relaxed can be used if it is not necessary to synchronize
  // with the invocation of the initializer, only the result is used.
  FOLLY_ALWAYS_INLINE static bool get(
      std::memory_order mo = std::memory_order_acquire) noexcept {
    auto f = flag_.load(mo);
    if (FOLLY_LIKELY(f != 0)) {
      return f > 0;
    }
    return getSlow(); // Tail call.
  }

 private:
  [[FOLLY_ATTR_GNU_COLD]] FOLLY_NOINLINE FOLLY_EXPORT static bool
  getSlow() noexcept {
    static bool rv = [] {
      auto v = Initializer{}();
      flag_.store(v ? 1 : -1, std::memory_order_release);
      return v;
    }();
    return rv;
  }

  static std::atomic<signed char> flag_;
};

template <class Initializer>
constinit std::atomic<signed char> FastStaticBool<Initializer>::flag_{};
#else // FOLLY_CPLUSPLUS >= 202002L
// Fallback on native static if std::atomic does not have a constexpr
// constructor.
template <class Initializer>
class FastStaticBool {
 public:
  FOLLY_ALWAYS_INLINE static bool get(
      std::memory_order = std::memory_order_acquire) noexcept {
    static const bool rv = Initializer{}();
    return rv;
  }
};
#endif

} // namespace detail

#if defined(__GNUC__)
// This is for checked malloc-like functions (returns non-null pointer
// which cannot alias any outstanding pointer).
#define FOLLY_MALLOC_CHECKED_MALLOC \
  __attribute__((__returns_nonnull__, __malloc__))
#else
#define FOLLY_MALLOC_CHECKED_MALLOC
#endif

/**
 * @brief Determine if we are using JEMalloc or not.
 *
 * @methodset Malloc checks
 *
 * @return bool
 */
#if defined(FOLLY_ASSUME_NO_JEMALLOC) || defined(FOLLY_SANITIZE)
#define FOLLY_CONSTANT_USING_JE_MALLOC 1
inline bool usingJEMalloc() noexcept {
  return false;
}
#elif defined(USE_JEMALLOC) && !defined(FOLLY_SANITIZE)
#define FOLLY_CONSTANT_USING_JE_MALLOC 1
inline bool usingJEMalloc() noexcept {
  return true;
}
#else
#define FOLLY_CONSTANT_USING_JE_MALLOC 0
FOLLY_EXPORT inline bool usingJEMalloc() noexcept {
  struct Initializer {
    bool operator()() const {
      // Checking for rallocx != nullptr is not sufficient; we may be in a
      // dlopen()ed module that depends on libjemalloc, so rallocx is resolved,
      // but the main program might be using a different memory allocator. How
      // do we determine that we're using jemalloc? In the hackiest way
      // possible. We allocate memory using malloc() and see if the per-thread
      // counter of allocated memory increases. This makes me feel dirty inside.
      // Also note that this requires jemalloc to have been compiled with
      // --enable-stats.

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
  };
  return detail::FastStaticBool<Initializer>::get(std::memory_order_relaxed);
}
#endif

/**
 * @brief Gets the named property.
 *
 * @param name name of the property.
 * @param out size is populated by the function.
 *
 * @return bool
 */
inline bool getTCMallocNumericProperty(const char* name, size_t* out) noexcept {
  return MallocExtension_Internal_GetNumericProperty(name, strlen(name), out);
}

/**
 * @brief Determine if we are using TCMalloc or not.
 *
 * @methodset Malloc checks
 *
 * @return bool
 */
#if defined(FOLLY_ASSUME_NO_TCMALLOC) || defined(FOLLY_SANITIZE)
#define FOLLY_CONSTANT_USING_TC_MALLOC 1
inline bool usingTCMalloc() noexcept {
  return false;
}
#elif defined(USE_TCMALLOC) && !defined(FOLLY_SANITIZE)
#define FOLLY_CONSTANT_USING_TC_MALLOC 1
inline bool usingTCMalloc() noexcept {
  return true;
}
#else
#define FOLLY_CONSTANT_USING_TC_MALLOC 0
FOLLY_EXPORT inline bool usingTCMalloc() noexcept {
  struct Initializer {
    bool operator()() const {
      // See comment in usingJEMalloc().
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
  };
  return detail::FastStaticBool<Initializer>::get(std::memory_order_relaxed);
}
#endif

namespace detail {
FOLLY_EXPORT inline bool usingJEMallocOrTCMalloc() noexcept {
  struct Initializer {
    bool operator()() const { return usingJEMalloc() || usingTCMalloc(); }
  };
#if FOLLY_CONSTANT_USING_JE_MALLOC && FOLLY_CONSTANT_USING_TC_MALLOC
  return Initializer{}();
#else
  return detail::FastStaticBool<Initializer>::get(std::memory_order_relaxed);
#endif
}
} // namespace detail

/**
 * @brief Return whether sdallocx() is supported by the current allocator.
 *
 * @return bool
 */
inline bool canSdallocx() noexcept {
  return detail::usingJEMallocOrTCMalloc();
}

/**
 * @brief Return whether nallocx() is supported by the current allocator.
 *
 * @return bool
 */
inline bool canNallocx() noexcept {
  return detail::usingJEMallocOrTCMalloc();
}

/**
 * @brief Simple wrapper around nallocx
 *
 * The nallocx function allocates no memory, but it performs the same size
 * computation as the malloc function, and returns the real size of the
 * allocation that would result from the equivalent malloc function call.
 *
 * https://www.unix.com/man-page/freebsd/3/nallocx/
 *
 * @param minSize Requested size for allocation
 * @return size_t
 */
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
 * @brief Trivial wrapper around malloc that check for allocation
 * failure and throw std::bad_alloc in that case.
 *
 * @methodset Allocation Wrappers
 *
 * @param size size of allocation
 *
 * @return void* pointer to allocated buffer
 */
inline void* checkedMalloc(size_t size) {
  void* p = malloc(size);
  if (!p) {
    throw_exception<std::bad_alloc>();
  }
  return p;
}

/**
 * @brief Trivial wrapper around calloc that check for allocation
 * failure and throw std::bad_alloc in that case.
 *
 * @methodset Allocation Wrappers
 *
 * @param n Number of elements
 * @param size Size of each element
 *
 * @return void* pointer to allocated buffer
 */
inline void* checkedCalloc(size_t n, size_t size) {
  void* p = calloc(n, size);
  if (!p) {
    throw_exception<std::bad_alloc>();
  }
  return p;
}

/**
 * @brief Trivial wrapper around realloc that check for allocation
 * failure and throw std::bad_alloc in that case.
 *
 * @methodset Allocation Wrappers
 *
 * @param ptr pointer to start of buffer
 * @param size size to reallocate starting from ptr
 *
 * @return pointer to reallocated buffer
 */
inline void* checkedRealloc(void* ptr, size_t size) {
  void* p = realloc(ptr, size);
  if (!p) {
    throw_exception<std::bad_alloc>();
  }
  return p;
}

/**
 * @brief Frees's memory using sdallocx if possible
 *
 * The sdallocx function deallocates memory allocated by malloc or memalign.  It
 * takes a size parameter to pass the original allocation size.
 * The default weak implementation calls free(), but TCMalloc overrides it and
 * uses the size to improve deallocation performance.
 *
 * @param ptr Pointer to the buffer to free
 * @param size Size to free
 */
inline void sizedFree(void* ptr, size_t size) {
  if (canSdallocx()) {
    sdallocx(ptr, size, 0);
  } else {
    free(ptr);
  }
}

/**
 * @brief Reallocs if there is less slack in the buffer, else performs
 * malloc-copy-free.
 *
 * This function tries to reallocate a buffer of which only the first
 * currentSize bytes are used. The problem with using realloc is that
 * if currentSize is relatively small _and_ if realloc decides it
 * needs to move the memory chunk to a new buffer, then realloc ends
 * up copying data that is not used. It's generally not a win to try
 * to hook in to realloc() behavior to avoid copies - at least in
 * jemalloc, realloc() almost always ends up doing a copy, because
 * there is little fragmentation / slack space to take advantage of.
 *
 * @param p Pointer to start of buffer
 * @param currentSize Current used size
 * @param currentCapacity Capacity of buffer
 * @param newCapacity New capacity for the buffer
 *
 * @return pointer to realloc'ed buffer
 */
FOLLY_MALLOC_CHECKED_MALLOC FOLLY_NOINLINE inline void* smartRealloc(
    void* p,
    const size_t currentSize,
    const size_t currentCapacity,
    const size_t newCapacity) {
  assert(p);
  assert(currentSize <= currentCapacity && currentCapacity < newCapacity);

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

/**
 * @brief Return value of MALLCTL_ARENAS_ALL defined in jemalloc's header.
 *
 * Technically doesn't require that the system is using jemalloc, but jemalloc
 * header must be included, if it is not, then call to this function will
 * throw std::logic_error exception.
 *
 * Usage example:
 *
 * if (folly::usingJEMalloc()) {
 *   static const std::string kCmd = fmt::format("arena.{}.purge",
 *       folly::getJEMallocMallctlArenasAll());
 *   folly::mallctlCall(kCmd.c_str());
 * }
 *
 * @return size_t
 */
inline size_t getJEMallocMallctlArenasAll() {
#if FOLLY_HAS_JEMALLOC_DEFS
  // Code below will not compile if `MALLCTL_ARENAS_ALL` is not defined, which
  // might happen if jemalloc renames and/or removes this macro.
  return MALLCTL_ARENAS_ALL;
#else
  throw_exception<std::logic_error>(
      "getJEMallocMallctlArenasAll: jemalloc header was not included");
#endif
}

} // namespace folly
