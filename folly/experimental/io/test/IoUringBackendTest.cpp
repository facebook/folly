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

#include <sys/eventfd.h>

#include <folly/FileUtil.h>
#include <folly/experimental/io/IoUringBackend.h>
#include <folly/init/Init.h>
#include <folly/io/async/EventHandler.h>
#include <folly/io/async/test/EventBaseTestLib.h>
#include <folly/portability/GTest.h>

// IoUringBackend specific tests
namespace {
class EventFD : public folly::EventHandler {
 public:
  EventFD(
      uint64_t num,
      uint64_t& total,
      bool persist,
      folly::EventBase* eventBase)
      : EventFD(total, createFd(num), persist, eventBase) {}
  ~EventFD() override {
    unregisterHandler();

    if (fd_ > 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

  // from folly::EventHandler
  void handlerReady(uint16_t /*events*/) noexcept override {
    // we do not read to leave the fd signalled
    if (!persist_) {
      registerHandler(folly::EventHandler::READ);
    }
    size_t data;
    if (sizeof(data) == folly::readNoInt(fd_, &data, sizeof(data))) {
      CHECK_EQ(data, 1);
      ++num_;
      if (total_ > 0) {
        --total_;
        if (total_ == 0) {
          evb_->terminateLoopSoon();
        }
      }
    }
  }

  uint64_t getNum() const {
    return num_;
  }

 private:
  static int createFd(uint64_t num) {
    // we want it a semaphore
    int fd = ::eventfd(0, EFD_CLOEXEC | EFD_SEMAPHORE | EFD_NONBLOCK);
    CHECK_GT(fd, 0);
    CHECK_EQ(folly::writeNoInt(fd, &num, sizeof(num)), sizeof(num));
    return fd;
  }

  EventFD(uint64_t& total, int fd, bool persist, folly::EventBase* eventBase)
      : EventHandler(eventBase, folly::NetworkSocket::fromFd(fd)),
        total_(total),
        fd_(fd),
        persist_(persist),
        evb_(eventBase) {
    if (persist_) {
      registerHandler(folly::EventHandler::READ | folly::EventHandler::PERSIST);
    } else {
      registerHandler(folly::EventHandler::READ);
    }
  }
  uint64_t num_{0};
  uint64_t& total_;
  int fd_{-1};
  bool persist_;
  folly::EventBase* evb_;
};

void testOverflow(bool overflow) {
  static constexpr size_t kBackendCapacity = 64;
  static constexpr size_t kBackendMaxSubmit = 32;
  // for overflow == true  we use a greater than kBackendCapacity number of
  // EventFD instances and lower when overflow == false
  size_t kNumEventFds = overflow ? 2048 : 32;
  static constexpr size_t kEventFdCount = 16;
  auto total = kNumEventFds * kEventFdCount;

  std::unique_ptr<folly::EventBaseBackendBase> backend;

  try {
    backend = std::make_unique<folly::IoUringBackend>(
        kBackendCapacity, kBackendMaxSubmit);
  } catch (const folly::IoUringBackend::NotAvailable&) {
  }

  SKIP_IF(!backend) << "Backend not available";

  folly::EventBase evb(std::move(backend));

  std::vector<std::unique_ptr<EventFD>> eventsVec;
  eventsVec.reserve(kNumEventFds);
  for (size_t i = 0; i < kNumEventFds; i++) {
    eventsVec.emplace_back(
        std::make_unique<EventFD>(kEventFdCount, total, true, &evb));
  }

  evb.loopForever();

  for (size_t i = 0; i < kNumEventFds; i++) {
    CHECK_EQ(eventsVec[i]->getNum(), kEventFdCount);
  }
}
} // namespace

TEST(IoUringBackend, NoOverflow) {
  testOverflow(false);
}

TEST(IoUringBackend, Overflow) {
  testOverflow(true);
}

namespace folly {
namespace test {
static constexpr size_t kCapacity = 16 * 1024;
static constexpr size_t kMaxSubmit = 128;
struct IoUringBackendProvider {
  static std::unique_ptr<folly::EventBaseBackendBase> getBackend() {
    try {
      return std::make_unique<folly::IoUringBackend>(kCapacity, kMaxSubmit);
    } catch (const IoUringBackend::NotAvailable&) {
      return nullptr;
    }
  }
};

INSTANTIATE_TYPED_TEST_CASE_P(
    EventBaseTest,
    EventBaseTest,
    IoUringBackendProvider);
INSTANTIATE_TYPED_TEST_CASE_P(
    EventBaseTest1,
    EventBaseTest1,
    IoUringBackendProvider);
} // namespace test
} // namespace folly
