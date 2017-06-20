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
#include <folly/Conv.h>
#include <folly/Exception.h>
#include <folly/File.h>
#include <folly/FileUtil.h>
#include <folly/String.h>
#include <folly/experimental/TestUtil.h>
#include <folly/experimental/logging/AsyncFileWriter.h>
#include <folly/experimental/logging/LoggerDB.h>
#include <folly/portability/GFlags.h>
#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>
#include <folly/portability/Unistd.h>

DEFINE_int64(
    async_discard_num_writer_threads,
    32,
    "number of threads to use to generate log messages during "
    "the AsyncFileWriter.discard test");
DEFINE_int64(
    async_discard_messages_per_writer,
    200000,
    "number of messages each writer threads should generate in "
    "the AsyncFileWriter.discard test");
DEFINE_int64(
    async_discard_read_sleep_usec,
    500,
    "how long the read thread should sleep between reads in "
    "the AsyncFileWriter.discard test");

using namespace folly;
using namespace std::literals::chrono_literals;
using folly::test::TemporaryFile;

TEST(AsyncFileWriter, noMessages) {
  TemporaryFile tmpFile{"logging_test"};

  // Test the simple construction and destruction of an AsyncFileWriter
  // without ever writing any messages.  This still exercises the I/O
  // thread start-up and shutdown code.
  AsyncFileWriter writer{folly::File{tmpFile.fd(), false}};
}

TEST(AsyncFileWriter, simpleMessages) {
  TemporaryFile tmpFile{"logging_test"};

  {
    AsyncFileWriter writer{folly::File{tmpFile.fd(), false}};
    for (int n = 0; n < 10; ++n) {
      writer.writeMessage(folly::to<std::string>("message ", n, "\n"));
      sched_yield();
    }
  }

  std::string data;
  auto ret = folly::readFile(tmpFile.path().native().c_str(), data);
  ASSERT_TRUE(ret);

  std::string expected =
      "message 0\n"
      "message 1\n"
      "message 2\n"
      "message 3\n"
      "message 4\n"
      "message 5\n"
      "message 6\n"
      "message 7\n"
      "message 8\n"
      "message 9\n";
  EXPECT_EQ(expected, data);
}

#ifndef _WIN32
namespace {
static std::vector<std::string>* internalWarnings;

void handleLoggingError(
    StringPiece /* file */,
    int /* lineNumber */,
    std::string&& msg) {
  internalWarnings->emplace_back(std::move(msg));
}
}

TEST(AsyncFileWriter, ioError) {
  // Set the LoggerDB internal warning handler so we can record the messages
  std::vector<std::string> logErrors;
  internalWarnings = &logErrors;
  LoggerDB::setInternalWarningHandler(handleLoggingError);

  // Create an AsyncFileWriter that refers to a pipe whose read end is closed
  std::array<int, 2> fds;
  auto rc = pipe(fds.data());
  folly::checkUnixError(rc, "failed to create pipe");
  signal(SIGPIPE, SIG_IGN);
  ::close(fds[0]);

  // Log a bunch of messages to the writer
  size_t numMessages = 100;
  {
    AsyncFileWriter writer{folly::File{fds[1], true}};
    for (size_t n = 0; n < numMessages; ++n) {
      writer.writeMessage(folly::to<std::string>("message ", n, "\n"));
      sched_yield();
    }
  }

  LoggerDB::setInternalWarningHandler(nullptr);

  // AsyncFileWriter should have some internal warning messages about the
  // log failures.  This will generally be many fewer than the number of
  // messages we wrote, though, since it performs write batching.
  for (const auto& msg : logErrors) {
    EXPECT_THAT(
        msg,
        testing::ContainsRegex(
            "error writing to log file .* in AsyncFileWriter.*: Broken pipe"));
  }
  EXPECT_GT(logErrors.size(), 0);
  EXPECT_LE(logErrors.size(), numMessages);
}
#endif

