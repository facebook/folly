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
#include <folly/io/async/AsyncUDPServerSocket.h>
#include <folly/io/async/AsyncUDPSocket.h>
#include <folly/io/async/EventHandler.h>
#include <folly/io/async/test/EventBaseTestLib.h>
#include <folly/portability/GTest.h>

// IoUringBackend specific tests
namespace {
class EventFD : public folly::EventHandler, public folly::EventReadCallback {
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

  void useAsyncReadCallback(bool val) {
    if (val) {
      setEventCallback(this);
    } else {
      resetEventCallback();
    }
  }

  // from folly::EventHandler
  void handlerReady(uint16_t /*events*/) noexcept override {
    // we do not read to leave the fd signalled
    ++num_;
    if (total_ > 0) {
      --total_;
    }

    if (total_ > 0) {
      if (!persist_) {
        registerHandler(folly::EventHandler::READ);
      }
    } else {
      if (persist_) {
        unregisterHandler();
      }
    }
  }

  uint64_t getAsyncNum() const {
    return asyncNum_;
  }

  uint64_t getNum() const {
    return num_;
  }

  // from folly::EventReadCallback
  folly::EventReadCallback::IoVec* allocateData() override {
    auto* ret = ioVecPtr_.release();
    return (ret ? ret : new IoVec(this));
  }

 private:
  struct IoVec : public folly::EventReadCallback::IoVec {
    IoVec() = delete;
    ~IoVec() override = default;
    explicit IoVec(EventFD* eventFd) {
      arg_ = eventFd;
      freeFunc_ = IoVec::free;
      cbFunc_ = IoVec::cb;
      data_.iov_base = &eventData_;
      data_.iov_len = sizeof(eventData_);
    }

    static void free(EventReadCallback::IoVec* ioVec) {
      delete ioVec;
    }

    static void cb(EventReadCallback::IoVec* ioVec, int res) {
      reinterpret_cast<EventFD*>(ioVec->arg_)
          ->cb(reinterpret_cast<IoVec*>(ioVec), res);
    }

    uint64_t eventData_{0};
  };

  static int createFd(uint64_t num) {
    // we want it a semaphore
    // and blocking for the async reads
    int fd = ::eventfd(0, EFD_CLOEXEC | EFD_SEMAPHORE);
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

  void cb(IoVec* ioVec, int res) {
    CHECK_EQ(res, sizeof(IoVec::eventData_));
    CHECK_EQ(ioVec->eventData_, 1);
    // reset it
    ioVec->eventData_ = 0;
    // save it for future use
    ioVecPtr_.reset(ioVec);

    ++asyncNum_;
    if (total_ > 0) {
      --total_;
    }

    if (total_ > 0) {
      if (!persist_) {
        registerHandler(folly::EventHandler::READ);
      }
    } else {
      if (persist_) {
        unregisterHandler();
      }
    }
  }

  uint64_t asyncNum_{0};
  uint64_t num_{0};
  uint64_t& total_;
  int fd_{-1};
  bool persist_;
  folly::EventBase* evb_;
  std::unique_ptr<IoVec> ioVecPtr_;
};

void testEventFD(bool overflow, bool persist, bool asyncRead) {
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
    auto ev = std::make_unique<EventFD>(
        true, 2 * kEventFdCount, total, persist, &evb);

    ev->useAsyncReadCallback(asyncRead);

    eventsVec.emplace_back(std::move(ev));
  }

  evb.loop();

  for (size_t i = 0; i < kNumEventFds; i++) {
    CHECK_GE(
        (asyncRead ? eventsVec[i]->getAsyncNum() : eventsVec[i]->getNum()),
        kEventFdCount);
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

  evb.loop();

  for (size_t i = 0; i < numTotal; i++) {
    CHECK_GE(eventsVec[i]->getNum(), 1);
  }
}

class EventRecvmsgCallback : public folly::EventRecvmsgCallback {
 private:
  struct MsgHdr : public folly::EventRecvmsgCallback::MsgHdr {
    static auto constexpr kBuffSize = 1024;

    MsgHdr() = delete;
    ~MsgHdr() override = default;
    explicit MsgHdr(EventRecvmsgCallback* cb) {
      arg_ = cb;
      freeFunc_ = MsgHdr::free;
      cbFunc_ = MsgHdr::cb;
      ioBuf_ = folly::IOBuf::create(kBuffSize);
    }

    void reset() {
      ::memset(&data_, 0, sizeof(data_));
      iov_.iov_base = ioBuf_->writableData();
      iov_.iov_len = kBuffSize;
      data_.msg_iov = &iov_;
      data_.msg_iovlen = 1;
      ::memset(&addrStorage_, 0, sizeof(addrStorage_));
      data_.msg_name = reinterpret_cast<sockaddr*>(&addrStorage_);
      data_.msg_namelen = sizeof(addrStorage_);
    }

    static void free(folly::EventRecvmsgCallback::MsgHdr* msgHdr) {
      delete msgHdr;
    }

    static void cb(folly::EventRecvmsgCallback::MsgHdr* msgHdr, int res) {
      reinterpret_cast<EventRecvmsgCallback*>(msgHdr->arg_)
          ->cb(reinterpret_cast<MsgHdr*>(msgHdr), res);
    }

    // data
    std::unique_ptr<folly::IOBuf> ioBuf_;
    struct iovec iov_;
    // addr
    struct sockaddr_storage addrStorage_;
  };

