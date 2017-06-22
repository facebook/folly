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
#include <folly/experimental/logging/LogLevel.h>

#include <cctype>
#include <ostream>

#include <folly/Conv.h>

using std::string;

namespace folly {

LogLevel stringToLogLevel(StringPiece name) {
  string lowerNameStr;
  lowerNameStr.reserve(name.size());
  for (char c : name) {
    lowerNameStr.push_back(static_cast<char>(std::tolower(c)));
  }
  StringPiece lowerName{lowerNameStr};

  // If the string is of the form "LogLevel::foo" or "LogLevel(foo)"
  // strip it down just to "foo".  This makes sure we can process both
  // the "LogLevel::DEBUG" and "LogLevel(1234)" formats produced by
  // logLevelToString().
  constexpr StringPiece lowercasePrefix{"loglevel::"};
  constexpr StringPiece wrapperPrefix{"loglevel("};
  if (lowerName.startsWith(lowercasePrefix)) {
    lowerName.advance(lowercasePrefix.size());
  } else if (lowerName.startsWith(wrapperPrefix) && lowerName.endsWith(")")) {
    lowerName.advance(wrapperPrefix.size());
    lowerName.subtract(1);
  }

  if (lowerName == "uninitialized") {
    return LogLevel::UNINITIALIZED;
  } else if (lowerName == "none") {
    return LogLevel::NONE;
  } else if (lowerName == "debug") {
    return LogLevel::DEBUG;
  } else if (lowerName == "info") {
    return LogLevel::INFO;
  } else if (lowerName == "warn" || lowerName == "warning") {
    return LogLevel::WARN;
  } else if (lowerName == "error" || lowerName == "err") {
    return LogLevel::ERR;
  } else if (lowerName == "critical") {
    return LogLevel::CRITICAL;
  } else if (lowerName == "dfatal") {
    return LogLevel::DFATAL;
  } else if (lowerName == "fatal") {
    return LogLevel::FATAL;
  } else if (lowerName == "max" || lowerName == "max_level") {
    return LogLevel::MAX_LEVEL;
  }

  if (lowerName.startsWith("dbg")) {
    auto remainder = lowerName.subpiece(3);
    auto level = folly::tryTo<int>(remainder).value_or(-1);
    if (level < 0 || level > 100) {
      throw std::range_error("invalid dbg logger level: " + name.str());
    }
    return LogLevel::DBG0 - level;
  }

  // Try as an plain integer if all else fails
  try {
    auto level = folly::to<uint32_t>(lowerName);
    return static_cast<LogLevel>(level);
  } catch (const std::exception&) {
    throw std::range_error("invalid logger name " + name.str());
  }
}

string logLevelToString(LogLevel level) {
  if (level == LogLevel::UNINITIALIZED) {
    return "UNINITIALIZED";
  } else if (level == LogLevel::NONE) {
    return "NONE";
  } else if (level == LogLevel::DEBUG) {
    return "DEBUG";
  } else if (level == LogLevel::INFO) {
    return "INFO";
  } else if (level == LogLevel::WARN) {
    return "WARN";
  } else if (level == LogLevel::ERR) {
    return "ERR";
  } else if (level == LogLevel::CRITICAL) {
    return "CRITICAL";
  } else if (level == LogLevel::DFATAL) {
    return "DFATAL";
  } else if (level == LogLevel::FATAL) {
    return "FATAL";
  }

  if (static_cast<uint32_t>(level) <= static_cast<uint32_t>(LogLevel::DBG0) &&
      static_cast<uint32_t>(level) > static_cast<uint32_t>(LogLevel::DEBUG)) {
    auto num =
        static_cast<uint32_t>(LogLevel::DBG0) - static_cast<uint32_t>(level);
    return folly::to<string>("DBG", num);
  }
  return folly::to<string>("LogLevel(", static_cast<uint32_t>(level), ")");
}

std::ostream& operator<<(std::ostream& os, LogLevel level) {
  os << logLevelToString(level);
  return os;
}
}
