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
#include <folly/experimental/logging/LoggerDB.h>

#include <set>

#include <folly/Conv.h>
#include <folly/FileUtil.h>
#include <folly/String.h>
#include <folly/experimental/logging/LogCategory.h>
#include <folly/experimental/logging/LogHandler.h>
#include <folly/experimental/logging/LogLevel.h>
#include <folly/experimental/logging/Logger.h>
#include <folly/experimental/logging/RateLimiter.h>

namespace folly {

namespace {
class LoggerDBSingleton {
 public:
  explicit LoggerDBSingleton(LoggerDB* db) : db_{db} {}
  ~LoggerDBSingleton() {
    // We intentionally leak the LoggerDB object on destruction.
    // We want Logger objects to remain valid for the entire lifetime of the
    // program, without having to worry about destruction ordering issues, or
    // making the Logger perform reference counting on the LoggerDB.
    //
    // Therefore the main LoggerDB object, and all of the LogCategory objects
    // it contains, are always intentionally leaked.
    //
    // However, we do call db_->cleanupHandlers() to destroy any registered
    // LogHandler objects.  The LogHandlers can be user-defined objects and may
    // hold resources that should be cleaned up.  This also ensures that the
    // LogHandlers flush all outstanding messages before we exit.
    db_->cleanupHandlers();
  }

  LoggerDB* getDB() const {
    return db_;
  }

