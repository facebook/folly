/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <folly/io/async/AsyncTransport.h>

namespace folly {

/**
 * Wrapper class for WriteCallback that includes a boolean variable to track
 * whether the write has already started or not
 */
class WriteCallbackWithState {
 public:
  explicit WriteCallbackWithState(AsyncWriter::WriteCallback* callback)
      : callback_(callback) {}
  AsyncWriter::WriteCallback* getCallback() const { return callback_; }

  void notifyOnWrite() noexcept {
    if (callback_ && !writeInProgress_) {
      callback_->writeStarting();
    }
    writeInProgress_ = true;
  }

 private:
  AsyncWriter::WriteCallback* callback_{nullptr};
  bool writeInProgress_{false};
};

} // namespace folly
