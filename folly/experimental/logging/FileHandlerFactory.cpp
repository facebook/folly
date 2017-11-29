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

#include <set>

#include <folly/Conv.h>
#include <folly/MapUtil.h>
#include <folly/experimental/logging/AsyncFileWriter.h>
#include <folly/experimental/logging/GlogStyleFormatter.h>
#include <folly/experimental/logging/ImmediateFileWriter.h>
#include <folly/experimental/logging/StandardLogHandler.h>

using std::make_shared;
using std::shared_ptr;
using std::string;

namespace folly {

std::shared_ptr<LogHandler> FileHandlerFactory::createHandler(
    const Options& options) {
  // Raise an error if we receive unexpected options
  auto knownOptions =
      std::set<std::string>{"path", "stream", "async", "max_buffer_size"};
  for (const auto& entry : options) {
    if (knownOptions.find(entry.first) == knownOptions.end()) {
      throw std::invalid_argument(to<string>(
          "unknown parameter \"", entry.first, "\" for FileHandlerFactory"));
    }
  }

  // Construct the formatter
  // TODO: We should eventually support parameters to control the formatter
  // behavior.
  auto formatter = make_shared<GlogStyleFormatter>();

  // Get the output file to use
  File outputFile;
  auto* path = get_ptr(options, "path");
  auto* stream = get_ptr(options, "stream");
  if (path && stream) {
    throw std::invalid_argument(
        "cannot specify both \"path\" and \"stream\" "
        "parameters for FileHandlerFactory");
  } else if (path) {
    outputFile = File{*path, O_WRONLY | O_APPEND | O_CREAT};
  } else if (stream) {
    if (*stream == "stderr") {
      outputFile = File{STDERR_FILENO, /* ownsFd */ false};
    } else if (*stream == "stdout") {
      outputFile = File{STDOUT_FILENO, /* ownsFd */ false};
    } else {
      throw std::invalid_argument(to<string>(
          "unknown stream for FileHandlerFactory: \"",
          *stream,
          "\" expected one of stdout or stderr"));
    }
  } else {
    throw std::invalid_argument(
        "must specify a \"path\" or \"stream\" "
        "parameter for FileHandlerFactory");
  }

  // Determine whether we should use ImmediateFileWriter or AsyncFileWriter
  shared_ptr<LogWriter> writer;
  bool async = true;
  auto* asyncOption = get_ptr(options, "async");
  if (asyncOption) {
    try {
      async = to<bool>(*asyncOption);
    } catch (const std::exception& ex) {
      throw std::invalid_argument(to<string>(
          "expected a boolean value for FileHandlerFactory \"async\" "
          "parameter: ",
          *asyncOption));
    }
  }
  auto* maxBufferOption = get_ptr(options, "max_buffer_size");
  if (async) {
    auto asyncWriter = make_shared<AsyncFileWriter>(std::move(outputFile));
    if (maxBufferOption) {
      size_t maxBufferSize;
      try {
        maxBufferSize = to<size_t>(*maxBufferOption);
      } catch (const std::exception& ex) {
        throw std::invalid_argument(to<string>(
            "expected an integer value for FileHandlerFactory "
            "\"max_buffer_size\": ",
            *maxBufferOption));
      }
      if (maxBufferSize == 0) {
        throw std::invalid_argument(to<string>(
            "expected a positive value for FileHandlerFactory "
            "\"max_buffer_size\": ",
            *maxBufferOption));
      }
      asyncWriter->setMaxBufferSize(maxBufferSize);
    }
    writer = std::move(asyncWriter);
  } else {
    if (maxBufferOption) {
      throw std::invalid_argument(to<string>(
          "the \"max_buffer_size\" option is only valid for async file "
          "handlers"));
    }
    writer = make_shared<ImmediateFileWriter>(std::move(outputFile));
  }

  return make_shared<StandardLogHandler>(formatter, writer);
}

} // namespace folly
