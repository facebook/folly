/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <vector>

#include <folly/logging/AsyncLogWriter.h>

namespace folly {
/**
 * An implementation of `folly::AsyncLogWriter` that writes log messages into a
 * file.
 *
 * See `folly::AsyncLogWriter` for details on asynchronous IO.
 */
class AsyncFileWriter : public AsyncLogWriter {
 public:
  /**
   * Construct an AsyncFileWriter that appends to the file at the specified
   * path.
   */
  explicit AsyncFileWriter(folly::StringPiece path);

  /**
   * Construct an AsyncFileWriter that writes to the specified File object.
   */
  explicit AsyncFileWriter(folly::File&& file);

  ~AsyncFileWriter();

  /**
   * Returns true if the output steam is a tty.
   */
  bool ttyOutput() const override;

  /**
   * Get the output file.
   */
  const folly::File& getFile() const { return file_; }

 private:
  void writeToFile(
      const std::vector<std::string>& ioQueue, size_t numDiscarded);

  void performIO(
      const std::vector<std::string>& ioQueue, size_t numDiscarded) override;

  std::string getNumDiscardedMsg(size_t numDiscarded);

  folly::File file_;
};
} // namespace folly
