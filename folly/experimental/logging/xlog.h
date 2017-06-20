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

#include <folly/Likely.h>
#include <folly/Range.h>
#include <folly/experimental/logging/LogStream.h>
#include <folly/experimental/logging/Logger.h>
#include <folly/experimental/logging/LoggerDB.h>

/*
 * This file contains the XLOG() and XLOGF() macros.
 *
 * These macros make it easy to use the logging library without having to
 * manually pick log category names.  All XLOG() and XLOGF() statements in a
 * given file automatically use a LogCategory based on the current file name.
 *
 * For instance, in src/foo/bar.cpp, the default log category name will be
 * "src.foo.bar"
 *
 * If desired, the log category name used by XLOG() in a .cpp file may be
 * overridden using XLOG_SET_CATEGORY() macro.
 */

/**
 * Log a message to this file's default log category.
 *
 * By default the log category name is automatically picked based on the
 * current filename.  In src/foo/bar.cpp the log category name "src.foo.bar"
 * will be used.  In "lib/stuff/foo.h" the log category name will be
 * "lib.stuff.foo"
 *
 * Note that the filename is based on the __FILE__ macro defined by the
 * compiler.  This is typically dependent on the filename argument that you
 * give to the compiler.  For example, if you compile src/foo/bar.cpp by
 * invoking the compiler inside src/foo and only give it "bar.cpp" as an
 * argument, the category name will simply be "bar".  In general XLOG() works
 * best if you always invoke the compiler from the root directory of your
 * project repository.
 */