  void cb(MsgHdr* msgHdr, int res) {
    // check the number of bytes
    CHECK_EQ(res, static_cast<int>(numBytes_));

    // check the contents
    std::string data;
    data.assign(
        reinterpret_cast<const char*>(msgHdr->ioBuf_->data()),
        static_cast<size_t>(res));
    CHECK_EQ(data, data_);

    // check the address
    folly::SocketAddress addr;
    addr.setFromSockaddr(
        reinterpret_cast<sockaddr*>(msgHdr->data_.msg_name),
        msgHdr->data_.msg_namelen);
    CHECK_EQ(addr, addr_);

    // reuse the msgHdr
    msgHdr_.reset(msgHdr);

    ++asyncNum_;
    if (total_ > 0) {
      --total_;
      if (total_ == 0) {
        evb_->terminateLoopSoon();
      }
    }
  }

 public:
  EventRecvmsgCallback(
      const std::string& data,
      const folly::SocketAddress& addr,
      size_t numBytes,
      uint64_t& total,
      folly::EventBase* eventBase)
      : data_(data),
        addr_(addr),
        numBytes_(numBytes),
        total_(total),
        evb_(eventBase) {}
  ~EventRecvmsgCallback() override = default;

  // from EventRecvmsgCallback
  EventRecvmsgCallback::MsgHdr* allocateData() override {
    auto* ret = msgHdr_.release();
    if (!ret) {
      ret = new MsgHdr(this);
    }

    ret->reset();

    return ret;
  }

  uint64_t getAsyncNum() const {
    return asyncNum_;
  }

 private:
  const std::string& data_;
  folly::SocketAddress addr_;
  size_t numBytes_{0};
  uint64_t& total_;
  folly::EventBase* evb_;
  uint64_t asyncNum_{0};
  std::unique_ptr<MsgHdr> msgHdr_;
};

void testAsyncUDPRecvmsg(bool useRegisteredFds) {
  static constexpr size_t kBackendCapacity = 64;
  static constexpr size_t kBackendMaxSubmit = 32;
  static constexpr size_t kBackendMaxGet = 32;
  static constexpr size_t kNumSockets = 32;
  static constexpr size_t kNumBytes = 16;
  static constexpr size_t kNumPackets = 32;
  auto total = kNumPackets * kNumSockets;

  std::unique_ptr<folly::EventBaseBackendBase> backend;

  try {
    backend = std::make_unique<folly::IoUringBackend>(
        kBackendCapacity, kBackendMaxSubmit, kBackendMaxGet, useRegisteredFds);
  } catch (const folly::IoUringBackend::NotAvailable&) {
  }

  SKIP_IF(!backend) << "Backend not available";

  folly::EventBase evb(std::move(backend));

  // create the server sockets
  std::vector<std::unique_ptr<folly::AsyncUDPServerSocket>> serverSocketVec;
  serverSocketVec.reserve(kNumSockets);

  std::vector<std::unique_ptr<folly::AsyncUDPSocket>> clientSocketVec;
  serverSocketVec.reserve(kNumSockets);

  std::vector<std::unique_ptr<EventRecvmsgCallback>> cbVec;
  cbVec.reserve(kNumSockets);

  std::string data(kNumBytes, 'A');

  for (size_t i = 0; i < kNumSockets; i++) {
    auto clientSock = std::make_unique<folly::AsyncUDPSocket>(&evb);
    clientSock->bind(folly::SocketAddress("::1", 0));

    auto cb = std::make_unique<EventRecvmsgCallback>(
        data, clientSock->address(), kNumBytes, total, &evb);
    auto serverSock = std::make_unique<folly::AsyncUDPServerSocket>(
        &evb, 1500, folly::AsyncUDPServerSocket::DispatchMechanism::RoundRobin);
    // set the event callback
    serverSock->setEventCallback(cb.get());
    // bind
    serverSock->bind(folly::SocketAddress("::1", 0));
    // retrieve the real address
    folly::SocketAddress addr = serverSock->address();

    serverSock->listen();

    serverSocketVec.emplace_back(std::move(serverSock));

    // connect the client
    CHECK_EQ(clientSock->connect(addr), 0);
    for (size_t j = 0; j < kNumPackets; j++) {
      auto buf = folly::IOBuf::copyBuffer(data.c_str(), data.size());
      CHECK_EQ(clientSock->write(addr, std::move(buf)), data.size());
    }

    clientSocketVec.emplace_back(std::move(clientSock));

    cbVec.emplace_back(std::move(cb));
  }

  evb.loopForever();

  for (size_t i = 0; i < kNumSockets; i++) {
    CHECK_GE(cbVec[i]->getAsyncNum(), kNumPackets);
  }
}
} // namespace

TEST(IoUringBackend, AsyncUDPRecvmsgNoRegisterFd) {
  testAsyncUDPRecvmsg(false);
}

TEST(IoUringBackend, AsyncUDPRecvmsgRegisterFd) {
  testAsyncUDPRecvmsg(true);
}

TEST(IoUringBackend, EventFD_NoOverflowNoPersist) {
  testEventFD(false, false, false);
}

TEST(IoUringBackend, EventFD_OverflowNoPersist) {
  testEventFD(true, false, false);
}

TEST(IoUringBackend, EventFD_NoOverflowPersist) {
  testEventFD(false, true, false);
}

TEST(IoUringBackend, EventFD_OverflowPersist) {
  testEventFD(true, true, false);
}

TEST(IoUringBackend, EventFD_Persist_AsyncRead) {
  testEventFD(false, true, true);
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
