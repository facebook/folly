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
#include <folly/CppAttributes.h>
#include <folly/Range.h>
#include <folly/Synchronized.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <folly/experimental/logging/LogName.h>

namespace folly {

class LogCategory;
enum class LogLevel : uint32_t;

/**
 * LoggerDB stores the set of LogCategory objects.
 */
class LoggerDB {
 public:
  /**
   * Get the main LoggerDB singleton.
   */
  static LoggerDB* get();

  /**
   * Get the LogCategory for the specified name.
   *
   * This creates the LogCategory for the specified name if it does not exist
   * already.
   */
  LogCategory* getCategory(folly::StringPiece name);

  /**
   * Get the LogCategory for the specified name, if it already exists.
   *
   * This returns nullptr if no LogCategory has been created yet for the
   * specified name.
   */
  LogCategory* FOLLY_NULLABLE getCategoryOrNull(folly::StringPiece name);

  /**
   * Set the log level for the specified category.
   *
   * Messages logged to a specific log category will be ignored unless the
   * message log level is greater than the LogCategory's effective log level.
   *
   * If inherit is true, LogCategory's effective log level is the minimum of
   * its level and it's parent category's effective log level.  If inherit is
   * false, the LogCategory's effective log level is simply its log level.
   * (Setting inherit to false is necessary if you want a child LogCategory to
   * use a less verbose level than its parent categories.)
   */
  void setLevel(folly::StringPiece name, LogLevel level, bool inherit = true);
  void setLevel(LogCategory* category, LogLevel level, bool inherit = true);

  /**
   * Apply a configuration string specifying a series a log levels.
   *
   * The string format is a comma separated list of <name>=<level> sections.
   * e.g.: "foo=DBG3,log.bar=WARN"
   *
   * Returns a list of error messages for each error encountered trying to
   * parse the config string.  The return value will be an empty vector if no
   * errors were encountered.
   */
  std::vector<std::string> processConfigString(folly::StringPiece config);

  /**
   * Remove all registered LogHandlers on all LogCategory objects.
   *
   * This is called on the main LoggerDB object during shutdown.
   */
  void cleanupHandlers();

  /**
   * Call flush() on all LogHandler objects registered on any LogCategory in
   * this LoggerDB.
   *
   * Returns the number of registered LogHandlers.
   */
  size_t flushAllHandlers();

  /**
   * Initialize the LogCategory* and std::atomic<LogLevel> used by an XLOG()
   * statement.
   *
   * Returns the current effective LogLevel of the category.
   */
  LogLevel xlogInit(
      folly::StringPiece categoryName,
      std::atomic<LogLevel>* xlogCategoryLevel,
      LogCategory** xlogCategory);
  LogCategory* xlogInitCategory(
      folly::StringPiece categoryName,
      LogCategory** xlogCategory,
      std::atomic<bool>* isInitialized);

  enum TestConstructorArg { TESTING };

  /**
   * Construct a LoggerDB for testing purposes.
   *
   * Most callers should not need this function, and should use
   * LoggerDB::get() to obtain the main LoggerDB singleton.  This function
   * exists mainly to allow testing LoggerDB objects in unit tests.
   * It requires an explicit argument just to prevent callers from calling it
   * unintentionally.
   */
  explicit LoggerDB(TestConstructorArg);

  /**
   * internalWarning() is used to report a problem when something goes wrong
   * internally in the logging library.
   *
   * We can't log these messages through the normal logging flow since logging
   * itself has failed.
   *
   * Example scenarios where this is used:
   * - We fail to write to a log file (for instance, when the disk is full)
   * - A LogHandler throws an unexpected exception
   */
  template <typename... Args>
  static void internalWarning(
      folly::StringPiece file,
      int lineNumber,
      Args&&... args) noexcept {
    internalWarningImpl(
        file, lineNumber, folly::to<std::string>(std::forward<Args>(args)...));
  }

  using InternalWarningHandler =
      void (*)(folly::StringPiece file, int lineNumber, std::string&&);

  /**
   * Set a function to be called when the logging library generates an internal
   * warning.
   *
   * The supplied handler should never throw exceptions.
   *
   * If a null handler is supplied, the default built-in handler will be used.
   *
   * The default handler reports the message with _CrtDbgReport(_CRT_WARN) on
   * Windows, and prints the message to stderr on other platforms.  It also
   * rate limits messages if they are arriving too quickly.
   */
  static void setInternalWarningHandler(InternalWarningHandler handler);

 private:
  using LoggerNameMap = std::unordered_map<
      folly::StringPiece,
      std::unique_ptr<LogCategory>,
      LogName::Hash,
      LogName::Equals>;

  // Forbidden copy constructor and assignment operator
  LoggerDB(LoggerDB const&) = delete;
  LoggerDB& operator=(LoggerDB const&) = delete;

  LoggerDB();
  LogCategory* getOrCreateCategoryLocked(
      LoggerNameMap& loggersByName,
      folly::StringPiece name);
  LogCategory* createCategoryLocked(
      LoggerNameMap& loggersByName,
      folly::StringPiece name,
      LogCategory* parent);

  static void internalWarningImpl(
      folly::StringPiece filename,
      int lineNumber,
      std::string&& msg) noexcept;
  static void defaultInternalWarningImpl(
      folly::StringPiece filename,
      int lineNumber,
      std::string&& msg) noexcept;

  /**
   * A map of LogCategory objects by name.
   *
   * Lookups can be performed using arbitrary StringPiece values that do not
   * have to be in canonical form.
   */
  folly::Synchronized<LoggerNameMap> loggersByName_;

  static std::atomic<InternalWarningHandler> warningHandler_;
};
}