#define XLOG(level, ...)                   \
  XLOG_IMPL(                               \
      ::folly::LogLevel::level,            \
      ::folly::LogStreamProcessor::APPEND, \
      ##__VA_ARGS__)

/**
 * Log a message to this file's default log category, using a format string.
 */
#define XLOGF(level, fmt, arg1, ...)       \
  XLOG_IMPL(                               \
      ::folly::LogLevel::level,            \
      ::folly::LogStreamProcessor::FORMAT, \
      fmt,                                 \
      arg1,                                \
      ##__VA_ARGS__)

/**
 * Helper macro used to implement XLOG() and XLOGF()
 *
 * Beware that the level argument is evalutated twice.
 *
 * This macro is somewhat tricky:
 *
 * - In order to support streaming argument support (with the << operator),
 *   the macro must expand to a single ternary ? expression.  This is the only
 *   way we can avoid evaluating the log arguments if the log check fails,
 *   and still have the macro behave as expected when used as the body of an if
 *   or else statement.
 *
 * - We need to store some static-scope local state in order to track the
 *   LogCategory to use.  This is a bit tricky to do and still meet the
 *   requirements of being a single expression, but fortunately static
 *   variables inside a lambda work for this purpose.
 *
 *   Inside header files, each XLOG() statement defines to static variables:
 *   - the LogLevel for this category
 *   - a pointer to the LogCategory
 *
 *   If the __INCLUDE_LEVEL__ macro is available (both gcc and clang support
 *   this), then we we can detect when we are inside a .cpp file versus a
 *   header file.  If we are inside a .cpp file, we can avoid declaring these
 *   variables once per XLOG() statement, and instead we only declare one copy
 *   of these variables for the entire file.
 *
 * - We want to make sure this macro is safe to use even from inside static
 *   initialization code that runs before main.  We also want to make the log
 *   admittance check as cheap as possible, so that disabled debug logs have
 *   minimal overhead, and can be left in place even in performance senstive
 *   code.
 *
 *   In order to do this, we rely on zero-initialization of variables with
 *   static storage duration.  The LogLevel variable will always be
 *   0-initialized before any code runs.  Therefore the very first time an
 *   XLOG() statement is hit the initial log level check will always pass
 *   (since all level values are greater or equal to 0), and we then do a
 *   second check to see if the log level and category variables need to be
 *   initialized.  On all subsequent calls, disabled log statements can be
 *   skipped with just a single check of the LogLevel.
 */
#define XLOG_IMPL(level, type, ...)                                    \
  (!XLOG_IS_ON_IMPL(level))                                            \
      ? static_cast<void>(0)                                           \
      : ::folly::LogStreamProcessorT<::folly::isLogLevelFatal(level)>( \
            XLOG_GET_CATEGORY(),                                       \
            (level),                                                   \
            __FILE__,                                                  \
            __LINE__,                                                  \
            (type),                                                    \
            ##__VA_ARGS__) &                                           \
          ::folly::LogStream()

/**
 * Check if and XLOG() statement with the given log level would be enabled.
 */
#define XLOG_IS_ON(level) XLOG_IS_ON_IMPL(::folly::LogLevel::level)

/**
 * Helper macro to implement of XLOG_IS_ON()
 *
 * This macro is used in the XLOG() implementation, and therefore must be as
 * cheap as possible.  It stores the category's LogLevel as a local static
 * variable.  The very first time this macro is evaluated it will look up the
 * correct LogCategory and initialize the LogLevel.  Subsequent calls then
 * are only a single conditional log level check.
 *
 * The LogCategory object keeps track of this local LogLevel variable and
 * automatically keeps it up-to-date when the category's effective level is
 * changed.
 *
 * Most of this code must live in the macro definition itself, and cannot be
 * moved into a helper function: The getXlogCategoryName() call must be made as
 * part of the macro expansion in order to work correctly.  We also want to
 * avoid calling it whenever possible.  Therefore most of the logic must be
 * done in the macro expansion.
 *
 * See XlogLevelInfo for the implementation details.
 */
#define XLOG_IS_ON_IMPL(level)                                                 \
  ([] {                                                                        \
    static ::folly::XlogLevelInfo<XLOG_IS_IN_HEADER_FILE> _xlogLevel_;         \
    const auto _xlogLevelToCheck_ = (level);                                   \
    /*                                                                         \
     * Do an initial relaxed check.  If this fails we know the category level  \
     * is initialized and the log admittance check failed.                     \
     * Use LIKELY() to optimize for the case of disabled debug statements:     \
     * we disabled debug statements to be cheap.  If the log message is        \
     * enabled then this check will still be minimal perf overhead compared to \
     * the overall cost of logging it.                                         \
     */                                                                        \
    if (LIKELY(                                                                \
            _xlogLevelToCheck_ <                                               \
            _xlogLevel_.getLevel(                                              \
                &_xlogFileScopeInfo_, std::memory_order_relaxed))) {           \
      return false;                                                            \
    }                                                                          \
    /*                                                                         \
     * Load the level value with memory_order_acquire, and check               \
     * to see if it is initialized or not.                                     \
     */                                                                        \
    auto _xlogCurrentLevel_ =                                                  \
        _xlogLevel_.getLevel(&_xlogFileScopeInfo_, std::memory_order_acquire); \
    if (UNLIKELY(_xlogCurrentLevel_ == ::folly::LogLevel::UNINITIALIZED)) {    \
      _xlogCurrentLevel_ = _xlogLevel_.init(                                   \
          getXlogCategoryName(__FILE__), &_xlogFileScopeInfo_);                \
    }                                                                          \
    return _xlogLevelToCheck_ >= _xlogCurrentLevel_;                           \
  }())

/**
 * Get a pointer to the LogCategory that will be used by XLOG() statements in
 * this file.
 *
 * This macro is used in the XLOG() implementation, and therefore must be as
 * cheap as possible.  It stores the LogCategory* pointer as a local static
 * variable.  Only the first invocation has to look up the log category by
 * name.  Subsequent invocations re-use the already looked-up LogCategory
 * pointer.
 *
 * Most of this code must live in the macro definition itself, and cannot be
 * moved into a helper function: The getXlogCategoryName() call must be made as
 * part of the macro expansion in order to work correctly.  We also want to
 * avoid calling it whenever possible.  Therefore most of the logic must be
 * done in the macro expansion.
 *
 * See XlogCategoryInfo for the implementation details.
 */
#define XLOG_GET_CATEGORY()                                                  \
  [] {                                                                       \
    static ::folly::XlogCategoryInfo<XLOG_IS_IN_HEADER_FILE> _xlogCategory_; \
    if (!_xlogCategory_.isInitialized()) {                                   \
      return _xlogCategory_.init(getXlogCategoryName(__FILE__));             \
    }                                                                        \
    return _xlogCategory_.getCategory(&_xlogFileScopeInfo_);                 \
  }()

/**
 * XLOG_SET_CATEGORY() can be used to explicitly define the log category name
 * used by all XLOG() and XLOGF() calls in this translation unit.
 *
 * This overrides the default behavior of picking a category name based on the
 * current filename.
 *
 * This should be used at the top-level scope in a .cpp file, before any XLOG()
 * or XLOGF() macros have been used in the file.
 *
 * XLOG_SET_CATEGORY() cannot be used inside header files.
 */
#ifdef __INCLUDE_LEVEL__
#define XLOG_SET_CATEGORY(category)                              \
  namespace {                                                    \
  static_assert(                                                 \
      __INCLUDE_LEVEL__ == 0,                                    \
      "XLOG_SET_CATEGORY() should not be used in header files"); \
  inline std::string getXlogCategoryName(const char*) {          \
    return category;                                             \
  }                                                              \
  }
#else
#define XLOG_SET_CATEGORY(category)                     \
  namespace {                                           \
  inline std::string getXlogCategoryName(const char*) { \
    return category;                                    \
  }                                                     \
  }
#endif

/**
 * XLOG_IS_IN_HEADER_FILE evaluates to false if we can definitively tell if we
 * are not in a header file.  Otherwise, it evaluates to true.
 */
#ifdef __INCLUDE_LEVEL__
#define XLOG_IS_IN_HEADER_FILE bool(__INCLUDE_LEVEL__ > 0)
#else
// Without __INCLUDE_LEVEL__ we canot tell if we are in a header file or not,
// and must pessimstically assume we are always in a header file.
#define XLOG_IS_IN_HEADER_FILE true
#endif

namespace folly {

class XlogFileScopeInfo {
 public:
#ifdef __INCLUDE_LEVEL__
  std::atomic<::folly::LogLevel> level;
  ::folly::LogCategory* category;
#endif
};

/**
 * A file-static XlogLevelInfo and XlogCategoryInfo object is declared for each
 * XLOG() statement.
 *
 * We intentionally do not provide constructors for these structures, and rely
 * on their members to be zero-initialized when the program starts.  This
 * ensures that everything will work as desired even if XLOG() statements are
 * used during dynamic object initialization before main().
 */
template <bool IsInHeaderFile>
struct XlogLevelInfo {
 public:
  inline LogLevel getLevel(XlogFileScopeInfo*, std::memory_order order) {
    return level_.load(order);
  }

  inline LogLevel init(folly::StringPiece categoryName, XlogFileScopeInfo*) {
    return LoggerDB::get()->xlogInit(categoryName, &level_, nullptr);
  }

 private:
  // This member will always be zero-initialized on program start.
  std::atomic<LogLevel> level_;
};

template <bool IsInHeaderFile>
struct XlogCategoryInfo {
 public:
  bool isInitialized() {
    return isInitialized_.load(std::memory_order_acquire);
  }

  LogCategory* init(folly::StringPiece categoryName) {
    return LoggerDB::get()->xlogInitCategory(
        categoryName, &category_, &isInitialized_);
  }

  LogCategory* getCategory(XlogFileScopeInfo*) {
    return category_;
  }

 private:
  // These variables will always be zero-initialized on program start.
  std::atomic<bool> isInitialized_;
  LogCategory* category_;
};

#ifdef __INCLUDE_LEVEL__
/**
 * Specialization of XlogLevelInfo for XLOG() statements in the .cpp file being
 * compiled.  In this case we only define a single file-static LogLevel object
 * for the entire file, rather than defining one for each XLOG() statement.
 */
template <>
struct XlogLevelInfo<false> {
 public:
  inline LogLevel getLevel(
      XlogFileScopeInfo* fileScopeInfo,
      std::memory_order order) {
    return fileScopeInfo->level.load(order);
  }

  inline LogLevel init(
      folly::StringPiece categoryName,
      XlogFileScopeInfo* fileScopeInfo) {
    return LoggerDB::get()->xlogInit(
        categoryName, &fileScopeInfo->level, &fileScopeInfo->category);
  }
};

/**
 * Specialization of XlogCategoryInfo for XLOG() statements in the .cpp file
 * being compiled.  In this case we only define a single file-static LogLevel
 * object for the entire file, rather than defining one for each XLOG()
 * statement.
 */
template <>
struct XlogCategoryInfo<false> {
 public:
  constexpr bool isInitialized() {
    // XlogLevelInfo<false>::check() is always called before XlogCategoryInfo
    // is used, and it will will have already initialized fileScopeInfo.
    // Therefore we never have to check if it is initialized yet here.
    return true;
  }
  LogCategory* init(folly::StringPiece) {
    // This method is never used given that isInitialized() always returns true
    abort();
    return nullptr;
  }
  LogCategory* getCategory(XlogFileScopeInfo* fileScopeInfo) {
    return fileScopeInfo->category;
  }
};
#endif

/**
 * Get the default XLOG() category name for the given filename.
 *
 * This function returns the category name that will be used by XLOG() if
 * XLOG_SET_CATEGORY() has not been used.
 */
std::string getXlogCategoryNameForFile(folly::StringPiece filename);
}

/*
 * We intentionally use an unnamed namespace inside a header file here.
 *
 * We want each .cpp file that uses xlog.h to get its own separate
 * implementation of the following functions and variables.
 */
namespace {
/**
 * The default getXlogCategoryName() implementation.
 * This will be used if XLOG_SET_CATEGORY() has not been used yet.
 *
 * This is a template purely so that XLOG_SET_CATEGORY() can define a more
 * specific version if desired, allowing XLOG_SET_CATEGORY() to override this
 * implementation once it has been used.  The template paramete
 */
template <typename T>
inline std::string getXlogCategoryName(const T* filename) {
  return ::folly::getXlogCategoryNameForFile(filename);
}

/**
 * File-scope LogLevel and LogCategory data for XLOG() statements,
 * if __INCLUDE_LEVEL__ is supported.
 *
 * This allows us to only have one LogLevel and LogCategory pointer for the
 * entire .cpp file, rather than needing a separate copy for each XLOG()
 * statement.
 */
::folly::XlogFileScopeInfo _xlogFileScopeInfo_;
}
