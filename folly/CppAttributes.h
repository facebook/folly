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

/**
 * GCC compatible wrappers around clang attributes.
 *
 * ABI STABILITY NOTE:
 * Some attributes (notably [[no_unique_address]]) affect struct layout.
 * When the Folly library is compiled with one C++ standard and headers are
 * included from an application compiled with a different standard, ODR
 * violations can occur, leading to memory corruption and crashes.
 *
 * This header includes logic to detect such mismatches and force the headers
 * to use the same layout as the compiled library.
 * See: https://github.com/facebook/folly/issues/2477
 */

#pragma once

#include <folly/Portability.h>
#include <folly/portability/Config.h>

#ifndef __has_attribute
#define FOLLY_HAS_ATTRIBUTE(x) 0
#else
#define FOLLY_HAS_ATTRIBUTE(x) __has_attribute(x)
#endif

#ifndef __has_cpp_attribute
#define FOLLY_HAS_CPP_ATTRIBUTE(x) 0
#else
#define FOLLY_HAS_CPP_ATTRIBUTE(x) __has_cpp_attribute(x)
#endif

#ifndef __has_extension
#define FOLLY_HAS_EXTENSION(x) 0
#else
#define FOLLY_HAS_EXTENSION(x) __has_extension(x)
#endif

/**
 * Nullable indicates that a return value or a parameter may be a `nullptr`,
 * e.g.
 *
 * int* FOLLY_NULLABLE foo(int* a, int* FOLLY_NULLABLE b) {
 *   if (*a > 0) {  // safe dereference
 *     return nullptr;
 *   }
 *   if (*b < 0) {  // unsafe dereference
 *     return *a;
 *   }
 *   if (b != nullptr && *b == 1) {  // safe checked dereference
 *     return new int(1);
 *   }
 *   return nullptr;
 * }
 *
 * Ignores Clang's -Wnullability-extension since it correctly handles the case
 * where the extension is not present.
 */
#if FOLLY_HAS_EXTENSION(nullability)
#define FOLLY_NULLABLE                                   \
  FOLLY_PUSH_WARNING                                     \
  FOLLY_CLANG_DISABLE_WARNING("-Wnullability-extension") \
  _Nullable FOLLY_POP_WARNING
#define FOLLY_NONNULL                                    \
  FOLLY_PUSH_WARNING                                     \
  FOLLY_CLANG_DISABLE_WARNING("-Wnullability-extension") \
  _Nonnull FOLLY_POP_WARNING
#else
#define FOLLY_NULLABLE
#define FOLLY_NONNULL
#endif

/**
 * "Cold" indicates to the compiler that a function is only expected to be
 * called from unlikely code paths. It can affect decisions made by the
 * optimizer both when processing the function body and when analyzing
 * call-sites.
 */
#if FOLLY_HAS_CPP_ATTRIBUTE(gnu::cold)
#define FOLLY_ATTR_GNU_COLD gnu::cold
#else
#define FOLLY_ATTR_GNU_COLD
#endif

/// FOLLY_ATTR_MAYBE_UNUSED_IF_NDEBUG
///
/// When defined(NDEBUG), expands to maybe_unused; otherwise, expands to empty.
/// Useful for marking variables that are used, in the sense checked for by the
/// attribute maybe_unused, only in debug builds.
#if defined(NDEBUG)
#define FOLLY_ATTR_MAYBE_UNUSED_IF_NDEBUG maybe_unused
#else
#define FOLLY_ATTR_MAYBE_UNUSED_IF_NDEBUG
#endif

/**
 *  no_unique_address indicates that a member variable can be optimized to
 * occupy no space, rather than the minimum 1-byte used by default.
 *
 *  class Empty {};
 *
 *  class NonEmpty1 {
 *    [[FOLLY_ATTR_NO_UNIQUE_ADDRESS]] Empty e;
 *    int f;
 *  };
 *
 *  class NonEmpty2 {
 *    Empty e;
 *    int f;
 *  };
 *
 *  sizeof(NonEmpty1); // may be == sizeof(int)
 *  sizeof(NonEmpty2); // must be > sizeof(int)
 *
 * ABI STABILITY:
 * This attribute changes struct layout. When linking against a pre-compiled
 * Folly library, we MUST use the same layout as the library was compiled with.
 * If the library was compiled without [[no_unique_address]] support (e.g.,
 * C++17), we must NOT apply the attribute even if the current compilation
 * would support it (e.g., C++20). Doing otherwise causes ODR violations,
 * memory corruption, and crashes.
 * See: https://github.com/facebook/folly/issues/2477
 */

// First, detect what the current compilation unit supports
#if FOLLY_HAS_CPP_ATTRIBUTE(no_unique_address)
#define FOLLY_CURRENT_HAS_NO_UNIQUE_ADDRESS 1
#define FOLLY_CURRENT_NO_UNIQUE_ADDRESS_ATTR no_unique_address
#elif FOLLY_HAS_CPP_ATTRIBUTE(msvc::no_unique_address)
#define FOLLY_CURRENT_HAS_NO_UNIQUE_ADDRESS 1
#define FOLLY_CURRENT_NO_UNIQUE_ADDRESS_ATTR msvc::no_unique_address
#else
#define FOLLY_CURRENT_HAS_NO_UNIQUE_ADDRESS 0
#endif

// Now determine what to actually use, respecting library ABI
#if defined(FOLLY_LIBRARY_HAS_NO_UNIQUE_ADDRESS)
// We have library configuration available - use it for ABI compatibility

