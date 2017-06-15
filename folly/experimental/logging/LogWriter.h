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

#include <folly/Range.h>

namespace folly {

/**
 * LogWriter defines the interface for processing a serialized log message.
 */
class LogWriter {
 public:
  virtual ~LogWriter() {}

  /**
   * Write a serialized log message.
   */
  virtual void writeMessage(folly::StringPiece buffer) = 0;

  /**
   * Write a serialized message.
   *
   * This version of writeMessage() accepts a std::string&&.
   * The default implementation calls the StringPiece version of
   * writeMessage(), but subclasses may override this implementation if
   * desired.
   */
  virtual void writeMessage(std::string&& buffer) {
    writeMessage(folly::StringPiece{buffer});
  }
};
}
