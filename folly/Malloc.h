/*
 * Copyright 2013 Facebook, Inc.
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

// If using fbstring from libstdc++, then just define stub code
// here to typedef the fbstring type into the folly namespace.
// This provides backwards compatibility for code that explicitly
// includes and uses fbstring.
#if defined(_GLIBCXX_USE_FB) && !defined(_LIBSTDCXX_FBSTRING)

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
#define FOLLY_HAVE_MALLOC_H 1
#else
#include "folly/Portability.h"
#endif

// for malloc_usable_size
// NOTE: FreeBSD 9 doesn't have malloc.h.  It's defitions
// are found in stdlib.h.
#ifdef FOLLY_HAVE_MALLOC_H
#include <malloc.h>
#else
#include <stdlib.h>
#endif

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cstring>

#include <new>

#include <bits/functexcept.h>

/**
 * Define various ALLOCM_* macros normally provided by jemalloc.  We define
 * them so that we don't have to include jemalloc.h, in case the program is
 * built without jemalloc support.
 */
#ifndef ALLOCM_SUCCESS

#define ALLOCM_SUCCESS 0
#define ALLOCM_ERR_OOM 1
#define ALLOCM_ERR_NOT_MOVED 2

#define ALLOCM_ZERO    64
#define ALLOCM_NO_MOVE 128

#define ALLOCM_LG_ALIGN(la) (la)

#if defined(JEMALLOC_MANGLE) && defined(JEMALLOC_EXPERIMENTAL)
#define rallocm je_rallocm
#endif

#endif /* ALLOCM_SUCCESS */

/**
 * Declare rallocm() and malloc_usable_size() as weak symbols.  It
 * will be provided by jemalloc if we are using jemalloc, or it will
 * be NULL if we are using another malloc implementation.
 */
extern "C" int rallocm(void**, size_t*, size_t, size_t, int)
__attribute__((weak));

#ifdef _LIBSTDCXX_FBSTRING
namespace std _GLIBCXX_VISIBILITY(default) {
_GLIBCXX_BEGIN_NAMESPACE_VERSION
#else
namespace folly {
#endif


/**
 * Determine if we are using jemalloc or not.
 */
inline bool usingJEMalloc() {
  return rallocm != NULL;
}

/**
 * For jemalloc's size classes, see
 * http://www.canonware.com/download/jemalloc/jemalloc-latest/doc/jemalloc.html
 */
inline size_t goodMallocSize(size_t minSize) {
  if (!usingJEMalloc()) {
    // Not using jemalloc - no smarts
    return minSize;
  }
  if (minSize <= 64) {
    // Choose smallest allocation to be 64 bytes - no tripping over
    // cache line boundaries, and small string optimization takes care
    // of short strings anyway.
    return 64;
  }
  if (minSize <= 512) {
    // Round up to the next multiple of 64; we don't want to trip over
    // cache line boundaries.
    return (minSize + 63) & ~size_t(63);
  }
  if (minSize <= 3840) {
    // Round up to the next multiple of 256
    return (minSize + 255) & ~size_t(255);
  }
  if (minSize <= 4072 * 1024) {
    // Round up to the next multiple of 4KB
    return (minSize + 4095) & ~size_t(4095);
  }
  // Holy Moly
  // Round up to the next multiple of 4MB
  return (minSize + 4194303) & ~size_t(4194303);
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
    if (currentCapacity >= jemallocMinInPlaceExpandable &&
        rallocm(&p, NULL, newCapacity, 0, ALLOCM_NO_MOVE) == ALLOCM_SUCCESS) {
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
