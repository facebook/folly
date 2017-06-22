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
#include <folly/experimental/logging/Init.h>

#include <folly/experimental/logging/AsyncFileWriter.h>
#include <folly/experimental/logging/GlogStyleFormatter.h>
#include <folly/experimental/logging/ImmediateFileWriter.h>
#include <folly/experimental/logging/LogCategory.h>
#include <folly/experimental/logging/LoggerDB.h>
#include <folly/experimental/logging/StandardLogHandler.h>

using std::shared_ptr;
using std::string;
using std::vector;

namespace folly {

void initLogLevels(StringPiece configString, LogLevel defaultRootLevel) {
  // Set the default root category log level first
  LoggerDB::get()->getCategory(".")->setLevel(defaultRootLevel);

  // Then apply the configuration string
  if (!configString.empty()) {
    auto ret = LoggerDB::get()->processConfigString(configString);
    if (!ret.empty()) {
      throw LoggingConfigError(ret);
    }
  }
}

void initLoggingGlogStyle(
    StringPiece configString,
    LogLevel defaultRootLevel,
    bool asyncWrites) {
  // Configure log levels
  initLogLevels(configString, defaultRootLevel);

  // Create the LogHandler
  std::shared_ptr<LogWriter> writer;
  folly::File file{STDERR_FILENO, false};
  if (asyncWrites) {
    writer = std::make_shared<AsyncFileWriter>(std::move(file));
  } else {
    writer = std::make_shared<ImmediateFileWriter>(std::move(file));
  }
  auto handler = std::make_shared<StandardLogHandler>(
      std::make_shared<GlogStyleFormatter>(), std::move(writer));

  // Add the handler to the root category.
  LoggerDB::get()->getCategory(".")->addHandler(std::move(handler));
}

LoggingConfigError::LoggingConfigError(const vector<string>& errors)
    : invalid_argument{computeMessage(errors)} {}

std::string LoggingConfigError::computeMessage(const vector<string>& errors) {
  string msg = "error parsing logging configuration:";
  for (const auto& error : errors) {
    msg += "\n" + error;
  }
  return msg;
}
}