#if FOLLY_LIBRARY_HAS_NO_UNIQUE_ADDRESS && FOLLY_CURRENT_HAS_NO_UNIQUE_ADDRESS
// Both library and current compilation support it - use the attribute
#define FOLLY_ATTR_NO_UNIQUE_ADDRESS FOLLY_CURRENT_NO_UNIQUE_ADDRESS_ATTR

#elif FOLLY_LIBRARY_HAS_NO_UNIQUE_ADDRESS && \
    !FOLLY_CURRENT_HAS_NO_UNIQUE_ADDRESS
// Library was compiled with it, but current compilation doesn't support it.
// This is an error - the application will have wrong struct layouts.
#error \
    "Folly library was compiled with [[no_unique_address]] support, but " \
        "current compilation does not support it. This will cause ABI " \
        "incompatibility. Please compile with C++20 or later."

#elif !FOLLY_LIBRARY_HAS_NO_UNIQUE_ADDRESS && \
    FOLLY_CURRENT_HAS_NO_UNIQUE_ADDRESS
// Library was compiled WITHOUT [[no_unique_address]], but current compilation
// supports it. We MUST NOT use the attribute to maintain ABI compatibility.
// Issue a warning to inform users of this situation.
#if defined(__GNUC__) || defined(__clang__)
#pragma message(                                                           \
    "Warning: Folly library was compiled without [[no_unique_address]] "   \
    "(likely C++17), but current compilation supports it. Disabling the "  \
    "attribute to maintain ABI compatibility. Consider recompiling Folly " \
    "with C++20 for optimal performance. See issue #2477.")
#endif
#define FOLLY_ATTR_NO_UNIQUE_ADDRESS /* disabled for ABI compatibility */

#else
// Neither library nor current compilation support it
#define FOLLY_ATTR_NO_UNIQUE_ADDRESS
#endif

#else
// No library configuration available (header-only use or building Folly itself)
// Use whatever the current compilation supports
#if FOLLY_CURRENT_HAS_NO_UNIQUE_ADDRESS
#define FOLLY_ATTR_NO_UNIQUE_ADDRESS FOLLY_CURRENT_NO_UNIQUE_ADDRESS_ATTR
#else
#define FOLLY_ATTR_NO_UNIQUE_ADDRESS
#endif
#endif

#if FOLLY_HAS_CPP_ATTRIBUTE(clang::no_destroy)
#define FOLLY_ATTR_CLANG_NO_DESTROY clang::no_destroy
#else
#define FOLLY_ATTR_CLANG_NO_DESTROY
#endif

#if FOLLY_HAS_CPP_ATTRIBUTE(clang::uninitialized)
#define FOLLY_ATTR_CLANG_UNINITIALIZED clang::uninitialized
#else
#define FOLLY_ATTR_CLANG_UNINITIALIZED
#endif

/**
 * Accesses to objects with types with this attribute are not subjected to
 * type-based alias analysis, but are instead assumed to be able to alias any
 * other type of objects, just like the char type.
 */
#if FOLLY_HAS_CPP_ATTRIBUTE(gnu::may_alias)
#define FOLLY_ATTR_GNU_MAY_ALIAS gnu::may_alias
#else
#define FOLLY_ATTR_GNU_MAY_ALIAS
#endif

#if FOLLY_HAS_CPP_ATTRIBUTE(gnu::pure)
#define FOLLY_ATTR_GNU_PURE gnu::pure
#else
#define FOLLY_ATTR_GNU_PURE
#endif

#if FOLLY_HAS_CPP_ATTRIBUTE(clang::preserve_most)
#define FOLLY_ATTR_CLANG_PRESERVE_MOST clang::preserve_most
#else
#define FOLLY_ATTR_CLANG_PRESERVE_MOST
#endif

#if FOLLY_HAS_CPP_ATTRIBUTE(clang::preserve_all)
#define FOLLY_ATTR_CLANG_PRESERVE_ALL clang::preserve_all
#else
#define FOLLY_ATTR_CLANG_PRESERVE_ALL
#endif

#if FOLLY_HAS_CPP_ATTRIBUTE(gnu::used)
#define FOLLY_ATTR_GNU_USED gnu::used
#else
#define FOLLY_ATTR_GNU_USED
#endif

#if FOLLY_HAS_CPP_ATTRIBUTE(gnu::retain)
#define FOLLY_ATTR_GNU_RETAIN gnu::retain
#else
#define FOLLY_ATTR_GNU_RETAIN
#endif

#if FOLLY_HAS_CPP_ATTRIBUTE(gnu::noclone)
#define FOLLY_ATTR_GNU_NOCLONE gnu::noclone
#else
#define FOLLY_ATTR_GNU_NOCLONE
#endif

#if FOLLY_HAS_CPP_ATTRIBUTE(clang::lifetimebound)
#define FOLLY_ATTR_CLANG_LIFETIMEBOUND clang::lifetimebound
#else
#define FOLLY_ATTR_CLANG_LIFETIMEBOUND
#endif

#if FOLLY_HAS_CPP_ATTRIBUTE(clang::coro_await_elidable)
#define FOLLY_ATTR_CLANG_CORO_AWAIT_ELIDABLE clang::coro_await_elidable
#else
#define FOLLY_ATTR_CLANG_CORO_AWAIT_ELIDABLE
#endif

#if FOLLY_HAS_CPP_ATTRIBUTE(clang::coro_await_elidable_argument)
#define FOLLY_ATTR_CLANG_CORO_AWAIT_ELIDABLE_ARGUMENT \
  clang::coro_await_elidable_argument
#else
#define FOLLY_ATTR_CLANG_CORO_AWAIT_ELIDABLE_ARGUMENT
#endif
