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
#include <folly/experimental/logging/xlog.h>
#include <folly/init/Init.h>

DEFINE_string(logging, "", "Logging category configuration string");
DEFINE_string(
    handler_style,
    "async",
    "Log handler style: async, immediate, or none");

DEFINE_string(
    category,
    "",
    "Crash with a message to this category instead of the default");

using folly::LogLevel;

/*
 * This is a simple helper program to exercise the LOG(FATAL) functionality.
 */
int main(int argc, char* argv[]) {
  // Call folly::init() and then initialize log levels and handlers
  folly::init(&argc, &argv);

  if (FLAGS_handler_style == "async") {
    initLoggingGlogStyle(FLAGS_logging, LogLevel::INFO, true);
  } else if (FLAGS_handler_style == "immediate") {
    initLoggingGlogStyle(FLAGS_logging, LogLevel::INFO, false);
  } else if (FLAGS_handler_style != "none") {
    XLOGF(FATAL, "unknown log handler style \"{}\"", FLAGS_handler_style);
  }

  if (!FLAGS_category.empty()) {
    folly::Logger logger{FLAGS_category};
    FB_LOG(logger, FATAL, "crashing to category ", FLAGS_category);
  }

  XLOG(FATAL) << "test program crashing!";
  // Even though main() is defined to return an integer, the compiler
  // should be able to detect that XLOG(FATAL) never returns.  It shouldn't
  // complain that we don't return an integer here.
}
