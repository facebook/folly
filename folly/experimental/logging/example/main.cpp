/*
 * Copyright 2017-present Facebook, Inc.
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
#include <folly/portability/GFlags.h>

#include <folly/experimental/logging/example/lib.h>

DEFINE_string(logging, "", "Logging category configuration string");

using namespace example;
using folly::LogLevel;

// Invoking code that uses XLOG() statements before main() is safe.
// This will use default log settings defined by folly::initializeLoggerDB().
static ExampleObject staticInitialized("static");

namespace folly {
const char* getBaseLoggingConfig() {
  // Configure folly to enable INFO+ messages, and everything else to
  // enable WARNING+.
  //
  // Set the default log handler to log asynchronously by default.
  return ".=WARNING,folly=INFO; default:async=true";
}
} // namespace folly

int main(int argc, char* argv[]) {
  // Using log macros before calling folly::initLogging() will use the default
  // log settings defined by folly::initializeLoggerDB().  The default behavior
  // is to log WARNING+ messages to stderr.
  XLOG(INFO) << "log messages less than WARNING will be ignored";
  XLOG(ERR) << "error messages before initLogging() will be logged to stderr";

  // Call folly::init() and then initialize log levels and handlers
  folly::init(&argc, &argv);
  folly::initLogging(FLAGS_logging);

  // All XLOG() statements in this file will log to the category
  // folly.experimental.logging.example.main
  XLOG(INFO, "now the normal log settings have been applied");

  XLOG(DBG1, "log arguments are concatenated: ", 12345, ", ", 92.0);
  XLOGF(DBG1, "XLOGF supports {}-style formatting: {:.3f}", "python", 1.0 / 3);
  XLOG(DBG2) << "streaming syntax is also supported: " << 1234;
  XLOG(DBG2, "if you really want, ", "you can even")
      << " mix function-style and streaming syntax: " << 42;
  XLOGF(DBG3, "and {} can mix {} style", "you", "format") << " and streaming";

  ExampleObject("foo");
  XLOG(INFO) << "main returning";
  return 0;
}
