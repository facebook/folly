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

#include <folly/logging/AsyncLogWriter.h>

#include <folly/logging/LoggerDB.h>
#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>
#include <folly/test/TestUtils.h>
#include <iostream>

using namespace folly;

namespace {
static bool* expectedMessage;
void handleLoggingError(
    StringPiece /* file */,
    int /* lineNumber */,
    std::string&& msg) {
  if (folly::kIsDebug) {
    std::cerr << msg << std::endl;
  } else {
    *expectedMessage = (msg == "cleanup() is not called before destroying");
  }
}

class NoCleanUpLogWriter : public AsyncLogWriter {
  void performIO(const std::vector<std::string>&, size_t) override {}

  bool ttyOutput() const override {
    return false;
  }
};
} // namespace

TEST(AsyncLogWriterDeathTest, cleanupWarning) {
  bool flag;
  expectedMessage = &flag;

  LoggerDB::setInternalWarningHandler(handleLoggingError);

  if (folly::kIsDebug) {
    EXPECT_DEATH(
        { NoCleanUpLogWriter{}; },
        "cleanup\\(\\) is not called before destroying");
  } else {
    { NoCleanUpLogWriter{}; }
    EXPECT_TRUE(flag);
  }
}
