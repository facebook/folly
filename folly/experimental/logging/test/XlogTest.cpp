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
#include <folly/experimental/logging/LoggerDB.h>
#include <folly/experimental/logging/test/TestLogHandler.h>
#include <folly/experimental/logging/test/XlogHeader1.h>
#include <folly/experimental/logging/test/XlogHeader2.h>
#include <folly/experimental/logging/xlog.h>
#include <folly/portability/GTest.h>
#include <folly/test/TestUtils.h>

using namespace folly;
using std::make_shared;

XLOG_SET_CATEGORY_NAME("xlog_test.main_file");

// Note that the XLOG* macros always use the main LoggerDB singleton.
// There is no way to get them to use a test LoggerDB during unit tests.
//
// Therefore any configuration we do here affects the main log category
// settings for the entire program.  Fortunately all of the other unit tests do
// use testing LoggerDB objects.

TEST(Xlog, xlogName) {
  EXPECT_EQ("xlog_test.main_file", XLOG_GET_CATEGORY_NAME());
  EXPECT_EQ("xlog_test.main_file", XLOG_GET_CATEGORY()->getName());
}

TEST(Xlog, xlog) {
  auto handler = make_shared<TestLogHandler>();
  LoggerDB::get()->getCategory("xlog_test")->addHandler(handler);
  auto& messages = handler->getMessages();

  // info messages are not enabled initially.
  EXPECT_FALSE(XLOG_IS_ON(INFO));
  EXPECT_TRUE(XLOG_IS_ON(ERR));
  XLOG(INFO, "testing 1");
  EXPECT_EQ(0, messages.size());
  messages.clear();

  // Increase the log level, then log a message.
  LoggerDB::get()->setLevel("xlog_test.main_file", LogLevel::DBG1);

  XLOG(DBG1, "testing: ", 1, 2, 3);
  ASSERT_EQ(1, messages.size());
  EXPECT_EQ("testing: 123", messages[0].first.getMessage());
  EXPECT_TRUE(messages[0].first.getFileName().endsWith("XlogTest.cpp"))
      << "unexpected file name: " << messages[0].first.getFileName();
  EXPECT_EQ(LogLevel::DBG1, messages[0].first.getLevel());
  EXPECT_EQ("xlog_test.main_file", messages[0].first.getCategory()->getName());
  EXPECT_EQ("xlog_test", messages[0].second->getName());
  messages.clear();

  XLOGF(WARN, "number: {:>3d}; string: {}", 12, "foo");
  ASSERT_EQ(1, messages.size());
  EXPECT_EQ("number:  12; string: foo", messages[0].first.getMessage());
  EXPECT_TRUE(messages[0].first.getFileName().endsWith("XlogTest.cpp"))
      << "unexpected file name: " << messages[0].first.getFileName();
  EXPECT_EQ(LogLevel::WARN, messages[0].first.getLevel());
  EXPECT_EQ("xlog_test.main_file", messages[0].first.getCategory()->getName());
  EXPECT_EQ("xlog_test", messages[0].second->getName());
  messages.clear();

  XLOG(DBG2, "this log check should not pass");
  EXPECT_EQ(0, messages.size());
  messages.clear();

  // Test stream arguments to XLOG()
  XLOG(INFO) << "stream test: " << 1 << ", two, " << 3;
  ASSERT_EQ(1, messages.size());
  EXPECT_EQ("stream test: 1, two, 3", messages[0].first.getMessage());
  EXPECT_TRUE(messages[0].first.getFileName().endsWith("XlogTest.cpp"))
      << "unexpected file name: " << messages[0].first.getFileName();
  EXPECT_EQ(LogLevel::INFO, messages[0].first.getLevel());
  EXPECT_EQ("xlog_test.main_file", messages[0].first.getCategory()->getName());
  EXPECT_EQ("xlog_test", messages[0].second->getName());
  messages.clear();
}

