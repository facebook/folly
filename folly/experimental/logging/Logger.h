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

#include <folly/Conv.h>
#include <folly/Format.h>
#include <folly/experimental/logging/LogCategory.h>
#include <folly/experimental/logging/LogLevel.h>

/**
 * Log a message to the specified logger.
 *
 * This macro avoids evaluating the log arguments unless the log level check
 * succeeds.
 */
#define FB_LOG(logger, level, msg, ...)                             \
  do {                                                              \
    const auto fbLogLevelTmp = ::folly::LogLevel::level;            \
    const auto& fbLoggerTmp = (logger);                             \
    if (fbLoggerTmp.logCheck(fbLogLevelTmp)) {                      \
      fbLoggerTmp.log(                                              \
          fbLogLevelTmp, __FILE__, __LINE__, (msg), ##__VA_ARGS__); \
    }                                                               \
  } while (0)

/**
 * Log a message to the specified logger, using a folly::format() string.
 *
 * The arguments will be processed using folly::format().  The format syntax
 * is similar to Python format strings.
 *
 * This macro avoids evaluating the log arguments unless the log level check
 * succeeds.
 */
#define FB_LOGF(logger, level, fmt, ...)                            \
  do {                                                              \
    const auto fbLogLevelTmp = ::folly::LogLevel::level;            \
    const auto& fbLoggerTmp = (logger);                             \
    if (fbLoggerTmp.logCheck(fbLogLevelTmp)) {                      \
      fbLoggerTmp.logf(                                             \
          fbLogLevelTmp, __FILE__, __LINE__, (fmt), ##__VA_ARGS__); \
    }                                                               \
  } while (0)

namespace folly {

class LoggerDB;
class LogMessage;

/**
 * Logger is the class you will normally use to log messages.
 *
 * The Logger is really just a small wrapper class that contains a pointer
 * to the appropriate LogCategory object.  It exists to allow for easy static
 * initialization of log categories, as well as to provide fast checking of the
 * current effective log level.
 */
class Logger {
 public:
  /**
   * Construct a Logger for the given category name.
   *
   * A LogCategory object for this category will be created if one does not
   * already exist.
   */
  explicit Logger(folly::StringPiece name);

  /**
   * Construct a Logger pointing to an existing LogCategory object.
   */
  explicit Logger(LogCategory* cat);

  /**
   * Construct a Logger for a specific LoggerDB object, rather than the main
   * singleton.
   *
   * This is primarily intended for use in unit tests.
   */
  Logger(LoggerDB* db, folly::StringPiece name);

  /**
   * Get the effective level for this logger.
   *
   * This is the minimum log level of this logger, or any of its parents.
   * Log messages below this level will be ignored, while messages at or
   * above this level need to be processed by this logger or one of its
   * parents.
   */
  LogLevel getEffectiveLevel() const {
    return category_->getEffectiveLevel();
  }

  /**
   * Check whether this Logger or any of its parent Loggers would do anything
   * with a log message at the given level.
   */
  bool logCheck(LogLevel level) const {
    // We load the effective level using std::memory_order_relaxed.
    //
    // We want to make log checks as lightweight as possible.  It's fine if we
    // don't immediately respond to changes made to the log level from other
    // threads.  We can wait until some other operation triggers a memory
    // barrier before we honor the new log level setting.  No other memory
    // accesses depend on the log level value.  Callers should not rely on all
    // other threads to immediately stop logging as soon as they decrease the
    // log level for a given category.
    return category_->getEffectiveLevelRelaxed() <= level;
  }

  /**
   * Unconditionally log a message.
   *
   * The caller is responsible for calling logCheck() before log() to ensure
   * that this log message should be admitted.  This is typically done with one
   * of the logging macros.
   */
  void log(
      LogLevel level,
      folly::StringPiece filename,
      unsigned int lineNumber,
      std::string&& msg) const;
  void log(
      LogLevel level,
      folly::StringPiece filename,
      unsigned int lineNumber,
      folly::StringPiece msg) const;

  /**
   * Unconditionally log a message.
   *
   * This concatenates the arguments into a string using
   * folly::to<std::string>()
   */
  template <typename... Args>
  void log(
      LogLevel level,
      folly::StringPiece filename,
      unsigned int lineNumber,
      Args&&... args) const {
    std::string msg;
    try {
      msg = folly::to<std::string>(std::forward<Args>(args)...);
    } catch (const std::exception& ex) {
      // This most likely means there was some error converting the arguments
      // to strings.  Handle the exception here, rather than letting it
      // propagate up, since callers generally do not expect log statements to
      // throw.
      //
      // Just log an error message letting indicating that something went wrong
      // formatting the log message.
      msg =
          folly::to<std::string>("error constructing log message: ", ex.what());
    }
    log(level, filename, lineNumber, std::move(msg));
  }

  /**
   * Unconditionally log a message using a format string.
   *
   * This uses folly::format() to format the message.
   */
  template <typename... Args>
  void logf(
      LogLevel level,
      folly::StringPiece filename,
      unsigned int lineNumber,
      folly::StringPiece fmt,
      Args&&... args) const {
    std::string msg;
    try {
      msg = folly::sformat(fmt, std::forward<Args>(args)...);
    } catch (const std::exception& ex) {
      // This most likely means that the caller had a bug in their format
      // string/arguments.  Handle the exception here, rather than letting it
      // propagate up, since callers generally do not expect log statements to
      // throw.
      //
      // Log the format string by itself, to help the developer at least
      // identify the buggy format string in their code.
      msg = folly::to<std::string>(
          "error formatting log message: ",
          ex.what(),
          "; format string: ",
          fmt);
    }
    log(level, filename, lineNumber, std::move(msg));
  }

  /**
   * Get the LogCategory that this Logger refers to.
   */
  LogCategory* getCategory() const {
    return category_;
  }

 private:
  LogCategory* const category_{nullptr};
};
}
