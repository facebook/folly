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

#include <folly/test/TestUtils.h>
#include <queue>

#include <folly/logging/Init.h>
#include <folly/logging/LogConfigParser.h>
#include <folly/logging/LogHandlerFactory.h>
#include <folly/logging/LogWriter.h>
#include <folly/logging/LoggerDB.h>
#include <folly/logging/StandardLogHandler.h>
#include <folly/logging/StandardLogHandlerFactory.h>
#include <folly/logging/xlog.h>

namespace folly {

class TestLogWriter : public LogWriter {
 public:
  void writeMessage(folly::StringPiece /* buffer */, uint32_t /* flags */ = 0)
      override {
    message_count++;
  }

  void flush() override {}

  int message_count{0};

  bool ttyOutput() const override {
    return false;
  }
};

class TestHandlerFactory : public LogHandlerFactory {
 public:
  explicit TestHandlerFactory(const std::shared_ptr<TestLogWriter> writer)
      : writer_(writer) {}

  StringPiece getType() const override {
    return "test";
  }

  std::shared_ptr<LogHandler> createHandler(const Options& options) override {
    TestWriterFactory writerFactory{writer_};
    return StandardLogHandlerFactory::createHandler(
        getType(), &writerFactory, options);
  }

 private:
  std::shared_ptr<TestLogWriter> writer_;
  class TestWriterFactory : public StandardLogHandlerFactory::WriterFactory {
   public:
    explicit TestWriterFactory(std::shared_ptr<TestLogWriter> writer)
        : writer_(writer) {}

    bool processOption(StringPiece /* name */, StringPiece /* value */)
        override {
      return false;
    }

    std::shared_ptr<LogWriter> createWriter() override {
      return writer_;
    }

   private:
    std::shared_ptr<TestLogWriter> writer_;
  };
};

} // namespace folly

using namespace folly;

namespace {
class StandardLogHandlerFactoryTest : public testing::Test {
 public:
  StandardLogHandlerFactoryTest() {
    writer = std::make_shared<TestLogWriter>();
    db.registerHandlerFactory(
        std::make_unique<TestHandlerFactory>(writer), true);
  }

  LoggerDB db{LoggerDB::TESTING};
  std::shared_ptr<TestLogWriter> writer;
};
} // namespace

TEST_F(StandardLogHandlerFactoryTest, LogLevelTest) {
  Logger logger{&db, "test"};
  db.resetConfig(parseLogConfig("test=DBG4:default; default=test:level=WARN"));

  FB_LOG(logger, DBG9) << "DBG9";
  FB_LOG(logger, DBG3) << "DBG3";
  FB_LOG(logger, WARN) << "WARN 1";
  FB_LOG(logger, WARN) << "WARN 2";

  EXPECT_EQ(writer->message_count, 2);
}

TEST_F(StandardLogHandlerFactoryTest, LogLevelReverseTest) {
  Logger logger{&db, "test"};
  // log category level is WARN, log handler level is DBG3
  db.resetConfig(parseLogConfig("test=WARN:default; default=test:level=DBG3"));

  FB_LOG(logger, DBG9) << "DBG9";
  FB_LOG(logger, DBG3) << "DBG3";
  FB_LOG(logger, DBG2) << "DBG2";
  FB_LOG(logger, WARN) << "WARN 1";

  EXPECT_EQ(writer->message_count, 1);
}

TEST_F(StandardLogHandlerFactoryTest, MultipleLoggerTest) {
  Logger logger{&db, "test"};

  // log category level is DBG4
  // log handler "default" level is DBG3
  // log handler "other" level is WARN
  db.resetConfig(
      parseLogConfig("test=DBG4:default:other;"
                     "default=test:level=DBG3;"
                     "other=test:level=WARN"));

  FB_LOG(logger, DBG9) << "DBG9"; // no handler should log this
  FB_LOG(logger, DBG3) << "DBG3"; // only "default" logs this
  FB_LOG(logger, WARN) << "WARN 1"; // both log handlers log this

  EXPECT_EQ(writer->message_count, 3);
}
