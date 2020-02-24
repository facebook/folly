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

#include <folly/logging/xlog.h>

#include <folly/logging/BridgeFromGoogleLogging.h>
#include <folly/logging/LogConfigParser.h>
#include <folly/logging/LogHandler.h>
#include <folly/logging/LogMessage.h>
#include <folly/logging/LoggerDB.h>
#include <folly/logging/test/TestLogHandler.h>
#include <folly/portability/GTest.h>
#include <folly/test/TestUtils.h>

using namespace folly;
using std::make_shared;

namespace {
class BridgeFromGoogleLoggingTest : public testing::Test {
 public:
  BridgeFromGoogleLoggingTest() {
    // Note that the XLOG* macros always use the main LoggerDB singleton.
    // There is no way to get them to use a test LoggerDB during unit tests.
    //
    // In order to ensure that changes to the LoggerDB singleton do not persist
    // across test functions we reset the configuration to a fixed state before
    // each test starts.
    auto config =
        parseLogConfig(".=WARN:default; default=stream:stream=stderr");
    LoggerDB::get().resetConfig(config);
  }
};
} // namespace

TEST_F(BridgeFromGoogleLoggingTest, bridge) {
  auto handler = make_shared<TestLogHandler>();
  logging::BridgeFromGoogleLogging test_bridge{};
  LoggerDB::get().getCategory("")->addHandler(handler);
  auto& messages = handler->getMessages();

  // info messages are not enabled initially.
  EXPECT_FALSE(XLOG_IS_ON(INFO));
  EXPECT_TRUE(XLOG_IS_ON(ERR));
  LOG(INFO) << "testing 1";
  EXPECT_EQ(0, messages.size());
  messages.clear();

  // Increase the log level, then log a message.
  LoggerDB::get().setLevel("", LogLevel::INFO);

  LOG(INFO) << "testing: " << 1 << 2 << 3;
  ASSERT_EQ(1, messages.size());
  EXPECT_EQ("testing: 123", messages[0].first.getMessage());
  EXPECT_TRUE(messages[0].first.getFileName().endsWith(
      "BridgeFromGoogleLoggingTest.cpp"))
      << "unexpected file name: " << messages[0].first.getFileName();
  EXPECT_EQ(LogLevel::INFO, messages[0].first.getLevel());
  messages.clear();
}
