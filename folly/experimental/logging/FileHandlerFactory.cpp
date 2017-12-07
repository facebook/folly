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
#include <folly/experimental/logging/FileHandlerFactory.h>

#include <folly/Conv.h>
#include <folly/experimental/logging/AsyncFileWriter.h>
#include <folly/experimental/logging/ImmediateFileWriter.h>
#include <folly/experimental/logging/StandardLogHandler.h>
#include <folly/experimental/logging/StandardLogHandlerFactory.h>

using std::make_shared;
using std::shared_ptr;
using std::string;

namespace folly {

class FileHandlerFactory::WriterFactory
    : public StandardLogHandlerFactory::WriterFactory {
 public:
  bool processOption(StringPiece name, StringPiece value) override {
    if (name == "path") {
      if (!stream_.empty()) {
        throw std::invalid_argument(
            "cannot specify both \"path\" and \"stream\" "
            "parameters for FileHandlerFactory");
      }
      path_ = value.str();
      return true;
    } else if (name == "stream") {
      if (!path_.empty()) {
        throw std::invalid_argument(
            "cannot specify both \"path\" and \"stream\" "
            "parameters for FileHandlerFactory");
      }
      stream_ = value.str();
      return true;
    } else if (name == "async") {
      async_ = to<bool>(value);
      return true;
    } else if (name == "max_buffer_size") {
      auto size = to<size_t>(value);
      if (size == 0) {
        throw std::invalid_argument(to<string>("must be a postive number"));
      }
      maxBufferSize_ = size;
      return true;
    } else {
      return false;
    }
  }

  std::shared_ptr<LogWriter> createWriter() override {
    // Get the output file to use
    File outputFile;
    if (!path_.empty()) {
      outputFile = File{path_, O_WRONLY | O_APPEND | O_CREAT};
    } else if (stream_ == "stderr") {
      outputFile = File{STDERR_FILENO, /* ownsFd */ false};
    } else if (stream_ == "stdout") {
      outputFile = File{STDOUT_FILENO, /* ownsFd */ false};
    } else {
      throw std::invalid_argument(to<string>(
          "unknown stream for FileHandlerFactory: \"",
          stream_,
          "\" expected one of stdout or stderr"));
    }

    // Determine whether we should use ImmediateFileWriter or AsyncFileWriter
    if (async_) {
      auto asyncWriter = make_shared<AsyncFileWriter>(std::move(outputFile));
      if (maxBufferSize_.hasValue()) {
        asyncWriter->setMaxBufferSize(maxBufferSize_.value());
      }
      return asyncWriter;
    } else {
      if (maxBufferSize_.hasValue()) {
        throw std::invalid_argument(to<string>(
            "the \"max_buffer_size\" option is only valid for async file "
            "handlers"));
      }
      return make_shared<ImmediateFileWriter>(std::move(outputFile));
    }
  }

  std::string path_;
  std::string stream_;
  bool async_{true};
  Optional<size_t> maxBufferSize_;
};

std::shared_ptr<LogHandler> FileHandlerFactory::createHandler(
    const Options& options) {
  WriterFactory writerFactory;
  return StandardLogHandlerFactory::createHandler(
      getType(), &writerFactory, options);
}

} // namespace folly
