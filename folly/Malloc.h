/*
 * Copyright 2015 Facebook, Inc.
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

// Functions to provide smarter use of jemalloc, if jemalloc is being used.
// http://www.canonware.com/download/jemalloc/jemalloc-latest/doc/jemalloc.html

#ifndef FOLLY_MALLOC_H_
#define FOLLY_MALLOC_H_

/**
 * Define various MALLOCX_* macros normally provided by jemalloc.  We define
 * them so that we don't have to include jemalloc.h, in case the program is
 * built without jemalloc support.
 */
#ifndef MALLOCX_LG_ALIGN
#define MALLOCX_LG_ALIGN(la) (la)
#endif
#ifndef MALLOCX_ZERO
#define MALLOCX_ZERO (static_cast<int>(0x40))
#endif

// If using fbstring from libstdc++, then just define stub code
// here to typedef the fbstring type into the folly namespace.
// This provides backwards compatibility for code that explicitly
// includes and uses fbstring.
#if defined(_GLIBCXX_USE_FB) && !defined(_LIBSTDCXX_FBSTRING)

#include <folly/detail/Malloc.h>

#include <string>

namespace folly {
  using std::goodMallocSize;
  using std::jemallocMinInPlaceExpandable;
  using std::usingJEMalloc;
  using std::smartRealloc;
  using std::checkedMalloc;
  using std::checkedCalloc;
  using std::checkedRealloc;
}

#else // !defined(_GLIBCXX_USE_FB) || defined(_LIBSTDCXX_FBSTRING)

#ifdef _LIBSTDCXX_FBSTRING
#pragma GCC system_header

/**
 * Declare *allocx() and mallctl() as weak symbols. These will be provided by
 * jemalloc if we are using jemalloc, or will be NULL if we are using another
 * malloc implementation.
 */
extern "C" void* mallocx(size_t, int)
__attribute__((__weak__));
extern "C" void* rallocx(void*, size_t, int)
__attribute__((__weak__));
extern "C" size_t xallocx(void*, size_t, size_t, int)
__attribute__((__weak__));
extern "C" size_t sallocx(const void*, int)
__attribute__((__weak__));
extern "C" void dallocx(void*, int)
__attribute__((__weak__));
extern "C" size_t nallocx(size_t, int)
__attribute__((__weak__));
extern "C" int mallctl(const char*, void*, size_t*, void*, size_t)
__attribute__((__weak__));

#include <bits/functexcept.h>
#define FOLLY_HAVE_MALLOC_H 1
#else
#include <folly/detail/Malloc.h> /* nolint */
#include <folly/Portability.h>
#endif

// for malloc_usable_size
// NOTE: FreeBSD 9 doesn't have malloc.h.  It's defitions
// are found in stdlib.h.
#if FOLLY_HAVE_MALLOC_H
#include <malloc.h>
#else
#include <stdlib.h>
#endif

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cstring>

#include <new>

#ifdef _LIBSTDCXX_FBSTRING
namespace std _GLIBCXX_VISIBILITY(default) {
_GLIBCXX_BEGIN_NAMESPACE_VERSION
#else
namespace folly {
#endif

bool usingJEMallocSlow();

/**
 * Determine if we are using jemalloc or not.
 */
inline bool usingJEMalloc() {
  // Checking for rallocx != NULL is not sufficient; we may be in a dlopen()ed
  // module that depends on libjemalloc, so rallocx is resolved, but the main
  // program might be using a different memory allocator. Look at the
  // implementation of usingJEMallocSlow() for the (hacky) details.
  static const bool result = usingJEMallocSlow();
  return result;
}

/**
 * For jemalloc's size classes, see
 * http://www.canonware.com/download/jemalloc/jemalloc-latest/doc/jemalloc.html
 */
inline size_t goodMallocSize(size_t minSize) noexcept {
  if (!usingJEMalloc()) {
    // Not using jemalloc - no smarts
    return minSize;
  }
  size_t goodSize;
  if (minSize <= 64) {
    // Choose smallest allocation to be 64 bytes - no tripping over
    // cache line boundaries, and small string optimization takes care
    // of short strings anyway.
    goodSize = 64;
  } else if (minSize <= 512) {
    // Round up to the next multiple of 64; we don't want to trip over
    // cache line boundaries.
    goodSize = (minSize + 63) & ~size_t(63);
  } else {
    // Boundaries between size classes depend on numerious factors, some of
    // which can even be modified at run-time. Determine the good allocation
    // size by calling nallocx() directly.
    goodSize = nallocx(minSize, 0);
  }
  assert(nallocx(goodSize, 0) == goodSize);
  return goodSize;
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
  if (!p) std::__throw_bad_alloc();
  return p;
}

inline void* checkedCalloc(size_t n, size_t size) {
  void* p = calloc(n, size);
  if (!p) std::__throw_bad_alloc();
  return p;
}

inline void* checkedRealloc(void* ptr, size_t size) {
  void* p = realloc(ptr, size);
  if (!p) std::__throw_bad_alloc();
  return p;
}

/**
 * This function tries to reallocate a buffer of which only the first
 * currentSize bytes are used. The problem with using realloc is that
 * if currentSize is relatively small _and_ if realloc decides it
 * needs to move the memory chunk to a new buffer, then realloc ends
 * up copying data that is not used. It's impossible to hook into
 * GNU's malloc to figure whether expansion will occur in-place or as
 * a malloc-copy-free troika. (If an expand_in_place primitive would
 * be available, smartRealloc would use it.) As things stand, this
 * routine just tries to call realloc() (thus benefitting of potential
 * copy-free coalescing) unless there's too much slack memory.
 */
inline void* smartRealloc(void* p,
                          const size_t currentSize,
                          const size_t currentCapacity,
                          const size_t newCapacity) {
  assert(p);
  assert(currentSize <= currentCapacity &&
         currentCapacity < newCapacity);

  if (usingJEMalloc()) {
    // using jemalloc's API. Don't forget that jemalloc can never grow
    // in place blocks smaller than 4096 bytes.
    //
    // NB: newCapacity may not be precisely equal to a jemalloc size class,
    // i.e. newCapacity is not guaranteed to be the result of a
    // goodMallocSize() call, therefore xallocx() may return more than
    // newCapacity bytes of space.  Use >= rather than == to check whether
    // xallocx() successfully expanded in place.
    if (currentCapacity >= jemallocMinInPlaceExpandable &&
        xallocx(p, newCapacity, 0, 0) >= newCapacity) {
      // Managed to expand in place
      return p;
    }
    // Cannot expand; must move
    auto const result = checkedMalloc(newCapacity);
    std::memcpy(result, p, currentSize);
    free(p);
    return result;
  }

  // No jemalloc no honey
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

#ifdef _LIBSTDCXX_FBSTRING
_GLIBCXX_END_NAMESPACE_VERSION
#endif

} // folly

#endif // !defined(_GLIBCXX_USE_FB) || defined(_LIBSTDCXX_FBSTRING)

#endif // FOLLY_MALLOC_H_
