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

#include <memory>

#include <folly/File.h>
#include <folly/Range.h>
#include <folly/experimental/logging/LogHandler.h>

namespace folly {

class LogFormatter;
class LogWriter;

/**
 * StandardLogHandler is a LogHandler implementation that uses a LogFormatter
 * class to serialize the LogMessage into a string, and then gives it to a
 * LogWriter object.
 *
 * This basically is a simple glue class that helps chain together
 * configurable LogFormatter and LogWriter objects.
 *
 * StandardLogHandler also supports ignoring messages less than a specific
 * LogLevel.  By default it processes all messages.
 */
class StandardLogHandler : public LogHandler {
 public:
  StandardLogHandler(
      std::shared_ptr<LogFormatter> formatter,
      std::shared_ptr<LogWriter> writer);
  ~StandardLogHandler();

  /**
   * Get the handler's current LogLevel.
   *
   * Messages less than this LogLevel will be ignored.  This defaults to
   * LogLevel::NONE when the handler is constructed.
   */
  LogLevel getLevel() const {
    return level_.load(std::memory_order_acquire);
  }

  /**
   * Set the handler's current LogLevel.
   *
   * Messages less than this LogLevel will be ignored.
   */
  void setLevel(LogLevel level) {
    return level_.store(level, std::memory_order_release);
  }

  void handleMessage(
      const LogMessage& message,
      const LogCategory* handlerCategory) override;

  void flush() override;

 private:
  std::atomic<LogLevel> level_{LogLevel::NONE};
  std::shared_ptr<LogFormatter> formatter_;
  std::shared_ptr<LogWriter> writer_;
};
}
