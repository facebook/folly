/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
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

#include <folly/experimental/io/SimpleAsyncIO.h>

#include <bitset>

#include <folly/File.h>
#include <folly/Random.h>
#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Collect.h>
#include <folly/io/IOBuf.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/Baton.h>

#include <glog/logging.h>

using namespace folly;

class SimpleAsyncIOTest : public ::testing::TestWithParam<SimpleAsyncIO::Mode> {
 public:
  void SetUp() override { config_.setMode(GetParam()); }

  static std::string testTypeToString(
      testing::TestParamInfo<SimpleAsyncIO::Mode> const& setting) {
    switch (setting.param) {
      case SimpleAsyncIO::Mode::AIO:
        return "aio";
      case SimpleAsyncIO::Mode::IOURING:
        return "iouring";
    }
  }

 protected:
  SimpleAsyncIO::Config config_;
};

TEST_P(SimpleAsyncIOTest, WriteAndReadBack) {
  auto tmpfile = File::temporary();
  SimpleAsyncIO aio(config_);

  Baton done;
  int result;
  const std::string data("Green Room Rockers");

  aio.pwrite(
      tmpfile.fd(), data.data(), data.size(), 0, [&done, &result](int rc) {
        result = rc;
        done.post();
      });
  ASSERT_TRUE(done.try_wait_for(std::chrono::seconds(10)));
  EXPECT_EQ(result, data.size());

  std::array<uint8_t, 128> buffer;
  done.reset();
  aio.pread(
      tmpfile.fd(), buffer.data(), buffer.size(), 0, [&done, &result](int rc) {
        result = rc;
        done.post();
      });
  ASSERT_TRUE(done.try_wait_for(std::chrono::seconds(10)));
  EXPECT_EQ(result, data.size());
  EXPECT_EQ(memcmp(buffer.data(), data.data(), data.size()), 0);
}

std::string makeRandomBinaryString(size_t size) {
  std::string content;
  content.clear();
  while (content.size() < size) {
    content.append(std::bitset<8>(folly::Random::rand32()).to_string());
  }
  content.resize(size);
  return content;
}

TEST_P(SimpleAsyncIOTest, ChainedReads) {
  auto tmpfile = File::temporary();
  int fd = tmpfile.fd();
  Baton done;

  static const size_t chunkSize = 128;
  static const size_t numChunks = 1000;
  std::vector<std::unique_ptr<IOBuf>> writeChunks;
  std::vector<std::unique_ptr<IOBuf>> readChunks;
  std::atomic<uint32_t> completed = 0;

  for (size_t i = 0; i < numChunks; ++i) {
    writeChunks.push_back(IOBuf::copyBuffer(makeRandomBinaryString(chunkSize)));
    readChunks.push_back(IOBuf::create(chunkSize));
  }

  // allow for one read and one write for each chunk to be outstanding.
  SimpleAsyncIO aio(config_.setMaxRequests(numChunks * 2));
  for (size_t i = 0; i < numChunks; ++i) {
    aio.pwrite(
        fd,
        writeChunks[i]->data(),
        chunkSize,
        i * chunkSize,
        [fd, i, &readChunks, &aio, &done, &completed](int rc) {
          ASSERT_EQ(rc, chunkSize);
          aio.pread(
              fd,
              readChunks[i]->writableData(),
              chunkSize,
              i * chunkSize,
              [=, &done, &completed](int rc) {
                ASSERT_EQ(rc, chunkSize);
                if (++completed == numChunks) {
                  done.post();
                }
              });
        });
  }

  ASSERT_TRUE(done.try_wait_for(std::chrono::seconds(60)));

  for (size_t i = 0; i < numChunks; ++i) {
    CHECK_EQ(
        memcmp(writeChunks[i]->data(), readChunks[i]->data(), chunkSize), 0);
  }
}

TEST_P(SimpleAsyncIOTest, DestroyWithPendingIO) {
  auto tmpfile = File::temporary();
  int fd = tmpfile.fd();
  std::atomic<uint32_t> completed = 0;
  static const size_t bufferSize = 128;
  static const size_t numWrites = 100;
  std::array<uint8_t, bufferSize> buffer;
  memset(buffer.data(), 0, buffer.size());

  // Slam out 100 writes and then destroy the SimpleAsyncIO instance
  // without waiting for them to complete.
  {
    SimpleAsyncIO aio(config_);
    for (size_t i = 0; i < numWrites; ++i) {
      aio.pwrite(
          fd, buffer.data(), bufferSize, i * bufferSize, [&completed](int rc) {
            ASSERT_EQ(rc, bufferSize);
            ++completed;
          });
    }
  }

  // Destructor should have blocked until all IO was done.
  ASSERT_EQ(completed, numWrites);
}

#if FOLLY_HAS_COROUTINES
static folly::coro::Task<folly::Unit> doCoAsyncWrites(
    SimpleAsyncIO& aio, int fd, std::string const& data, int copies) {
  std::vector<folly::coro::Task<int>> writes;

  for (int i = 0; i < copies; ++i) {
    writes.emplace_back(
        aio.co_pwrite(fd, data.data(), data.length(), data.length() * i));
  }

  auto results = co_await folly::coro::collectAllRange(std::move(writes));

  for (int result : results) {
    EXPECT_EQ(result, data.length());
  }
  co_return Unit{};
}

static folly::coro::Task<folly::Unit> doCoAsyncReads(
    SimpleAsyncIO& aio, int fd, std::string const& data, int copies) {
  std::vector<std::unique_ptr<char[]>> buffers;
  std::vector<folly::coro::Task<int>> reads;

  for (int i = 0; i < copies; ++i) {
    buffers.emplace_back(std::make_unique<char[]>(data.length()));

    reads.emplace_back(
        aio.co_pread(fd, buffers[i].get(), data.length(), data.length() * i));
  }

  auto results = co_await folly::coro::collectAllRange(std::move(reads));

  for (int i = 0; i < copies; ++i) {
    EXPECT_EQ(results[i], data.length());
    EXPECT_EQ(::memcmp(data.data(), buffers[i].get(), data.length()), 0);
  }
  co_return Unit{};
}

TEST_P(SimpleAsyncIOTest, CoroutineReadWrite) {
  auto tmpfile = File::temporary();
  int fd = tmpfile.fd();
  SimpleAsyncIO aio(config_);
  std::string testStr = "Uncle Touchy goes to college";
  folly::coro::blockingWait(doCoAsyncWrites(aio, fd, testStr, 10));
  folly::coro::blockingWait(doCoAsyncReads(aio, fd, testStr, 10));
}
#endif // FOLLY_HAS_COROUTINES

INSTANTIATE_TEST_SUITE_P(
    SimpleAsyncIOTests,
    SimpleAsyncIOTest,
    ::testing::Values(
        SimpleAsyncIO::Mode::AIO /*, SimpleAsyncIO::Mode::IOURING */),
    SimpleAsyncIOTest::testTypeToString);
