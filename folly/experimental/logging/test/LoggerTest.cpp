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
#include <folly/experimental/logging/LogCategory.h>
#include <folly/experimental/logging/LogHandler.h>
#include <folly/experimental/logging/LogMessage.h>
#include <folly/experimental/logging/Logger.h>
#include <folly/experimental/logging/LoggerDB.h>
#include <folly/experimental/logging/test/TestLogHandler.h>
#include <folly/portability/GTest.h>

using namespace folly;
using std::make_shared;

class LoggerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto* category = logger_.getCategory();

    handler_ = make_shared<TestLogHandler>();
    category->addHandler(handler_);
    category->setLevel(LogLevel::DEBUG, true);
  }

  LoggerDB db_{LoggerDB::TESTING};
  Logger logger_{&db_, "test"};
  std::shared_ptr<TestLogHandler> handler_;
};

TEST_F(LoggerTest, basic) {
  // Simple log message
  logger_.log(LogLevel::WARN, "src/myproject/myfile.cpp", 1234, "hello world");

  auto& messages = handler_->getMessages();
  ASSERT_EQ(1, messages.size());
  EXPECT_EQ("hello world", messages[0].first.getMessage());
  EXPECT_EQ("src/myproject/myfile.cpp", messages[0].first.getFileName());
  EXPECT_EQ(1234, messages[0].first.getLineNumber());
  EXPECT_EQ(LogLevel::WARN, messages[0].first.getLevel());
  EXPECT_FALSE(messages[0].first.containsNewlines());
  EXPECT_EQ(logger_.getCategory(), messages[0].first.getCategory());
  EXPECT_EQ(logger_.getCategory(), messages[0].second);
}

TEST_F(LoggerTest, subCategory) {
  // Log from a sub-category.
  Logger subLogger{&db_, "test.foo.bar"};
  subLogger.log(LogLevel::ERROR, "myfile.cpp", 99, "sub-category\nlog message");

  auto& messages = handler_->getMessages();
  ASSERT_EQ(1, messages.size());
  EXPECT_EQ("sub-category\nlog message", messages[0].first.getMessage());
  EXPECT_EQ("myfile.cpp", messages[0].first.getFileName());
  EXPECT_EQ(99, messages[0].first.getLineNumber());
  EXPECT_EQ(LogLevel::ERROR, messages[0].first.getLevel());
  EXPECT_TRUE(messages[0].first.containsNewlines());
  EXPECT_EQ(subLogger.getCategory(), messages[0].first.getCategory());
  EXPECT_EQ(logger_.getCategory(), messages[0].second);
}

TEST_F(LoggerTest, formatMessage) {
  logger_.logf(
      LogLevel::WARN,
      "log.cpp",
      9,
      "num events: {:06d}, duration: {:6.3f}",
      1234,
      5.6789);

  auto& messages = handler_->getMessages();
  ASSERT_EQ(1, messages.size());
  EXPECT_EQ(
      "num events: 001234, duration:  5.679", messages[0].first.getMessage());
  EXPECT_EQ("log.cpp", messages[0].first.getFileName());
  EXPECT_EQ(9, messages[0].first.getLineNumber());
  EXPECT_EQ(LogLevel::WARN, messages[0].first.getLevel());
  EXPECT_FALSE(messages[0].first.containsNewlines());
  EXPECT_EQ(logger_.getCategory(), messages[0].first.getCategory());
  EXPECT_EQ(logger_.getCategory(), messages[0].second);
}

TEST_F(LoggerTest, follyFormatError) {
  // If we pass in a bogus format string, logf() should not throw.
  // It should instead log a message, just complaining about the format error.
  logger_.logf(
      LogLevel::WARN,
      "log.cpp",
      9,
      "param1: {:06d}, param2: {:6.3f}",
      1234,
      "hello world!");

  auto& messages = handler_->getMessages();
  ASSERT_EQ(1, messages.size());
  EXPECT_EQ(
      "error formatting log message: "
      "invalid format argument {:6.3f}: invalid specifier 'f'; "
      "format string: param1: {:06d}, param2: {:6.3f}",
      messages[0].first.getMessage());
  EXPECT_EQ("log.cpp", messages[0].first.getFileName());
  EXPECT_EQ(9, messages[0].first.getLineNumber());
  EXPECT_EQ(LogLevel::WARN, messages[0].first.getLevel());
  EXPECT_FALSE(messages[0].first.containsNewlines());
  EXPECT_EQ(logger_.getCategory(), messages[0].first.getCategory());
  EXPECT_EQ(logger_.getCategory(), messages[0].second);
}

