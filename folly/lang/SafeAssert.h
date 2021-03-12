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

#pragma once

#include <cerrno>

#include <folly/Portability.h>
#include <folly/Preprocessor.h>

#define FOLLY_SAFE_CHECK_IMPL(expr, expr_s, msg, error, ...)     \
  ((expr) ? static_cast<void>(0)                                 \
          : (::folly::signal_safe::writeErrBegin(                \
                 FOLLY_PP_STRINGIZE(expr_s), (msg)),             \
             ::folly::signal_safe::writeStderrPack(__VA_ARGS__), \
             ::folly::signal_safe::writeErrEnd(                  \
                 __FILE__, __LINE__, __PRETTY_FUNCTION__, error)))

/**
 * Verify that the expression is true. If not, prints an error message
 * (containing msg) to stderr and abort()s. Just like CHECK(), but only
 * logs to stderr and only does async-signal-safe calls.
 */
#define FOLLY_SAFE_CHECK(expr, msg, ...) \
  FOLLY_SAFE_CHECK_IMPL((expr), (expr), (msg), 0, __VA_ARGS__);

/**
 * In debug mode, verify that the expression is true. Otherwise, do nothing
 * (do not even evaluate expr). Just like DCHECK(), but only logs to stderr and
 * only does async-signal-safe calls.
 */
#define FOLLY_SAFE_DCHECK(expr, msg, ...) \
  FOLLY_SAFE_CHECK_IMPL(                  \
      !::folly::kIsDebug || (expr), (expr), (msg), 0, __VA_ARGS__)

/**
 * Like FOLLY_SAFE_CHECK, but also prints errno.
 */
#define FOLLY_SAFE_PCHECK(expr, msg, ...) \
  FOLLY_SAFE_CHECK_IMPL((expr), (expr), (msg), errno, __VA_ARGS__)

namespace folly {
namespace signal_safe {

void writeStderr(const char*);
void writeStderr(unsigned long int);

template <typename... A>
void writeStderrPack(A... a) {
  // NOTE: C++14-compatible version of comma fold expression.
  using _ = int[];
  void(_{0, (writeStderr(a), 0)...});
}

void writeErrBegin(const char* expr, const char* msg);
[[noreturn]] void writeErrEnd(
    const char* file, unsigned int line, const char* function, int error);

} // namespace signal_safe
} // namespace folly