TEST(Xlog, perFileCategoryHandling) {
  using namespace logging_test;

  auto handler = make_shared<TestLogHandler>();
  LoggerDB::get()
      ->getCategory("folly.experimental.logging.test")
      ->addHandler(handler);
  LoggerDB::get()->setLevel("folly.experimental.logging.test", LogLevel::DBG9);
  auto& messages = handler->getMessages();

  // Use the simple helper function in XlogHeader2
  testXlogHdrFunction("factor", 99);
  ASSERT_EQ(1, messages.size());
  EXPECT_EQ("test: factor=99", messages[0].first.getMessage());
  EXPECT_TRUE(messages[0].first.getFileName().endsWith("XlogHeader2.h"))
      << "unexpected file name: " << messages[0].first.getFileName();
  EXPECT_EQ(LogLevel::DBG3, messages[0].first.getLevel());
  EXPECT_EQ(
      "folly.experimental.logging.test.XlogHeader2",
      messages[0].first.getCategory()->getName());
  EXPECT_EQ("folly.experimental.logging.test", messages[0].second->getName());
  messages.clear();

  // Test the loop function from XlogHeader1
  testXlogHdrLoop(3, "hello world");
  ASSERT_EQ(5, messages.size());
  EXPECT_EQ("starting: hello world", messages[0].first.getMessage());
  EXPECT_TRUE(messages[0].first.getFileName().endsWith("XlogHeader1.h"))
      << "unexpected file name: " << messages[0].first.getFileName();
  EXPECT_EQ(LogLevel::DBG1, messages[0].first.getLevel());
  EXPECT_EQ(
      "folly.experimental.logging.test.XlogHeader1",
      messages[0].first.getCategory()->getName());
  EXPECT_EQ("folly.experimental.logging.test", messages[0].second->getName());

  EXPECT_EQ("test: hello world", messages[1].first.getMessage());
  EXPECT_EQ("test: hello world", messages[2].first.getMessage());
  EXPECT_EQ("test: hello world", messages[3].first.getMessage());
  EXPECT_EQ("finished: hello world", messages[4].first.getMessage());
  EXPECT_EQ(LogLevel::DBG5, messages[1].first.getLevel());
  EXPECT_EQ(LogLevel::DBG5, messages[2].first.getLevel());
  EXPECT_EQ(LogLevel::DBG5, messages[3].first.getLevel());
  EXPECT_EQ(LogLevel::DBG1, messages[4].first.getLevel());
  messages.clear();

  // Reduce the log level so that the messages inside the loop
  // should not be logged.
  LoggerDB::get()->setLevel("folly.experimental.logging.test", LogLevel::DBG2);
  testXlogHdrLoop(300, "hello world");
  ASSERT_EQ(2, messages.size());
  EXPECT_EQ("starting: hello world", messages[0].first.getMessage());
  EXPECT_EQ("finished: hello world", messages[1].first.getMessage());
  messages.clear();

  // Call the helpers function in XlogFile1.cpp and XlogFile2.cpp and makes
  // sure their categories are reported correctly.
  testXlogFile1Dbg1("foobar 1234");
  ASSERT_EQ(1, messages.size());
  EXPECT_EQ("file1: foobar 1234", messages[0].first.getMessage());
  EXPECT_EQ(
      "folly.experimental.logging.test.XlogFile1",
      messages[0].first.getCategory()->getName());
  messages.clear();

  testXlogFile2Dbg1("hello world");
  ASSERT_EQ(1, messages.size());
  EXPECT_EQ("file2: hello world", messages[0].first.getMessage());
  EXPECT_EQ(
      "folly.experimental.logging.test.XlogFile2",
      messages[0].first.getCategory()->getName());
  messages.clear();

  // Adjust the log level and make sure the changes take effect for the .cpp
  // file categories
  LoggerDB::get()->setLevel("folly.experimental.logging.test", LogLevel::INFO);
  testXlogFile1Dbg1("log check should fail now");
  testXlogFile2Dbg1("this should fail too");
  EXPECT_EQ(0, messages.size());
  messages.clear();

  LoggerDB::get()->setLevel(
      "folly.experimental.logging.test.XlogFile1", LogLevel::DBG1);
  testXlogFile1Dbg1("this log check should pass now");
  testXlogFile2Dbg1("but this one should still fail");
  ASSERT_EQ(1, messages.size());
  EXPECT_EQ(
      "file1: this log check should pass now", messages[0].first.getMessage());
  EXPECT_EQ(
      "folly.experimental.logging.test.XlogFile1",
      messages[0].first.getCategory()->getName());
  messages.clear();
}
