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

#include <folly/io/async/SimpleAsyncIO.h>

#include <array>
#include <atomic>
#include <bitset>
#include <memory>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

#include <folly/File.h>
#include <folly/Random.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/Collect.h>
#include <folly/io/IOBuf.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/EventBaseBackendBase.h>
#include <folly/io/async/ScopedEventBaseThread.h>
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

TEST_P(SimpleAsyncIOTest, FreshInstancesReapTheirFirstCompletion) {
  // Each instance registers its completion fd on, and later unregisters it
  // from, a freshly started event loop thread. Racing the loop from the
  // calling thread used to leave the fd unarmed (the write's completion was
  // never delivered) or corrupt libevent so the loop thread hung on exit.
  auto tmpfile = File::temporary();
  const int fd = tmpfile.fd();
  constexpr int kThreads = 8;
  constexpr int kInstancesPerThread = 250;
  auto config = config_;
  config.setMaxRequests(1);

  std::atomic<int> lost = 0;
  std::atomic<int> setupFailures = 0;
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&] {
      for (int i = 0; i < kInstancesPerThread; ++i) {
        // An exception escaping this thread would abort the whole binary, so
        // count setup failures and fail the test cleanly instead.
        std::unique_ptr<SimpleAsyncIO> aio;
        try {
          aio = std::make_unique<SimpleAsyncIO>(config);
        } catch (const std::exception& ex) {
          LOG(ERROR) << "SimpleAsyncIO setup failed: " << ex.what();
          ++setupFailures;
          continue;
        }
        auto buffer = std::make_unique<std::array<uint8_t, 512>>();
        auto done = std::make_unique<Baton<>>();
        aio->pwrite(
            fd, buffer->data(), buffer->size(), 0, [done = done.get()](int) {
              done->post();
            });
        if (!done->try_wait_for(std::chrono::seconds(10))) {
          // The kernel may still own the write: leak rather than free.
          ++lost;
          (void)aio.release();
          (void)buffer.release();
          (void)done.release();
        }
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  EXPECT_EQ(setupFailures, 0);
  EXPECT_EQ(lost, 0);
}

namespace {
// Delegates to the default backend, but once armed fails event_add for any
// fd it has not registered before, so only a newly registered handler fails.
class FailNewRegistrationsBackend : public EventBaseBackendBase {
 public:
  explicit FailNewRegistrationsBackend(const std::atomic<bool>& armed)
      : inner_(EventBase::getDefaultBackend()), armed_(armed) {}

  event_base* getEventBase() override { return inner_->getEventBase(); }
  int eb_event_base_loop(int flags) override {
    return inner_->eb_event_base_loop(flags);
  }
  int eb_event_base_loopbreak() override {
    return inner_->eb_event_base_loopbreak();
  }
  int eb_event_add(Event& event, const struct timeval* timeout) override {
    const auto fd = event.eb_ev_fd();
    if (armed_.load() && !knownFds_.contains(fd)) {
      errno = EINVAL;
      return -1;
    }
    knownFds_.insert(fd);
    return inner_->eb_event_add(event, timeout);
  }
  int eb_event_del(Event& event) override {
    return inner_->eb_event_del(event);
  }
  bool eb_event_active(Event& event, int res) override {
    return inner_->eb_event_active(event, res);
  }

 private:
  std::unique_ptr<EventBaseBackendBase> inner_;
  const std::atomic<bool>& armed_;
  // Only touched on the loop thread.
  std::set<libevent_fd_t> knownFds_;
};
} // namespace

TEST_P(SimpleAsyncIOTest, ThrowsWhenTheHandlerCannotBeRegistered) {
  std::atomic<bool> armed = false;
  ScopedEventBaseThread evbThread(
      EventBase::Options().setBackendFactory([&armed] {
        return std::make_unique<FailNewRegistrationsBackend>(armed);
      }),
      nullptr,
      "FailingBackend");
  auto config = config_;
  config.setEventBase(evbThread.getEventBase());

  armed = true;
  EXPECT_THROW(SimpleAsyncIO{config}, std::runtime_error);
  armed = false;

  SimpleAsyncIO aio(config);
  EXPECT_TRUE(aio.isHandlerRegistered());
}

TEST_P(SimpleAsyncIOTest, OutlivesProvidedEventBase) {
  // The header asks callers to keep a provided EventBase alive, but existing
  // users (e.g. a thread-local instance bound to an IO executor's EventBase)
  // destroy the EventBase first. Destruction must not touch it then.
  auto tmpfile = File::temporary();
  auto evbThread = std::make_unique<ScopedEventBaseThread>();
  auto config = config_;
  config.setEventBase(evbThread->getEventBase());
  auto aio = std::make_unique<SimpleAsyncIO>(config);

  Baton done;
  const std::string data("Outlived");
  aio->pwrite(tmpfile.fd(), data.data(), data.size(), 0, [&done](int) {
    done.post();
  });
  ASSERT_TRUE(done.try_wait_for(std::chrono::seconds(10)));

  evbThread.reset();
  aio.reset();
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
