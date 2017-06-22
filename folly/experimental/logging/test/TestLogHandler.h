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

#include <utility>
#include <vector>

#include <folly/experimental/logging/LogHandler.h>
#include <folly/experimental/logging/LogMessage.h>

namespace folly {

/**
 * A LogHandler that simply keeps a vector of all LogMessages it receives.
 *
 * This class is not thread-safe.  It is intended to be used in single-threaded
 * tests.
 */
class TestLogHandler : public LogHandler {
 public:
  std::vector<std::pair<LogMessage, const LogCategory*>>& getMessages() {
    return messages_;
  }

  void handleMessage(
      const LogMessage& message,
      const LogCategory* handlerCategory) override {
    messages_.emplace_back(message, handlerCategory);
  }

  void flush() override {
    ++flushCount_;
  }

  uint64_t getFlushCount() const {
    return flushCount_;
  }

 private:
  std::vector<std::pair<LogMessage, const LogCategory*>> messages_;
  uint64_t flushCount_{0};
};
}