/**
 * writeThread() writes a series of messages to the AsyncFileWriter
 */
void writeThread(
    AsyncFileWriter* writer,
    size_t id,
    size_t numMessages,
    uint32_t flags) {
  for (size_t n = 0; n < numMessages; ++n) {
    writer->writeMessage(
        folly::to<std::string>("thread ", id, " message ", n + 1, '\n'), flags);
  }
}

class ReadStats {
 public:
  ReadStats()
      : readSleepUS_{static_cast<uint64_t>(
            std::min(0L, FLAGS_async_discard_read_sleep_usec))} {}

  void clearSleepDuration() {
    readSleepUS_.store(0);
  }
  std::chrono::microseconds getSleepUS() const {
    return std::chrono::microseconds{readSleepUS_.load()};
  }

  void check(size_t numThreads, size_t messagesPerThread) {
    EXPECT_EQ("", trailingData_);
    EXPECT_EQ(numThreads, writers_.size());
    size_t totalMessagesReceived = 0;
    for (const auto& writerData : writers_) {
      EXPECT_LE(writerData.numMessages, messagesPerThread);
      EXPECT_LE(writerData.lastId, messagesPerThread);
      totalMessagesReceived += writerData.numMessages;
    }

    EXPECT_EQ(0, numUnableToParse_);
    EXPECT_EQ(0, numOutOfOrder_);
    EXPECT_EQ(
        numThreads * messagesPerThread, totalMessagesReceived + numDiscarded_);
  }

  /**
   * Check that no messages were dropped from the specified thread.
   */
  void checkNoDrops(size_t threadIndex, size_t messagesPerThread) {
    EXPECT_EQ(writers_[threadIndex].numMessages, messagesPerThread);
    EXPECT_EQ(writers_[threadIndex].lastId, messagesPerThread);
  }

  void messageReceived(StringPiece msg) {
    if (msg.endsWith(" log messages discarded: "
                     "logging faster than we can write")) {
      auto discardCount = folly::to<size_t>(msg.subpiece(0, msg.find(' ')));
      fprintf(stderr, "received discard notification: %zu\n", discardCount);
      numDiscarded_ += discardCount;
      return;
    }

    size_t threadID = 0;
    size_t messageIndex = 0;
    try {
      parseMessage(msg, &threadID, &messageIndex);
    } catch (const std::exception& ex) {
      ++numUnableToParse_;
      fprintf(
          stderr,
          "unable to parse log message: %s\n",
          folly::humanify(msg.str()).c_str());
      return;
    }

    if (threadID >= writers_.size()) {
      writers_.resize(threadID + 1);
    }
    writers_[threadID].numMessages++;
    if (messageIndex > writers_[threadID].lastId) {
      writers_[threadID].lastId = messageIndex;
    } else {
      ++numOutOfOrder_;
      fprintf(
          stderr,
          "received out-of-order messages from writer %zu: "
          "%zu received after %zu\n",
          threadID,
          messageIndex,
          writers_[threadID].lastId);
    }
  }

  void trailingData(StringPiece data) {
    trailingData_ = data.str();
  }

 private:
  struct WriterStats {
    size_t numMessages{0};
    size_t lastId{0};
  };

  void parseMessage(StringPiece msg, size_t* threadID, size_t* messageIndex) {
    constexpr StringPiece prefix{"thread "};
    constexpr StringPiece middle{" message "};
    if (!msg.startsWith(prefix)) {
      throw std::runtime_error("bad message prefix");
    }

    auto idx = prefix.size();
    auto end = msg.find(' ', idx);
    if (end == StringPiece::npos) {
      throw std::runtime_error("no middle found");
    }

    *threadID = folly::to<size_t>(msg.subpiece(idx, end - idx));
    auto rest = msg.subpiece(end);
    if (!rest.startsWith(middle)) {
      throw std::runtime_error("bad message middle");
    }

    rest.advance(middle.size());
    *messageIndex = folly::to<size_t>(rest);
  }

