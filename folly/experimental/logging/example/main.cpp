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

// Invoking code that uses XLOG() statements before main is safe,
// but will not log anywhere, since no handlers are configured yet.
static ExampleObject staticInitialized("static");

int main(int argc, char* argv[]) {
  // Using log macros before configuring any log levels or log handlers is
  // safe, but the messages will always be ignore since no handlers are defined.
  XLOG(INFO, "no handlers configured yet, so this will go nowhere");
  printf("main starting\n");
  fflush(stdout);

  // Call folly::init() and then initialize log levels and handlers
  folly::init(&argc, &argv);
  folly::initLogging(FLAGS_logging);

  // All XLOG() statements in this file will log to the category
  // folly.experimental.logging.example.main
  XLOG(INFO, "now log messages will be sent to stderr");

  XLOG(DBG1, "log arguments are concatenated: ", 12345, ", ", 92.0);
  XLOGF(DBG1, "XLOGF supports {}-style formatting: {:.3f}", "python", 1.0 / 3);
  XLOG(DBG2) << "streaming syntax is also supported: " << 1234;
  XLOG(DBG2, "you can even", " mix function-style") << " and streaming "
                                                    << "syntax";

  ExampleObject("foo");
  XLOG(INFO, "main returning");
  return 0;
}
