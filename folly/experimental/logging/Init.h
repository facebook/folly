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

/*
 * This file contains function to help configure the logging library behavior
 * during program start-up.
 */

#include <stdexcept>
#include <string>
#include <vector>

#include <folly/Range.h>
#include <folly/experimental/logging/LogLevel.h>

namespace folly {

/**
 * Configure log category levels based on a configuration string.
 *
 * This can be used to process a logging configuration string (such as received
 * via a command line flag) during program start-up.
 */
void initLogLevels(
    folly::StringPiece configString = "",
    LogLevel defaultRootLevel = LogLevel::WARNING);

/**
 * Initialize the logging library to write glog-style messages to stderr.
 *
 * This initializes the log category levels as specified (using
 * initLogLevels()), and adds a log handler that prints messages in glog-style
 * format to stderr.
 */
void initLoggingGlogStyle(
    folly::StringPiece configString = "",
    LogLevel defaultRootLevel = LogLevel::WARNING,
    bool asyncWrites = true);

/**
 * LoggingConfigError may be thrown by initLogLevels() if an error occurs
 * parsing the configuration string.
 */
class LoggingConfigError : public std::invalid_argument {
 public:
  explicit LoggingConfigError(const std::vector<std::string>& errors);

 private:
  std::string computeMessage(const std::vector<std::string>& errors);
};
}