  std::vector<WriterStats> writers_;
  std::string trailingData_;
  size_t numUnableToParse_{0};
  size_t numOutOfOrder_{0};
  size_t numDiscarded_{0};

  std::atomic<uint64_t> readSleepUS_;
};

/**
 * readThread() reads messages slowly from a pipe.  This helps test the
 * AsyncFileWriter behavior when I/O is slow.
 */
void readThread(folly::File&& file, ReadStats* stats) {
  std::vector<char> buffer;
  buffer.resize(1024);

  size_t bufferIdx = 0;
  while (true) {
    /* sleep override */
    usleep(stats->getSleepUS().count());

    auto readResult = folly::readNoInt(
        file.fd(), buffer.data() + bufferIdx, buffer.size() - bufferIdx);
    if (readResult < 0) {
      fprintf(stderr, "error reading from pipe: %d\n", errno);
      return;
    }
    if (readResult == 0) {
      fprintf(stderr, "read EOF\n");
      break;
    }

    auto logDataLen = bufferIdx + readResult;
    StringPiece logData{buffer.data(), logDataLen};
    auto idx = 0;
    while (true) {
      auto end = logData.find('\n', idx);
      if (end == StringPiece::npos) {
        bufferIdx = logDataLen - idx;
        memmove(buffer.data(), buffer.data() + idx, bufferIdx);
        break;
      }

      StringPiece logMsg{logData.data() + idx, end - idx};
      stats->messageReceived(logMsg);
      idx = end + 1;
    }
  }

  if (bufferIdx != 0) {
    stats->trailingData(StringPiece{buffer.data(), bufferIdx});
  }
}

/*
 * The discard test spawns a number of threads that each write a large number
 * of messages quickly.  The AsyncFileWriter writes to a pipe, an a separate
 * thread reads from it slowly, causing a backlog to build up.
 *
 * The test then checks that:
 * - The read thread always receives full messages (no partial log messages)
 * - Messages that are received are received in order
 * - The number of messages received plus the number reported in discard
 *   notifications matches the number of messages sent.
 */
TEST(AsyncFileWriter, discard) {
  std::array<int, 2> fds;
  auto pipeResult = pipe(fds.data());
  folly::checkUnixError(pipeResult, "pipe failed");
  folly::File readPipe{fds[0], true};
  folly::File writePipe{fds[1], true};

  // This test should always be run with at least 2 writer threads.
  // The last one will use the NEVER_DISCARD flag, and we want at least
  // one that does discard messages.
  ASSERT_GT(FLAGS_async_discard_num_writer_threads, 2);

  ReadStats readStats;
  std::thread reader(readThread, std::move(readPipe), &readStats);
  {
    AsyncFileWriter writer{std::move(writePipe)};

    std::vector<std::thread> writeThreads;
    for (int n = 0; n < FLAGS_async_discard_num_writer_threads; ++n) {
      uint32_t flags = 0;
      // Configure the last writer thread to never drop messages
      if (n == (FLAGS_async_discard_num_writer_threads - 1)) {
        flags = LogWriter::NEVER_DISCARD;
      }

      writeThreads.emplace_back(
          writeThread,
          &writer,
          n,
          FLAGS_async_discard_messages_per_writer,
          flags);
    }

    for (auto& t : writeThreads) {
      t.join();
    }
    fprintf(stderr, "writers done\n");
  }
  // Clear the read sleep duration so the reader will finish quickly now
  readStats.clearSleepDuration();
  reader.join();
  readStats.check(
      FLAGS_async_discard_num_writer_threads,
      FLAGS_async_discard_messages_per_writer);
  // Check that no messages were dropped from the thread using the
  // NEVER_DISCARD flag.
  readStats.checkNoDrops(
      FLAGS_async_discard_num_writer_threads - 1,
      FLAGS_async_discard_messages_per_writer);
}
