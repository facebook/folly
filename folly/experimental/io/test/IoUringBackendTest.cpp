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
      bool valid,
      uint64_t num,
      uint64_t& total,
      bool persist,
      folly::EventBase* eventBase)
      : EventFD(total, valid ? createFd(num) : -1, persist, eventBase) {}
  ~EventFD() override {
    unregisterHandler();

    if (fd_ >= 0) {
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
    ++num_;
    if (total_ > 0) {
      --total_;
      if (total_ == 0) {
        evb_->terminateLoopSoon();
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

void testOverflow(bool overflow, bool persist) {
  static constexpr size_t kBackendCapacity = 64;
  static constexpr size_t kBackendMaxSubmit = 32;
  // for overflow == true  we use a greater than kBackendCapacity number of
  // EventFD instances and lower when overflow == false
  size_t kNumEventFds = overflow ? 2048 : 32;
  static constexpr size_t kEventFdCount = 16;
  auto total = kNumEventFds * kEventFdCount + kEventFdCount / 2;

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
        std::make_unique<EventFD>(true, kEventFdCount, total, persist, &evb));
  }

  evb.loopForever();

  for (size_t i = 0; i < kNumEventFds; i++) {
    CHECK_GE(eventsVec[i]->getNum(), kEventFdCount);
  }
}

void testInvalidFd(size_t numTotal, size_t numValid, size_t numInvalid) {
  static constexpr size_t kBackendCapacity = 128;
  static constexpr size_t kBackendMaxSubmit = 64;

  auto total = numTotal;

  std::unique_ptr<folly::EventBaseBackendBase> backend;

  try {
    backend = std::make_unique<folly::IoUringBackend>(
        kBackendCapacity, kBackendMaxSubmit);
  } catch (const folly::IoUringBackend::NotAvailable&) {
  }

  SKIP_IF(!backend) << "Backend not available";

  folly::EventBase evb(std::move(backend));

  std::vector<std::unique_ptr<EventFD>> eventsVec;
  eventsVec.reserve(numTotal);

  for (size_t i = 0; i < numTotal; i++) {
    bool valid = (i % (numValid + numInvalid)) < numValid;
    eventsVec.emplace_back(
        std::make_unique<EventFD>(valid, 1, total, false /*persist*/, &evb));
  }

  evb.loopForever();

  for (size_t i = 0; i < numTotal; i++) {
    CHECK_GE(eventsVec[i]->getNum(), 1);
  }
}
} // namespace

TEST(IoUringBackend, NoOverflowNoPersist) {
  testOverflow(false, false);
}

TEST(IoUringBackend, OverflowNoPersist) {
  testOverflow(true, false);
}

TEST(IoUringBackend, NoOverflowPersist) {
  testOverflow(false, true);
}

TEST(IoUringBackend, OverflowPersist) {
  testOverflow(true, true);
}

// 9 valid fds followed by an invalid one
TEST(IoUringBackend, Invalid_fd_9_1) {
  testInvalidFd(32, 10, 1);
}

// only invalid fds
TEST(IoUringBackend, Invalid_fd_0_10) {
  testInvalidFd(32, 0, 10);
}

// equal distribution
TEST(IoUringBackend, Invalid_fd_5_5) {
  testInvalidFd(32, 10, 10);
}

TEST(IoUringBackend, RegisteredFds) {
  static constexpr size_t kBackendCapacity = 64;
  static constexpr size_t kBackendMaxSubmit = 32;
  static constexpr size_t kBackendMaxGet = 32;

  std::unique_ptr<folly::IoUringBackend> backendReg;
  std::unique_ptr<folly::IoUringBackend> backendNoReg;

  try {
    backendReg = std::make_unique<folly::IoUringBackend>(
        kBackendCapacity,
        kBackendMaxSubmit,
        kBackendMaxGet,
        true /*useRegisteredFds*/);

    backendNoReg = std::make_unique<folly::IoUringBackend>(
        kBackendCapacity,
        kBackendMaxSubmit,
        kBackendMaxGet,
        false /*useRegisteredFds*/);
  } catch (const folly::IoUringBackend::NotAvailable&) {
  }

  SKIP_IF(!backendReg) << "Backend not available";
  SKIP_IF(!backendNoReg) << "Backend not available";

  int eventFd = ::eventfd(0, EFD_CLOEXEC | EFD_SEMAPHORE | EFD_NONBLOCK);
  CHECK_GT(eventFd, 0);

  SCOPE_EXIT {
    ::close(eventFd);
  };

  // verify for useRegisteredFds = false we get a nullptr FdRegistrationRecord
  auto* record = backendNoReg->registerFd(eventFd);
  CHECK(!record);

  std::vector<folly::IoUringBackend::FdRegistrationRecord*> records;
  // we use kBackendCapacity -1 since we can have the timerFd
  // already using one fd
  records.reserve(kBackendCapacity - 1);
  for (size_t i = 0; i < kBackendCapacity - 1; i++) {
    record = backendReg->registerFd(eventFd);
    CHECK(record);
    records.emplace_back(record);
  }

  // try to allocate one more and check if we get a nullptr
  record = backendReg->registerFd(eventFd);
  CHECK(!record);

  // deallocate and allocate again
  for (size_t i = 0; i < records.size(); i++) {
    CHECK(backendReg->unregisterFd(records[i]));
    record = backendReg->registerFd(eventFd);
    CHECK(record);
    records[i] = record;
  }
}

namespace folly {
namespace test {
static constexpr size_t kCapacity = 16 * 1024;
static constexpr size_t kMaxSubmit = 128;
static constexpr size_t kMaxGet = static_cast<size_t>(-1);

struct IoUringBackendProvider {
  static std::unique_ptr<folly::EventBaseBackendBase> getBackend() {
    try {
      return std::make_unique<folly::IoUringBackend>(
          kCapacity, kMaxSubmit, kMaxGet, false /* useRegisteredFds */);
    } catch (const IoUringBackend::NotAvailable&) {
      return nullptr;
    }
  }
};

struct IoUringRegFdBackendProvider {
  static std::unique_ptr<folly::EventBaseBackendBase> getBackend() {
    try {
      return std::make_unique<folly::IoUringBackend>(
          kCapacity, kMaxSubmit, kMaxGet, true /* useRegisteredFds */);
    } catch (const IoUringBackend::NotAvailable&) {
      return nullptr;
    }
  }
};

// Instantiate the non registered fd tests
INSTANTIATE_TYPED_TEST_CASE_P(IoUring, EventBaseTest, IoUringBackendProvider);
INSTANTIATE_TYPED_TEST_CASE_P(IoUring, EventBaseTest1, IoUringBackendProvider);

// Instantiate the registered fd tests
INSTANTIATE_TYPED_TEST_CASE_P(
    IoUringRegFd,
    EventBaseTest,
    IoUringRegFdBackendProvider);
INSTANTIATE_TYPED_TEST_CASE_P(
    IoUringRegFd,
    EventBaseTest1,
    IoUringRegFdBackendProvider);
} // namespace test
} // namespace folly
