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

#include <folly/experimental/logging/LogLevel.h>

namespace folly {

class LogCategory;
class LogMessage;

/**
 * LogHandler represents a generic API for processing log messages.
 *
 * LogHandlers have an associated log level.  The LogHandler will discard any
 * messages below its log level.  This allows specific LogHandlers to perform
 * additional filtering of messages even if the messages were enabled at the
 * LogCategory level.  For instance, a single LogCategory may have two
 * LogHandlers attached, one that logs locally to a file, and one that sends
 * messages to a remote logging service.  The local LogHandler may be
 * configured to record all messages, but the remote LogHandler may want to
 * only process ERROR messages and above, even when debug logging is enabled
 * for this LogCategory.
 *
 * By default the LogHandler level is set to LogLevel::NONE, which means that
 * all log messages will be processed.
 */
class LogHandler {
 public:
  LogHandler() = default;
  virtual ~LogHandler() = default;

  /**
   * log() is called when a log message is processed by a LogCategory that this
   * handler is attached to.
   *
   * log() performs a level check, and calls handleMessage() if it passes.
   *
   * @param message The LogMessage objet.
   * @param handlerCategory  The LogCategory that invoked log().  This is the
   *     category that this LogHandler is attached to.  Note that this may be
   *     different than the category that this message was originally logged
   *     at.  message->getCategory() returns the category of the log message.
   */
  void log(const LogMessage& message, const LogCategory* handlerCategory);

  LogLevel getLevel() const {
    return level_.load(std::memory_order_acquire);
  }
  void setLevel(LogLevel level) {
    return level_.store(level, std::memory_order_release);
  }

 protected:
  /**
   * handleMessage() is invoked to process a LogMessage.
   *
   * This must be implemented by LogHandler subclasses.
   *
   * handleMessage() will always be invoked from the thread that logged the
   * message.  LogMessage::getThreadID() contains the thread ID, but the
   * LogHandler can also include any other thread-local state they desire, and
   * this will always be data for the thread that originated the log message.
   */
  virtual void handleMessage(
      const LogMessage& message,
      const LogCategory* handlerCategory) = 0;

 private:
  std::atomic<LogLevel> level_{LogLevel::NONE};
};
}
