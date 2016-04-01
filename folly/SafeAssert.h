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

#pragma once

#include <folly/Portability.h>
#include <folly/Preprocessor.h>

/**
 * Verify that the expression is true. If not, prints an error message
 * (containing msg) to stderr and abort()s. Just like CHECK(), but only
 * logs to stderr and only does async-signal-safe calls.
 */
#define FOLLY_SAFE_CHECK(expr, msg) \
  ((expr) ? static_cast<void>(0) : \
   ::folly::detail::assertionFailure( \
       FB_STRINGIZE(expr), (msg), __FILE__, __LINE__, __PRETTY_FUNCTION__))

/**
 * In debug mode, verify that the expression is true. Otherwise, do nothing
 * (do not even evaluate expr). Just like assert() or DCHECK(), but only
 * logs to stderr and only does async-signal-safe calls.
 */
#ifdef NDEBUG
#define FOLLY_SAFE_DCHECK(expr, msg) (static_cast<void>(0))
#else
#define FOLLY_SAFE_DCHECK FOLLY_SAFE_CHECK
#endif

namespace folly { namespace detail {

[[noreturn]] void assertionFailure(
    const char* expr,
    const char* msg,
    const char* file,
    unsigned int line,
    const char* function);
}}  // namespace folly