 private:
  LoggerDB* db_;
};
}

LoggerDB* LoggerDB::get() {
  // Intentionally leaky singleton
  static LoggerDBSingleton singleton{new LoggerDB()};
  return singleton.getDB();
}

LoggerDB::LoggerDB() {
  // Create the root log category, and set the level to ERR by default
  auto rootUptr = std::make_unique<LogCategory>(this);
  LogCategory* root = rootUptr.get();
  auto ret =
      loggersByName_.wlock()->emplace(root->getName(), std::move(rootUptr));
  DCHECK(ret.second);

  root->setLevelLocked(LogLevel::ERR, false);
}

LoggerDB::LoggerDB(TestConstructorArg) : LoggerDB() {}

LogCategory* LoggerDB::getCategory(StringPiece name) {
  return getOrCreateCategoryLocked(*loggersByName_.wlock(), name);
}

LogCategory* FOLLY_NULLABLE LoggerDB::getCategoryOrNull(StringPiece name) {
  auto loggersByName = loggersByName_.rlock();

  auto it = loggersByName->find(name);
  if (it == loggersByName->end()) {
    return nullptr;
  }
  return it->second.get();
}

void LoggerDB::setLevel(folly::StringPiece name, LogLevel level, bool inherit) {
  auto loggersByName = loggersByName_.wlock();
  LogCategory* category = getOrCreateCategoryLocked(*loggersByName, name);
  category->setLevelLocked(level, inherit);
}

void LoggerDB::setLevel(LogCategory* category, LogLevel level, bool inherit) {
  auto loggersByName = loggersByName_.wlock();
  category->setLevelLocked(level, inherit);
}

std::vector<std::string> LoggerDB::processConfigString(
    folly::StringPiece config) {
  std::vector<std::string> errors;
  if (config.empty()) {
    return errors;
  }

  std::vector<StringPiece> pieces;
  folly::split(",", config, pieces);
  for (const auto& p : pieces) {
    auto idx = p.rfind('=');
    if (idx == folly::StringPiece::npos) {
      errors.emplace_back(
          folly::sformat("missing '=' in logger configuration: \"{}\"", p));
      continue;
    }

    auto category = p.subpiece(0, idx);
    auto level_str = p.subpiece(idx + 1);
    LogLevel level;
    try {
      level = stringToLogLevel(level_str);
    } catch (const std::exception& ex) {
      errors.emplace_back(folly::sformat(
          "invalid log level \"{}\" for category \"{}\"", level_str, category));
      continue;
    }

    setLevel(category, level);
  }

  return errors;
}

LogCategory* LoggerDB::getOrCreateCategoryLocked(
    LoggerNameMap& loggersByName,
    StringPiece name) {
  auto it = loggersByName.find(name);
  if (it != loggersByName.end()) {
    return it->second.get();
  }

  StringPiece parentName = LogName::getParent(name);
  LogCategory* parent = getOrCreateCategoryLocked(loggersByName, parentName);
  return createCategoryLocked(loggersByName, name, parent);
}

LogCategory* LoggerDB::createCategoryLocked(
    LoggerNameMap& loggersByName,
    StringPiece name,
    LogCategory* parent) {
  auto uptr = std::make_unique<LogCategory>(name, parent);
  LogCategory* logger = uptr.get();
  auto ret = loggersByName.emplace(logger->getName(), std::move(uptr));
  DCHECK(ret.second);
  return logger;
}

void LoggerDB::cleanupHandlers() {
  // Get a copy of all categories, so we can call clearHandlers() without
  // holding the loggersByName_ lock.  We don't need to worry about LogCategory
  // lifetime, since LogCategory objects always live for the lifetime of the
  // LoggerDB.
  std::vector<LogCategory*> categories;
  {
    auto loggersByName = loggersByName_.wlock();
    categories.reserve(loggersByName->size());
    for (const auto& entry : *loggersByName) {
      categories.push_back(entry.second.get());
    }
  }

  for (auto* category : categories) {
    category->clearHandlers();
  }
}

size_t LoggerDB::flushAllHandlers() {
  // Build a set of all LogHandlers.  We use a set to avoid calling flush()
  // more than once on the same handler if it is registered on multiple
  // different categories.
  std::set<std::shared_ptr<LogHandler>> handlers;
  {
    auto loggersByName = loggersByName_.wlock();
    for (const auto& entry : *loggersByName) {
      for (const auto& handler : entry.second->getHandlers()) {
        handlers.emplace(handler);
      }
    }
  }

  // Call flush() on each handler
  for (const auto& handler : handlers) {
    handler->flush();
  }
  return handlers.size();
}

LogLevel LoggerDB::xlogInit(
    StringPiece categoryName,
    std::atomic<LogLevel>* xlogCategoryLevel,
    LogCategory** xlogCategory) {
  // Hold the lock for the duration of the operation
  // xlogInit() may be called from multiple threads simultaneously.
  // Only one needs to perform the initialization.
  auto loggersByName = loggersByName_.wlock();
  if (xlogCategory != nullptr && *xlogCategory != nullptr) {
    // The xlogCategory was already initialized before we acquired the lock
    return (*xlogCategory)->getEffectiveLevel();
  }

  auto* category = getOrCreateCategoryLocked(*loggersByName, categoryName);
  if (xlogCategory) {
    // Set *xlogCategory before we update xlogCategoryLevel below.
    // This is important, since the XLOG() macros check xlogCategoryLevel to
    // tell if *xlogCategory has been initialized yet.
    *xlogCategory = category;
  }
  auto level = category->getEffectiveLevel();
  xlogCategoryLevel->store(level, std::memory_order_release);
  category->registerXlogLevel(xlogCategoryLevel);
  return level;
}

LogCategory* LoggerDB::xlogInitCategory(
    StringPiece categoryName,
    LogCategory** xlogCategory,
    std::atomic<bool>* isInitialized) {
  // Hold the lock for the duration of the operation
  // xlogInitCategory() may be called from multiple threads simultaneously.
  // Only one needs to perform the initialization.
  auto loggersByName = loggersByName_.wlock();
  if (isInitialized->load(std::memory_order_acquire)) {
    // The xlogCategory was already initialized before we acquired the lock
    return *xlogCategory;
  }

  auto* category = getOrCreateCategoryLocked(*loggersByName, categoryName);
  *xlogCategory = category;
  isInitialized->store(true, std::memory_order_release);
  return category;
}

std::atomic<LoggerDB::InternalWarningHandler> LoggerDB::warningHandler_;

void LoggerDB::internalWarningImpl(
    folly::StringPiece filename,
    int lineNumber,
    std::string&& msg) noexcept {
  auto handler = warningHandler_.load();
  if (handler) {
    handler(filename, lineNumber, std::move(msg));
  } else {
    defaultInternalWarningImpl(filename, lineNumber, std::move(msg));
  }
}

void LoggerDB::setInternalWarningHandler(InternalWarningHandler handler) {
  // This API is intentionally pretty basic.  It has a number of limitations:
  //
  // - We only support plain function pointers, and not full std::function
  //   objects.  This makes it possible to use std::atomic to access the
  //   handler pointer, and also makes it safe to store in a zero-initialized
  //   file-static pointer.
  //
  // - We don't support any void* argument to the handler.  The caller is
  //   responsible for storing any callback state themselves.
  //
  // - When replacing or unsetting a handler we don't make any guarantees about
  //   when the old handler will stop being called.  It may still be called
  //   from other threads briefly even after setInternalWarningHandler()
  //   returns.  This is also a consequence of using std::atomic rather than a
  //   full lock.
  //
  // This provides the minimum capabilities needed to customize the handler,
  // while still keeping the implementation simple and safe to use even before
  // main().
  warningHandler_.store(handler);
}

void LoggerDB::defaultInternalWarningImpl(
    folly::StringPiece filename,
    int lineNumber,
    std::string&& msg) noexcept {
  // Rate limit to 10 messages every 5 seconds.
  //
  // We intentonally use a leaky Meyer's singleton here over folly::Singleton:
  // - We want this code to work even before main()
  // - This singleton does not depend on any other singletons.
  static auto* rateLimiter =
      new logging::IntervalRateLimiter{10, std::chrono::seconds(5)};
  if (!rateLimiter->check()) {
    return;
  }

  if (folly::kIsDebug) {
    // Write directly to file descriptor 2.
    //
    // It's possible the application has closed fd 2 and is using it for
    // something other than stderr.  However we have no good way to detect
    // this, which is the main reason we only write to stderr in debug build
    // modes.  assert() also writes directly to stderr on failure, which seems
    // like a reasonable precedent.
    //
    // Another option would be to use openlog() and syslog().  However
    // calling openlog() may inadvertently affect the behavior of other parts
    // of the program also using syslog().
    //
    // We don't check for write errors here, since there's not much else we can
    // do if it fails.
    auto fullMsg = folly::to<std::string>(
        "logging warning:", filename, ":", lineNumber, ": ", msg, "\n");
    folly::writeFull(STDERR_FILENO, fullMsg.data(), fullMsg.size());
  }
}
}
