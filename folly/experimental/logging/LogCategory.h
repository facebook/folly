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

#include <atomic>
#include <cstdint>
#include <list>
#include <string>

#include <folly/Range.h>
#include <folly/Synchronized.h>
#include <folly/experimental/logging/LogLevel.h>

namespace folly {

class LoggerDB;
class LogHandler;
class LogMessage;

/**
 * LogCategory stores all of the logging configuration for a specific
 * log category.
 *
 * This class is separate from Logger to allow multiple Logger objects to all
 * refer to the same log category.  Logger can be thought of as a small wrapper
 * class that behaves like a pointer to a LogCategory object.
 */
class LogCategory {
 public:
  /**
   * Create the root LogCategory.
   *
   * This should generally only be invoked by LoggerDB.
   */
  explicit LogCategory(LoggerDB* db);

  /**
   * Create a new LogCategory.
   *
   * This should only be invoked by LoggerDB, while holding the main LoggerDB
   * lock.
   *
   * The name argument should already be in canonical form.
   *
   * This constructor automatically adds this new LogCategory to the parent
   * category's firstChild_ linked-list.
   */
  LogCategory(folly::StringPiece name, LogCategory* parent);

  /**
   * Get the name of this log category.
   */
  const std::string& getName() const {
    return name_;
  }

  /**
   * Get the level for this log category.
   */
  LogLevel getLevel() const {
    return static_cast<LogLevel>(
        level_.load(std::memory_order_acquire) & ~FLAG_INHERIT);
  }

  /**
   * Get the effective level for this log category.
   *
   * This is the minimum log level of this category and all of its parents.
   * Log messages below this level will be ignored, while messages at or
   * above this level need to be processed by this category or one of its
   * parents.
   */
  LogLevel getEffectiveLevel() const {
    return effectiveLevel_.load(std::memory_order_acquire);
  }

  /**
   * Get the effective log level using std::memory_order_relaxed.
   *
   * This is primarily used for log message checks.  Most other callers should
   * use getEffectiveLevel() above to be more conservative with regards to
   * memory ordering.
   */
  LogLevel getEffectiveLevelRelaxed() const {
    return effectiveLevel_.load(std::memory_order_relaxed);
  }

  /**
   * Set the log level for this LogCategory.
   *
   * Messages logged to a specific log category will be ignored unless the
   * message log level is greater than the LogCategory's effective log level.
   *
   * If inherit is true, LogCategory's effective log level is the minimum of
   * its level and its parent category's effective log level.  If inherit is
   * false, the LogCategory's effective log level is simply its log level.
   * (Setting inherit to false is necessary if you want a child LogCategory to
   * use a less verbose level than its parent categories.)
   */
  void setLevel(LogLevel level, bool inherit = true);

  /**
   * Process a log message.
   *
   * This method generally should be invoked through the Logger APIs,
   * rather than calling this directly.
   *
   * This method assumes that log level admittance checks have already been
   * performed.  This method unconditionally passes the message to the
   * LogHandlers attached to this LogCategory, without any additional log level
   * checks (apart from the ones done in the LogHandlers).
   */
  void processMessage(const LogMessage& message);

  /**
   * Get the LoggerDB that this LogCategory belongs to.
   *
   * This is almost always the main LoggerDB singleton returned by
   * LoggerDB::get().  The logging unit tests are the main location that
   * creates alternative LoggerDB objects.
   */
  LoggerDB* getDB() const {
    return db_;
  }

  /**
   * Attach a LogHandler to this category.
   */
  void addHandler(std::shared_ptr<LogHandler> handler);

  /**
   * Remove all LogHandlers from this category.
   */
  void clearHandlers();

  /**
   * Note: setLevelLocked() may only be called while holding the main
   * LoggerDB lock.
   *
   * This method should only be invoked by LoggerDB.
   */
  void setLevelLocked(LogLevel level, bool inherit);

 private:
  enum : uint32_t { FLAG_INHERIT = 0x80000000 };

  // Forbidden copy constructor and assignment operator
  LogCategory(LogCategory const&) = delete;
  LogCategory& operator=(LogCategory const&) = delete;

  void updateEffectiveLevel(LogLevel newEffectiveLevel);
  void parentLevelUpdated(LogLevel parentEffectiveLevel);

  /**
   * The minimum log level of this category and all of its parents.
   */
  std::atomic<LogLevel> effectiveLevel_{LogLevel::MAX_LEVEL};

  /**
   * The current log level for this category.
   *
   * The most significant bit is used to indicate if this logger should
   * inherit its parent's effective log level.
   */
  std::atomic<uint32_t> level_{0};

  /**
   * Our parent LogCategory in the category hierarchy.
   *
   * For instance, if our log name is "foo.bar.abc", our parent category
   * is "foo.bar".
   */
  LogCategory* const parent_{nullptr};

  /**
   * Our log category name.
   */
  const std::string name_;

  /**
   * The list of LogHandlers attached to this category.
   */
  folly::Synchronized<std::vector<std::shared_ptr<LogHandler>>> handlers_;

  /**
   * A pointer to the LoggerDB that we belong to.
   *
   * This is almost always the main LoggerDB singleton.  Unit tests are the
   * main place where we use other LoggerDB objects besides the singleton.
   */
  LoggerDB* const db_{nullptr};

  /**
   * Pointers to children and sibling loggers.
   * These pointers should only ever be accessed while holding the main
   * LoggerDB lock.  (These are only modified when creating new loggers,
   * which occurs with the main LoggerDB lock held.)
   */
  LogCategory* firstChild_{nullptr};
  LogCategory* nextSibling_{nullptr};
};
}
