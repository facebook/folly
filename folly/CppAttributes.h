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
 * @author Dominik Gabi
 */

#pragma once

#include <folly/Portability.h>

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
#if defined(__GNUC__) && __GNUC__
#define FOLLY_COLD __attribute__((__cold__))
#else
#define FOLLY_COLD
#endif

/**
 *  no_unique_address indicates that a member variable can be optimized to
 * occupy no space, rather than the minimum 1-byte used by default.
 *
 *  class Empty {};
 *
 *  class NonEmpty1 {
 *    FOLLY_ATTR_NO_UNIQUE_ADDRESS Empty e;
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
 */
#if FOLLY_HAS_CPP_ATTRIBUTE(no_unique_address)
#define FOLLY_ATTR_NO_UNIQUE_ADDRESS [[no_unique_address]]
#else
#define FOLLY_ATTR_NO_UNIQUE_ADDRESS
#endif

#if FOLLY_HAS_CPP_ATTRIBUTE(clang::no_destroy)
#define FOLLY_ATTR_CLANG_NO_DESTROY clang::no_destroy
#else
#define FOLLY_ATTR_CLANG_NO_DESTROY
#endif

/**
 * Accesses to objects with types with this attribute are not subjected to
 * type-based alias analysis, but are instead assumed to be able to alias any
 * other type of objects, just like the char type.
 */
#if FOLLY_HAS_CPP_ATTRIBUTE(gnu::may_alias)
#define FOLLY_ATTR_MAY_ALIAS gnu::may_alias
#else
#define FOLLY_ATTR_MAY_ALIAS
#endif

#if FOLLY_HAS_CPP_ATTRIBUTE(gnu::pure)
#define FOLLY_ATTR_PURE gnu::pure
#else
#define FOLLY_ATTR_PURE
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