TEST_F(LoggerTest, toString) {
  // Use the log API that calls folly::to<string>
  logger_.log(LogLevel::DBG5, "log.cpp", 3, "status=", 5, " name=", "foobar");

  auto& messages = handler_->getMessages();
  ASSERT_EQ(1, messages.size());
  EXPECT_EQ("status=5 name=foobar", messages[0].first.getMessage());
  EXPECT_EQ("log.cpp", messages[0].first.getFileName());
  EXPECT_EQ(3, messages[0].first.getLineNumber());
  EXPECT_EQ(LogLevel::DBG5, messages[0].first.getLevel());
  EXPECT_FALSE(messages[0].first.containsNewlines());
  EXPECT_EQ(logger_.getCategory(), messages[0].first.getCategory());
  EXPECT_EQ(logger_.getCategory(), messages[0].second);
}

class ToStringFailure {};

[[noreturn]] void toAppend(
    const ToStringFailure& /* arg */,
    std::string* /* result */) {
  throw std::runtime_error(
      "error converting ToStringFailure object to a string");
}

TEST_F(LoggerTest, toStringError) {
  // Use the folly::to<string> log API, with an object that will throw
  // an exception when we try to convert it to a string.
  //
  // The logging code should not throw, but should instead log a message
  // with some detail about the failure.
  ToStringFailure obj;
  logger_.log(LogLevel::DBG1, "log.cpp", 3, "status=", obj, " name=", "foobar");

  auto& messages = handler_->getMessages();
  ASSERT_EQ(1, messages.size());
  EXPECT_EQ(
      "error constructing log message: "
      "error converting ToStringFailure object to a string",
      messages[0].first.getMessage());
  EXPECT_EQ("log.cpp", messages[0].first.getFileName());
  EXPECT_EQ(3, messages[0].first.getLineNumber());
  EXPECT_EQ(LogLevel::DBG1, messages[0].first.getLevel());
  EXPECT_FALSE(messages[0].first.containsNewlines());
  EXPECT_EQ(logger_.getCategory(), messages[0].first.getCategory());
  EXPECT_EQ(logger_.getCategory(), messages[0].second);
}

TEST_F(LoggerTest, escapeSequences) {
  // Escape characters (and any other unprintable characters) in the log
  // message should be escaped when logged.
  logger_.log(LogLevel::WARN, "termcap.cpp", 34, "hello \033[34mworld\033[0m!");

  auto& messages = handler_->getMessages();
  ASSERT_EQ(1, messages.size());
  EXPECT_EQ("hello \\x1b[34mworld\\x1b[0m!", messages[0].first.getMessage());
  EXPECT_EQ("termcap.cpp", messages[0].first.getFileName());
  EXPECT_EQ(34, messages[0].first.getLineNumber());
  EXPECT_EQ(LogLevel::WARN, messages[0].first.getLevel());
  EXPECT_FALSE(messages[0].first.containsNewlines());
  EXPECT_EQ(logger_.getCategory(), messages[0].first.getCategory());
  EXPECT_EQ(logger_.getCategory(), messages[0].second);
}

TEST_F(LoggerTest, logMacros) {
  Logger foo{&db_, "test.foo.bar"};
  Logger foobar{&db_, "test.foo.bar"};
  Logger footest{&db_, "test.foo.test"};
  Logger footest1234{&db_, "test.foo.test.1234"};
  Logger other{&db_, "test.other"};
  db_.setLevel("test", LogLevel::ERROR);
  db_.setLevel("test.foo", LogLevel::DBG2);
  db_.setLevel("test.foo.test", LogLevel::DBG7);

  auto& messages = handler_->getMessages();

  // test.other's effective level should be ERROR, so a warning
  // message to it should be discarded
  FB_LOG(other, WARN, "this should be discarded");
  ASSERT_EQ(0, messages.size());

  // Disabled log messages should not evaluate their arguments
  bool argumentEvaluated = false;
  auto getValue = [&] {
    argumentEvaluated = true;
    return 5;
  };
  FB_LOG(foobar, DBG3, "discarded message: ", getValue());
  EXPECT_FALSE(argumentEvaluated);

  FB_LOG(foobar, DBG1, "this message should pass: ", getValue());
  ASSERT_EQ(1, messages.size());
  EXPECT_EQ("this message should pass: 5", messages[0].first.getMessage());
  EXPECT_TRUE(argumentEvaluated);
  messages.clear();

  // Similar checks with FB_LOGF()
  argumentEvaluated = false;
  FB_LOGF(footest1234, DBG9, "failing log check: {}", getValue());
  EXPECT_FALSE(argumentEvaluated);

  FB_LOGF(footest1234, DBG5, "passing log: {:03}", getValue());
  ASSERT_EQ(1, messages.size());
  EXPECT_EQ("passing log: 005", messages[0].first.getMessage());
  EXPECT_TRUE(argumentEvaluated);
  messages.clear();

  // Bad format arguments should not throw
  FB_LOGF(footest1234, ERROR, "whoops: {}, {}", getValue());
  ASSERT_EQ(1, messages.size());
  EXPECT_EQ(
      "error formatting log message: "
      "invalid format argument {}: argument index out of range, max=1; "
      "format string: whoops: {}, {}",
      messages[0].first.getMessage());
  messages.clear();
}
