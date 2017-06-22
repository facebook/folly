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
#include <folly/experimental/logging/LogStreamProcessor.h>

#include <folly/experimental/logging/LogStream.h>
#include <folly/experimental/logging/xlog.h>

namespace folly {

LogStreamProcessor::LogStreamProcessor(
    const LogCategory* category,
    LogLevel level,
    folly::StringPiece filename,
    unsigned int lineNumber,
    AppendType) noexcept
    : LogStreamProcessor(
          category,
          level,
          filename,
          lineNumber,
          INTERNAL,
          std::string()) {}

LogStreamProcessor::LogStreamProcessor(
    XlogCategoryInfo<true>* categoryInfo,
    LogLevel level,
    folly::StringPiece categoryName,
    bool isCategoryNameOverridden,
    folly::StringPiece filename,
    unsigned int lineNumber,
    AppendType) noexcept
    : LogStreamProcessor(
          categoryInfo,
          level,
          categoryName,
          isCategoryNameOverridden,
          filename,
          lineNumber,
          INTERNAL,
          std::string()) {}

LogStreamProcessor::LogStreamProcessor(
    XlogFileScopeInfo* fileScopeInfo,
    LogLevel level,
    folly::StringPiece filename,
    unsigned int lineNumber,
    AppendType) noexcept
    : LogStreamProcessor(
          fileScopeInfo,
          level,
          filename,
          lineNumber,
          INTERNAL,
          std::string()) {}

LogStreamProcessor::LogStreamProcessor(
    const LogCategory* category,
    LogLevel level,
    folly::StringPiece filename,
    unsigned int lineNumber,
    InternalType,
    std::string&& msg) noexcept
    : category_{category},
      level_{level},
      filename_{filename},
      lineNumber_{lineNumber},
      message_{std::move(msg)},
      stream_{this} {}

namespace {
LogCategory* getXlogCategory(
    XlogCategoryInfo<true>* categoryInfo,
    folly::StringPiece categoryName,
    bool isCategoryNameOverridden) {
  if (!categoryInfo->isInitialized()) {
    return categoryInfo->init(categoryName, isCategoryNameOverridden);
  }
  return categoryInfo->getCategory(&xlog_detail::xlogFileScopeInfo);
}

LogCategory* getXlogCategory(XlogFileScopeInfo* fileScopeInfo) {
  // By the time a LogStreamProcessor is created, the XlogFileScopeInfo object
  // should have already been initialized to perform the log level check.
  // Therefore we never need to check if it is initialized here.
  return fileScopeInfo->category;
}
}

/**
 * Construct a LogStreamProcessor from an XlogCategoryInfo.
 *
 * We intentionally define this in LogStreamProcessor.cpp instead of
 * LogStreamProcessor.h to avoid having it inlined at every XLOG() call site,
 * to reduce the emitted code size.
 */
LogStreamProcessor::LogStreamProcessor(
    XlogCategoryInfo<true>* categoryInfo,
    LogLevel level,
    folly::StringPiece categoryName,
    bool isCategoryNameOverridden,
    folly::StringPiece filename,
    unsigned int lineNumber,
    InternalType,
    std::string&& msg) noexcept
    : category_{getXlogCategory(
          categoryInfo,
          categoryName,
          isCategoryNameOverridden)},
      level_{level},
      filename_{filename},
      lineNumber_{lineNumber},
      message_{std::move(msg)},
      stream_{this} {}

/**
 * Construct a LogStreamProcessor from an XlogFileScopeInfo.
 *
 * We intentionally define this in LogStreamProcessor.cpp instead of
 * LogStreamProcessor.h to avoid having it inlined at every XLOG() call site,
 * to reduce the emitted code size.
 */
LogStreamProcessor::LogStreamProcessor(
    XlogFileScopeInfo* fileScopeInfo,
    LogLevel level,
    folly::StringPiece filename,
    unsigned int lineNumber,
    InternalType,
    std::string&& msg) noexcept
    : category_{getXlogCategory(fileScopeInfo)},
      level_{level},
      filename_{filename},
      lineNumber_{lineNumber},
      message_{std::move(msg)},
      stream_{this} {}

/*
 * We intentionally define the LogStreamProcessor destructor in
 * LogStreamProcessor.cpp instead of LogStreamProcessor.h to avoid having it
 * emitted inline at every log statement site.  This helps reduce the emitted
 * code size for each log statement.
 */
LogStreamProcessor::~LogStreamProcessor() noexcept {
  // The LogStreamProcessor destructor is responsible for logging the message.
  // Doing this in the destructor avoids an separate function call to log the
  // message being emitted inline at every log statement site.
  logNow();
}

void LogStreamProcessor::logNow() noexcept {
  // Note that admitMessage() is not noexcept and theoretically may throw.
  // However, the only exception that should be possible is std::bad_alloc if
  // we fail to allocate memory.  We intentionally let our noexcept specifier
  // crash in that case, since the program likely won't be able to continue
  // anyway.
  //
  // Any other error here is unexpected and we also want to fail hard
  // in that situation too.
  category_->admitMessage(LogMessage{category_,
                                     level_,
                                     filename_,
                                     lineNumber_,
                                     extractMessageString(stream_)});
}

void LogStreamVoidify<true>::operator&(std::ostream& stream) {
  // Non-fatal log messages wait until the LogStreamProcessor destructor to log
  // the message.  However for fatal messages we log immediately in the &
  // operator, since it is marked noreturn.
  //
  // This does result in slightly larger emitted code for fatal log messages
  // (since the operator & call cannot be completely omitted).  However, fatal
  // log messages should typically be much more rare than non-fatal messages,
  // so the small amount of extra overhead shouldn't be a big deal.
  auto& logStream = static_cast<LogStream&>(stream);
  logStream.getProcessor()->logNow();
  abort();
}

std::string LogStreamProcessor::extractMessageString(
    LogStream& stream) noexcept {
  if (stream.empty()) {
    return std::move(message_);
  }

  if (message_.empty()) {
    return stream.extractString();
  }
  message_.append(stream.extractString());
  return std::move(message_);
}
}
