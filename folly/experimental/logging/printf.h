/*
 * Copyright 2004-present Facebook, Inc.
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

/*
 * C-style printf-like macros for the logging library.
 *
 * These are defined in their own separate header file to discourage their use
 * in new code.  These macros make it somewhat easier to convert existing code
 * using printf()-like statements to the logging library.  However, new code
 * should generally prefer to use one of the other macro forms instead
 * (simple argument concatenation, folly::format based, or iostream-like).
 *
 * These use a "C" suffix to the macro name since these use C-style format
 * syntax.  (The "F" suffix is used for folly:format()-style.)
 */

#include <folly/experimental/logging/Logger.h>
#include <folly/experimental/logging/xlog.h>

namespace folly {
std::string loggingFormatPrintf(
    FOLLY_PRINTF_FORMAT const char* format,
    ...) noexcept FOLLY_PRINTF_FORMAT_ATTR(1, 2);
}

/**
 * Log a message to the specified logger using a printf-style format string.
 */
#define FB_LOGC(logger, level, fmt, ...)   \
  FB_LOG_IMPL(                             \
      logger,                              \
      ::folly::LogLevel::level,            \
      ::folly::LogStreamProcessor::APPEND, \
      ::folly::loggingFormatPrintf(fmt, ##__VA_ARGS__))

/**
 * Log a message to the file's default log category using a printf-style format
 * string.
 */
#define XLOGC(level, fmt, ...)             \
  XLOG_IMPL(                               \
      ::folly::LogLevel::level,            \
      ::folly::LogStreamProcessor::APPEND, \
      ::folly::loggingFormatPrintf(fmt, ##__VA_ARGS__))
