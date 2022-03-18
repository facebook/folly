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

#include <sys/eventfd.h>
#include <numeric>

#include <folly/FileUtil.h>
#include <folly/String.h>
#include <folly/experimental/io/IoUringBackend.h>
#include <folly/experimental/io/test/IoTestTempFileUtil.h>
#include <folly/init/Init.h>
#include <folly/io/async/AsyncUDPServerSocket.h>
#include <folly/io/async/AsyncUDPSocket.h>
#include <folly/io/async/EventHandler.h>
#include <folly/io/async/test/EventBaseTestLib.h>
#include <folly/portability/GTest.h>

#ifndef RESOLVE_IN_ROOT
#define RESOLVE_IN_ROOT 0x10
#endif

// IoUringBackend specific tests
namespace {
class AlignedBuf {
 public:
  static constexpr size_t kAlign = 4096;
  AlignedBuf() = delete;

  AlignedBuf(size_t count, char ch) : size_(count) {
    ::posix_memalign(&data_, kAlign, size_);
    CHECK(!!data_);
    ::memset(data_, ch, count);
  }

  AlignedBuf(const AlignedBuf& buf) : size_(buf.size_) {
    if (size_) {
      ::posix_memalign(&data_, kAlign, size_);
      CHECK(!!data_);
      ::memcpy(data_, buf.data_, size_);
    }
  }

  ~AlignedBuf() {
    if (data_) {
      ::free(data_);
    }
  }

  AlignedBuf& operator=(const AlignedBuf& buf) {
    if (data_) {
      ::free(data_);
    }

    size_ = buf.size_;
    if (size_) {
      ::posix_memalign(&data_, kAlign, size_);
      CHECK(!!data_);
      ::memcpy(data_, buf.data_, size_);
    }

    return *this;
  }

  bool operator==(const AlignedBuf& buf) const {
    if (size_ != buf.size_) {
      return false;
    }

    if (size_ == 0) {
      return true;
    }

    return (0 == ::memcmp(data_, buf.data_, size_));
  }

  bool operator!=(const AlignedBuf& buf) const { return !(*this == buf); }

  void* data() const { return data_; }

  size_t size() const { return size_; }

 private:
  void* data_{nullptr};
  size_t size_{0};
};
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

  uint64_t getAsyncNum() const { return asyncNum_; }

  uint64_t getNum() const { return num_; }

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

    static void free(EventReadCallback::IoVec* ioVec) { delete ioVec; }

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

std::unique_ptr<folly::EventBase> getEventBase(
    folly::PollIoBackend::Options opts) {
  try {
    auto factory = [opts] {
      return std::make_unique<folly::IoUringBackend>(opts);
    };
    return std::make_unique<folly::EventBase>(
        folly::EventBase::Options().setBackendFactory(std::move(factory)));
  } catch (const folly::IoUringBackend::NotAvailable&) {
    return nullptr;
  }
}

std::unique_ptr<folly::EventBase> getEventBase() {
  static constexpr size_t kBackendCapacity = 32;
  static constexpr size_t kBackendMaxSubmit = 16;
  static constexpr size_t kBackendMaxGet = 8;
  folly::PollIoBackend::Options options;
  options.setCapacity(kBackendCapacity)
      .setMaxSubmit(kBackendMaxSubmit)
      .setMaxGet(kBackendMaxGet)
      .setUseRegisteredFds(false);
  return getEventBase(options);
}

void testEventFD(bool overflow, bool persist, bool asyncRead) {
  static constexpr size_t kBackendCapacity = 16;
  static constexpr size_t kBackendMaxSubmit = 8;
  // for overflow == true  we use a greater than kBackendCapacity number of
  // EventFD instances and lower when overflow == false
  size_t kNumEventFds = overflow ? 2048 : 32;
  static constexpr size_t kEventFdCount = 16;
  auto total = kNumEventFds * kEventFdCount + kEventFdCount / 2;

  folly::PollIoBackend::Options options;
  options.setCapacity(kBackendCapacity).setMaxSubmit(kBackendMaxSubmit);
  auto evbPtr = getEventBase(options);
  SKIP_IF(!evbPtr) << "Backend not available";

  std::vector<std::unique_ptr<EventFD>> eventsVec;
  eventsVec.reserve(kNumEventFds);
  for (size_t i = 0; i < kNumEventFds; i++) {
    auto ev = std::make_unique<EventFD>(
        true, 2 * kEventFdCount, total, persist, evbPtr.get());

    ev->useAsyncReadCallback(asyncRead);

    eventsVec.emplace_back(std::move(ev));
  }

  evbPtr->loop();

  for (size_t i = 0; i < kNumEventFds; i++) {
    CHECK_GE(
        (asyncRead ? eventsVec[i]->getAsyncNum() : eventsVec[i]->getNum()),
        kEventFdCount);
  }
}

void testInvalidFd(size_t numTotal, size_t numValid, size_t numInvalid) {
  static constexpr size_t kBackendCapacity = 32;
  static constexpr size_t kBackendMaxSubmit = 16;

  auto total = numTotal;

  folly::PollIoBackend::Options options;
  options.setCapacity(kBackendCapacity).setMaxSubmit(kBackendMaxSubmit);
  auto evbPtr = getEventBase(options);
  SKIP_IF(!evbPtr) << "Backend not available";

  std::vector<std::unique_ptr<EventFD>> eventsVec;
  eventsVec.reserve(numTotal);

  for (size_t i = 0; i < numTotal; i++) {
    bool valid = (i % (numValid + numInvalid)) < numValid;
    eventsVec.emplace_back(std::make_unique<EventFD>(
        valid, 1, total, false /*persist*/, evbPtr.get()));
  }

  evbPtr->loop();

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

  uint64_t getAsyncNum() const { return asyncNum_; }

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
  static constexpr size_t kBackendCapacity = 16;
  static constexpr size_t kBackendMaxSubmit = 8;
  static constexpr size_t kBackendMaxGet = 8;
  static constexpr size_t kNumSockets = 8;
  static constexpr size_t kNumBytes = 16;
  static constexpr size_t kNumPackets = 32;
  auto total = kNumPackets * kNumSockets;

  folly::PollIoBackend::Options options;
  options.setCapacity(kBackendCapacity)
      .setMaxSubmit(kBackendMaxSubmit)
      .setMaxGet(kBackendMaxGet)
      .setUseRegisteredFds(useRegisteredFds);
  auto evbPtr = getEventBase(options);
  SKIP_IF(!evbPtr) << "Backend not available";

  // create the server sockets
  std::vector<std::unique_ptr<folly::AsyncUDPServerSocket>> serverSocketVec;
  serverSocketVec.reserve(kNumSockets);

  std::vector<std::unique_ptr<folly::AsyncUDPSocket>> clientSocketVec;
  clientSocketVec.reserve(kNumSockets);

  std::vector<std::unique_ptr<EventRecvmsgCallback>> cbVec;
  cbVec.reserve(kNumSockets);

  std::string data(kNumBytes, 'A');

  for (size_t i = 0; i < kNumSockets; i++) {
    auto clientSock = std::make_unique<folly::AsyncUDPSocket>(evbPtr.get());
    clientSock->bind(folly::SocketAddress("::1", 0));

    auto cb = std::make_unique<EventRecvmsgCallback>(
        data, clientSock->address(), kNumBytes, total, evbPtr.get());
    auto serverSock = std::make_unique<folly::AsyncUDPServerSocket>(
        evbPtr.get(),
        1500,
        folly::AsyncUDPServerSocket::DispatchMechanism::RoundRobin);
    // set the event callback
    serverSock->setEventCallback(cb.get());
    // bind
    serverSock->bind(folly::SocketAddress("::1", 0));
    // retrieve the real address
    folly::SocketAddress addr = serverSock->address();

    serverSock->listen();

    serverSocketVec.emplace_back(std::move(serverSock));

    // connect the client
    clientSock->connect(addr);
    for (size_t j = 0; j < kNumPackets; j++) {
      auto buf = folly::IOBuf::copyBuffer(data.c_str(), data.size());
      CHECK_EQ(clientSock->write(addr, std::move(buf)), data.size());
    }

    clientSocketVec.emplace_back(std::move(clientSock));

    cbVec.emplace_back(std::move(cb));
  }

  evbPtr->loopForever();

  for (size_t i = 0; i < kNumSockets; i++) {
    CHECK_GE(cbVec[i]->getAsyncNum(), kNumPackets);
  }
}
} // namespace

TEST(IoUringBackend, FailCreateNoRetry) {
  bool bSuccess = true;
  try {
    folly::IoUringBackend::Options options;
    options.setCapacity(256 * 1024);
    options.setMinCapacity(0);
    folly::IoUringBackend backend(options);
  } catch (const folly::IoUringBackend::NotAvailable&) {
    bSuccess = false;
  }
  CHECK(!bSuccess);
}

TEST(IoUringBackend, SuccessCreateRetry) {
  bool bSuccess = true;
  try {
    folly::IoUringBackend::Options options;
    options.setCapacity(256 * 1024);
    options.setMinCapacity(16);
    options.setMaxSubmit(8);
    folly::IoUringBackend backend(options);
  } catch (const folly::IoUringBackend::NotAvailable&) {
    bSuccess = false;
  }
  CHECK(bSuccess);
}

TEST(IoUringBackend, OpenAt) {
  auto evbPtr = getEventBase();
  SKIP_IF(!evbPtr) << "Backend not available";

  auto* backendPtr = dynamic_cast<folly::IoUringBackend*>(evbPtr->getBackend());
  CHECK(!!backendPtr);

  auto dirPath = folly::fs::temp_directory_path();
  auto path = folly::fs::unique_path();
  auto filePath = dirPath / path;

  int dfd = ::open(dirPath.string().c_str(), O_DIRECTORY | O_RDONLY, 0666);
  CHECK_GE(dfd, 0);

  SCOPE_EXIT {
    ::close(dfd);
    ::unlink(filePath.string().c_str());
  };

  folly::IoUringBackend::FileOpCallback openCb = [&](int res) {
    evbPtr->terminateLoopSoon();
    CHECK_GE(res, 0);
    CHECK_EQ(0, ::close(res));
  };

  backendPtr->queueOpenat(
      dfd,
      path.string().c_str(),
      O_RDWR | O_CREAT | O_EXCL,
      0666,
      std::move(openCb));

  evbPtr->loopForever();
}

TEST(IoUringBackend, OpenAt2) {
  auto evbPtr = getEventBase();
  SKIP_IF(!evbPtr) << "Backend not available";

  auto* backendPtr = dynamic_cast<folly::IoUringBackend*>(evbPtr->getBackend());
  CHECK(!!backendPtr);

  auto dirPath = folly::fs::temp_directory_path();
  auto path = folly::fs::unique_path();
  auto filePath = dirPath / path;

  int dfd = ::open(dirPath.string().c_str(), O_DIRECTORY | O_RDONLY, 0666);
  CHECK_GE(dfd, 0);

  SCOPE_EXIT {
    ::close(dfd);
    ::unlink(filePath.string().c_str());
  };

  folly::IoUringBackend::FileOpCallback openCb = [&](int res) {
    evbPtr->terminateLoopSoon();
    CHECK_GE(res, 0);
    CHECK_EQ(0, ::close(res));
  };

  struct open_how how = {};
  how.flags = O_RDWR | O_CREAT | O_EXCL;
  how.mode = 0666;
  how.resolve = RESOLVE_IN_ROOT;

  backendPtr->queueOpenat2(dfd, path.string().c_str(), &how, std::move(openCb));
}

TEST(IoUringBackend, Close) {
  auto evbPtr = getEventBase();
  SKIP_IF(!evbPtr) << "Backend not available";

  auto* backendPtr = dynamic_cast<folly::IoUringBackend*>(evbPtr->getBackend());
  CHECK(!!backendPtr);

  auto path = folly::fs::temp_directory_path();
  path /= folly::fs::unique_path();

  int fd = ::open(path.string().c_str(), O_RDWR | O_CREAT | O_EXCL, 0666);
  CHECK_GE(fd, 0);

  SCOPE_EXIT {
    if (fd >= 0) {
      ::close(fd);
    }
    ::unlink(path.string().c_str());
  };

  folly::IoUringBackend::FileOpCallback closeCb = [&](int res) {
    evbPtr->terminateLoopSoon();
    CHECK_EQ(res, 0);
    fd = -1;
  };

  backendPtr->queueClose(fd, std::move(closeCb));

  evbPtr->loopForever();

  CHECK_EQ(fd, -1);
}

TEST(IoUringBackend, Fallocate) {
  auto evbPtr = getEventBase();
  SKIP_IF(!evbPtr) << "Backend not available";

  auto* backendPtr = dynamic_cast<folly::IoUringBackend*>(evbPtr->getBackend());
  CHECK(!!backendPtr);

  auto path = folly::fs::temp_directory_path();
  path /= folly::fs::unique_path();

  int fd = ::open(path.string().c_str(), O_RDWR | O_CREAT | O_EXCL, 0666);
  CHECK_GE(fd, 0);

  SCOPE_EXIT {
    if (fd >= 0) {
      ::close(fd);
      ::unlink(path.string().c_str());
    }
  };

  folly::IoUringBackend::FileOpCallback fallocateCb = [&](int res) {
    CHECK_EQ(res, 0);
    evbPtr->terminateLoopSoon();
  };

  backendPtr->queueFallocate(fd, 0, 0, 4096, std::move(fallocateCb));

  evbPtr->loopForever();
}

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
  static constexpr size_t kBackendCapacity = 16;
  static constexpr size_t kBackendMaxSubmit = 8;
  static constexpr size_t kBackendMaxGet = 8;

  std::unique_ptr<folly::IoUringBackend> backendReg;
  std::unique_ptr<folly::IoUringBackend> backendNoReg;

  try {
    folly::PollIoBackend::Options options;
    options.setCapacity(kBackendCapacity)
        .setMaxSubmit(kBackendMaxSubmit)
        .setMaxGet(kBackendMaxGet)
        .setUseRegisteredFds(true);

    backendReg = std::make_unique<folly::IoUringBackend>(options);
    options.setUseRegisteredFds(false);
    backendNoReg = std::make_unique<folly::IoUringBackend>(options);
  } catch (const folly::IoUringBackend::NotAvailable&) {
  }

  SKIP_IF(!backendReg) << "Backend not available";
  SKIP_IF(!backendNoReg) << "Backend not available";

  int eventFd = ::eventfd(0, EFD_CLOEXEC | EFD_SEMAPHORE | EFD_NONBLOCK);
  CHECK_GT(eventFd, 0);

  SCOPE_EXIT { ::close(eventFd); };

  // verify for useRegisteredFds = false we get a nullptr FdRegistrationRecord
  auto* record = backendNoReg->registerFd(eventFd);
  CHECK(!record);

  std::vector<folly::IoUringBackend::FdRegistrationRecord*> records;
  // we use kBackendCapacity since the timerFd
  // allocates it only on the first loop
  records.reserve(kBackendCapacity);
  for (size_t i = 0; i < kBackendCapacity; i++) {
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

TEST(IoUringBackend, FileReadWrite) {
  static constexpr size_t kBackendCapacity = 512;
  static constexpr size_t kBackendMaxSubmit = 8;
  static constexpr size_t kBackendMaxGet = 8;

  folly::PollIoBackend::Options options;
  options.setCapacity(kBackendCapacity)
      .setMaxSubmit(kBackendMaxSubmit)
      .setMaxGet(kBackendMaxGet)
      .setUseRegisteredFds(false);
  auto evbPtr = getEventBase(options);
  SKIP_IF(!evbPtr) << "Backend not available";

  static constexpr size_t kNumBlocks = 512;
  static constexpr size_t kBlockSize = 4096;
  static constexpr size_t kFileSize = kNumBlocks * kBlockSize;
  auto tempFile = folly::test::TempFileUtil::getTempFile(kFileSize);

  int fd = ::open(tempFile.path().c_str(), O_DIRECT | O_RDWR);
  SKIP_IF(fd == -1) << "Tempfile can't be opened with O_DIRECT: "
                    << folly::errnoStr(errno);
  SCOPE_EXIT { ::close(fd); };

  auto* backendPtr = dynamic_cast<folly::IoUringBackend*>(evbPtr->getBackend());
  CHECK(!!backendPtr);

  size_t num = 0;

  AlignedBuf writeData(kBlockSize, 'A'), readData(kBlockSize, 'Z');
  std::vector<AlignedBuf> writeDataVec(kNumBlocks, writeData),
      readDataVec(kNumBlocks, readData);

  CHECK(readData != writeData);

  for (size_t i = 0; i < kNumBlocks; i++) {
    folly::IoUringBackend::FileOpCallback writeCb = [&, i](int res) {
      CHECK_EQ(res, writeDataVec[i].size());
      folly::IoUringBackend::FileOpCallback readCb = [&, i](int res) {
        CHECK_EQ(res, readDataVec[i].size());
        CHECK(readDataVec[i] == writeDataVec[i]);
        ++num;
      };
      backendPtr->queueRead(
          fd,
          readDataVec[i].data(),
          readDataVec[i].size(),
          i * kBlockSize,
          std::move(readCb));
    };

    backendPtr->queueWrite(
        fd,
        writeDataVec[i].data(),
        writeDataVec[i].size(),
        i * kBlockSize,
        std::move(writeCb));
  }

  evbPtr->loop();

  EXPECT_EQ(num, kNumBlocks);
}

TEST(IoUringBackend, FileReadvWritev) {
  static constexpr size_t kBackendCapacity = 512;
  static constexpr size_t kBackendMaxSubmit = 8;
  static constexpr size_t kBackendMaxGet = 8;

  folly::PollIoBackend::Options options;
  options.setCapacity(kBackendCapacity)
      .setMaxSubmit(kBackendMaxSubmit)
      .setMaxGet(kBackendMaxGet)
      .setUseRegisteredFds(false);
  auto evbPtr = getEventBase(options);
  SKIP_IF(!evbPtr) << "Backend not available";

  static constexpr size_t kNumBlocks = 512;
  static constexpr size_t kNumIov = 4;
  static constexpr size_t kIovSize = 4096;
  static constexpr size_t kBlockSize = kNumIov * kIovSize;
  static constexpr size_t kFileSize = kNumBlocks * kBlockSize;
  auto tempFile = folly::test::TempFileUtil::getTempFile(kFileSize);

  int fd = ::open(tempFile.path().c_str(), O_DIRECT | O_RDWR);
  SKIP_IF(fd == -1) << "Tempfile can't be opened with O_DIRECT: "
                    << folly::errnoStr(errno);
  SCOPE_EXIT { ::close(fd); };

  auto* backendPtr = dynamic_cast<folly::IoUringBackend*>(evbPtr->getBackend());
  CHECK(!!backendPtr);

  size_t num = 0;

  AlignedBuf writeData(kIovSize, 'A'), readData(kIovSize, 'Z');
  std::vector<AlignedBuf> writeDataVec(kNumIov, writeData),
      readDataVec(kNumIov, readData);

  std::vector<std::vector<AlignedBuf>> writeDataVecVec(
      kNumBlocks, writeDataVec),
      readDataVecVec(kNumBlocks, readDataVec);

  CHECK(readDataVec != writeDataVec);

  std::vector<std::vector<struct iovec>> readDataIov, writeDataIov;
  std::vector<size_t> lenVec;

  readDataIov.reserve(kNumBlocks);
  writeDataIov.reserve(kNumBlocks);
  lenVec.reserve(kNumBlocks);

  for (size_t i = 0; i < kNumBlocks; i++) {
    size_t len = 0;
    std::vector<struct iovec> readIov, writeIov;
    readIov.reserve(kNumIov);
    writeIov.reserve(kNumIov);
    for (size_t j = 0; j < kNumIov; j++) {
      struct iovec riov {
        readDataVecVec[i][j].data(), readDataVecVec[i][j].size()
      };
      readIov.push_back(riov);
      struct iovec wiov {
        writeDataVecVec[i][j].data(), writeDataVecVec[i][j].size()
      };
      writeIov.push_back(wiov);
      len += riov.iov_len;
    }

    readDataIov.emplace_back(std::move(readIov));
    writeDataIov.emplace_back(std::move(writeIov));
    lenVec.emplace_back(len);
  }

  for (size_t i = 0; i < kNumBlocks; i++) {
    folly::IoUringBackend::FileOpCallback writeCb = [&, i](int res) {
      CHECK_EQ(res, lenVec[i]);
      folly::IoUringBackend::FileOpCallback readCb = [&, i](int res) {
        CHECK_EQ(res, lenVec[i]);
        CHECK(readDataVecVec[i] == writeDataVecVec[i]);
        if (++num == kNumBlocks) {
          evbPtr->terminateLoopSoon();
        }
      };

      backendPtr->queueReadv(
          fd, readDataIov[i], i * kBlockSize, std::move(readCb));
    };

    backendPtr->queueWritev(
        fd, writeDataIov[i], i * kBlockSize, std::move(writeCb));
  }

  evbPtr->loopForever();

  EXPECT_EQ(num, kNumBlocks);
}

TEST(IoUringBackend, FileReadMany) {
  static constexpr size_t kBackendCapacity = 256;
  static constexpr size_t kBackendMaxSubmit = 32;
  static constexpr size_t kBackendMaxGet = 32;

  folly::PollIoBackend::Options options;
  options.setCapacity(kBackendCapacity)
      .setMaxSubmit(kBackendMaxSubmit)
      .setMaxGet(kBackendMaxGet)
      .setUseRegisteredFds(false);
  auto evbPtr = getEventBase(options);
  SKIP_IF(!evbPtr) << "Backend not available";

  static constexpr size_t kNumBlocks = 8 * 1024;
  static constexpr size_t kBlockSize = 4096;
  static constexpr size_t kBigBlockSize = 2 * 1024 * 1024;

  static constexpr size_t kFileSize = kNumBlocks * kBlockSize;
  auto tempFile = folly::test::TempFileUtil::getTempFile(kFileSize);

  int fd = ::open(tempFile.path().c_str(), O_DIRECT | O_RDWR);
  SKIP_IF(fd == -1) << "Tempfile can't be opened with O_DIRECT: "
                    << folly::errnoStr(errno);
  SCOPE_EXIT { ::close(fd); };

  auto* backendPtr = dynamic_cast<folly::IoUringBackend*>(evbPtr->getBackend());
  CHECK(!!backendPtr);

  size_t num = 0;
  AlignedBuf readData(kBlockSize, 'Z');
  std::vector<AlignedBuf> readDataVec(kNumBlocks, readData);

  AlignedBuf bigReadData(kBigBlockSize, 'Z');

  for (size_t i = 0; i < kNumBlocks; i++) {
    folly::IoUringBackend::FileOpCallback readCb = [&, i](int res) {
      CHECK_EQ(res, readDataVec[i].size());
      ++num;
    };
    backendPtr->queueRead(
        fd,
        readDataVec[i].data(),
        readDataVec[i].size(),
        i * kBlockSize,
        std::move(readCb));
  }

  folly::IoUringBackend::FileOpCallback bigReadCb = [&](int res) {
    CHECK_EQ(res, bigReadData.size());
  };

  backendPtr->queueRead(
      fd, bigReadData.data(), bigReadData.size(), 0, std::move(bigReadCb));

  evbPtr->loop();

  EXPECT_EQ(num, kNumBlocks);
}

TEST(IoUringBackend, FileWriteMany) {
  static constexpr size_t kBackendCapacity = 256;
  static constexpr size_t kBackendMaxSubmit = 32;
  static constexpr size_t kBackendMaxGet = 32;

  folly::PollIoBackend::Options options;
  options.setCapacity(kBackendCapacity)
      .setMaxSubmit(kBackendMaxSubmit)
      .setMaxGet(kBackendMaxGet)
      .setUseRegisteredFds(false);
  auto evbPtr = getEventBase(options);
  SKIP_IF(!evbPtr) << "Backend not available";

  static constexpr size_t kNumBlocks = 8 * 1024;
  static constexpr size_t kBlockSize = 4096;
  static constexpr size_t kBigBlockSize = 2 * 1024 * 1024;

  static constexpr size_t kFileSize = kNumBlocks * kBlockSize;
  auto tempFile = folly::test::TempFileUtil::getTempFile(kFileSize);

  int fd = ::open(tempFile.path().c_str(), O_DIRECT | O_RDWR);
  SKIP_IF(fd == -1) << "Tempfile can't be opened with O_DIRECT: "
                    << folly::errnoStr(errno);
  SCOPE_EXIT { ::close(fd); };

  auto* backendPtr = dynamic_cast<folly::IoUringBackend*>(evbPtr->getBackend());
  CHECK(!!backendPtr);

  size_t num = 0;
  AlignedBuf writeData(kBlockSize, 'A');
  std::vector<AlignedBuf> writeDataVec(kNumBlocks, writeData);

  AlignedBuf bigWriteData(kBigBlockSize, 'A');

  bool bFdatasync = false;

  for (size_t i = 0; i < kNumBlocks; i++) {
    folly::IoUringBackend::FileOpCallback writeCb = [&, i](int res) {
      CHECK_EQ(res, writeDataVec[i].size());
      ++num;

      if (num == kNumBlocks) {
        folly::IoUringBackend::FileOpCallback fdatasyncCb = [&](int res) {
          CHECK_EQ(res, 0);
          bFdatasync = true;
        };
        backendPtr->queueFdatasync(fd, std::move(fdatasyncCb));
      }
    };

    backendPtr->queueWrite(
        fd,
        writeDataVec[i].data(),
        writeDataVec[i].size(),
        i * kBlockSize,
        std::move(writeCb));
  }

  evbPtr->loop();
  EXPECT_EQ(num, kNumBlocks);
  EXPECT_EQ(bFdatasync, true);

  bool bFsync = false;
  folly::IoUringBackend::FileOpCallback bigWriteCb = [&](int res) {
    CHECK_EQ(res, bigWriteData.size());

    folly::IoUringBackend::FileOpCallback fsyncCb = [&](int res) {
      CHECK_EQ(res, 0);
      bFsync = true;
    };

    backendPtr->queueFsync(fd, std::move(fsyncCb));
  };

  backendPtr->queueWrite(
      fd, bigWriteData.data(), bigWriteData.size(), 0, std::move(bigWriteCb));

  evbPtr->loop();

  EXPECT_EQ(bFsync, true);
}

TEST(IoUringBackend, SendmsgRecvmsg) {
  static constexpr size_t kBackendCapacity = 256;
  static constexpr size_t kBackendMaxSubmit = 32;
  static constexpr size_t kBackendMaxGet = 32;

  folly::PollIoBackend::Options options;
  options.setCapacity(kBackendCapacity)
      .setMaxSubmit(kBackendMaxSubmit)
      .setMaxGet(kBackendMaxGet)
      .setUseRegisteredFds(false);
  auto evbPtr = getEventBase(options);
  SKIP_IF(!evbPtr) << "Backend not available";

  auto* backendPtr = dynamic_cast<folly::IoUringBackend*>(evbPtr->getBackend());
  CHECK(!!backendPtr);

  // we want raw sockets
  auto sendFd = ::socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
  CHECK_GT(sendFd, 0);
  auto recvFd = ::socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
  CHECK_GT(recvFd, 0);

  folly::SocketAddress addr("::1", 0);

  sockaddr_storage addrStorage;
  addr.getAddress(&addrStorage);
  auto& saddr = reinterpret_cast<sockaddr&>(addrStorage);
  auto ret = ::bind(sendFd, &saddr, addr.getActualSize());
  CHECK_EQ(ret, 0);
  ret = ::bind(recvFd, &saddr, addr.getActualSize());
  CHECK_EQ(ret, 0);

  folly::SocketAddress sendAddr;
  folly::SocketAddress recvAddr;
  sendAddr.setFromLocalAddress(folly::NetworkSocket(sendFd));
  recvAddr.setFromLocalAddress(folly::NetworkSocket(recvFd));

  bool sendDone = false;
  bool recvDone = false;

  static constexpr size_t kNumBytes = 64;
  static std::array<char, kNumBytes> sendBuf, recvBuf;

  folly::IoUringBackend::FileOpCallback sendCb = [&](int res) {
    CHECK_EQ(res, kNumBytes);
    CHECK(!sendDone);
    sendDone = true;

    if (recvDone) {
      evbPtr->terminateLoopSoon();
    }
  };

  folly::IoUringBackend::FileOpCallback recvCb = [&](int res) {
    CHECK_EQ(res, kNumBytes);
    CHECK(!recvDone);
    recvDone = true;

    CHECK_EQ(::memcmp(sendBuf.data(), recvBuf.data(), kNumBytes), 0);

    if (sendDone) {
      evbPtr->terminateLoopSoon();
    }
  };

  struct msghdr sendMsg = {};
  struct msghdr recvMsg = {};
  struct iovec sendIov, recvIov;
  sendIov.iov_base = sendBuf.data();
  sendIov.iov_len = sendBuf.size();

  recvIov.iov_base = recvBuf.data();
  recvIov.iov_len = recvBuf.size();

  recvAddr.getAddress(&addrStorage);

  sendMsg.msg_iov = &sendIov;
  sendMsg.msg_iovlen = 1;

  sendMsg.msg_name = reinterpret_cast<void*>(&addrStorage);
  sendMsg.msg_namelen = recvAddr.getActualSize();

  recvMsg.msg_iov = &recvIov;
  recvMsg.msg_iovlen = 1;

  ::memset(sendBuf.data(), 0xAB, sendBuf.size());
  ::memset(recvBuf.data(), 0x0, recvBuf.size());

  CHECK_NE(::memcmp(sendBuf.data(), recvBuf.data(), kNumBytes), 0);

  backendPtr->queueRecvmsg(recvFd, &recvMsg, 0, std::move(recvCb));
  backendPtr->queueSendmsg(sendFd, &sendMsg, 0, std::move(sendCb));

  evbPtr->loopForever();

  CHECK(sendDone && recvDone);

  ::close(sendFd);
  ::close(recvFd);
}

namespace folly {
namespace test {
static constexpr size_t kCapacity = 32;
static constexpr size_t kMaxSubmit = 4;
static constexpr size_t kMaxGet = static_cast<size_t>(-1);

struct IoUringBackendProvider {
  static std::unique_ptr<folly::EventBaseBackendBase> getBackend() {
    try {
      folly::PollIoBackend::Options options;
      options.setCapacity(kCapacity)
          .setMaxSubmit(kMaxSubmit)
          .setMaxGet(kMaxGet)
          .setUseRegisteredFds(false);

      return std::make_unique<folly::IoUringBackend>(options);
    } catch (const IoUringBackend::NotAvailable&) {
      return nullptr;
    }
  }
};

struct IoUringRegFdBackendProvider {
  static std::unique_ptr<folly::EventBaseBackendBase> getBackend() {
    try {
      folly::PollIoBackend::Options options;
      options.setCapacity(kCapacity)
          .setMaxSubmit(kMaxSubmit)
          .setMaxGet(kMaxGet)
          .setUseRegisteredFds(true);
      return std::make_unique<folly::IoUringBackend>(options);
    } catch (const IoUringBackend::NotAvailable&) {
      return nullptr;
    }
  }
};

// CQ polling
struct IoUringPollCQBackendProvider {
  static std::unique_ptr<folly::EventBaseBackendBase> getBackend() {
    try {
      folly::PollIoBackend::Options options;
      options.setCapacity(kCapacity)
          .setMaxSubmit(kMaxSubmit)
          .setMaxGet(kMaxGet)
          .setUseRegisteredFds(false)
          .setFlags(folly::PollIoBackend::Options::Flags::POLL_CQ);
      return std::make_unique<folly::IoUringBackend>(options);
    } catch (const IoUringBackend::NotAvailable&) {
      return nullptr;
    }
  }
};

// SQ/CQ polling
struct IoUringPollSQCQBackendProvider {
  static std::unique_ptr<folly::EventBaseBackendBase> getBackend() {
    try {
      folly::PollIoBackend::Options options;
      options.setCapacity(kCapacity)
          .setMaxSubmit(kMaxSubmit)
          .setMaxGet(kMaxGet)
          .setUseRegisteredFds(false)
          .setFlags(
              folly::PollIoBackend::Options::Flags::POLL_SQ |
              folly::PollIoBackend::Options::Flags::POLL_CQ);
      return std::make_unique<folly::IoUringBackend>(options);
    } catch (const IoUringBackend::NotAvailable&) {
      return nullptr;
    }
  }
};

REGISTER_TYPED_TEST_SUITE_P(
    EventBaseTest,
    ReadEvent,
    ReadPersist,
    ReadImmediate,
    WriteEvent,
    WritePersist,
    WriteImmediate,
    ReadWrite,
    WriteRead,
    ReadWriteSimultaneous,
    ReadWritePersist,
    ReadPartial,
    WritePartial,
    DestroyingHandler,
    RunAfterDelay,
    RunAfterDelayDestruction,
    BasicTimeouts,
    ReuseTimeout,
    RescheduleTimeout,
    CancelTimeout,
    DestroyingTimeout,
    ScheduledFn,
    ScheduledFnAt,
    RunInThread,
    RunInEventBaseThreadAndWait,
    RunImmediatelyOrRunInEventBaseThreadAndWaitCross,
    RunImmediatelyOrRunInEventBaseThreadAndWaitWithin,
    RunImmediatelyOrRunInEventBaseThreadAndWaitNotLooping,
    RunImmediatelyOrRunInEventBaseThreadCross,
    RunImmediatelyOrRunInEventBaseThreadNotLooping,
    RepeatedRunInLoop,
    RunInLoopNoTimeMeasurement,
    RunInLoopStopLoop,
    messageAvailableException,
    TryRunningAfterTerminate,
    CancelRunInLoop,
    LoopTermination,
    CallbackOrderTest,
    AlwaysEnqueueCallbackOrderTest,
    IdleTime,
    MaxLatencyUndamped,
    UnsetMaxLatencyUndamped,
    ThisLoop,
    EventBaseThreadLoop,
    EventBaseThreadName,
    RunBeforeLoop,
    RunBeforeLoopWait,
    StopBeforeLoop,
    RunCallbacksOnDestruction,
    LoopKeepAlive,
    LoopKeepAliveInLoop,
    LoopKeepAliveWithLoopForever,
    LoopKeepAliveShutdown,
    LoopKeepAliveAtomic,
    LoopKeepAliveCast);

REGISTER_TYPED_TEST_SUITE_P(
    EventBaseTest1,
    DrivableExecutorTest,
    IOExecutorTest,
    RequestContextTest,
    CancelLoopCallbackRequestContextTest,
    TestStarvation,
    RunOnDestructionBasic,
    RunOnDestructionCancelled,
    RunOnDestructionAfterHandleDestroyed,
    RunOnDestructionAddCallbackWithinCallback,
    InternalExternalCallbackOrderTest,
    pidCheck,
    EventBaseExecutionObserver);

// Instantiate the non registered fd tests
INSTANTIATE_TYPED_TEST_SUITE_P(IoUring, EventBaseTest, IoUringBackendProvider);
INSTANTIATE_TYPED_TEST_SUITE_P(IoUring, EventBaseTest1, IoUringBackendProvider);

// Instantiate the registered fd tests
INSTANTIATE_TYPED_TEST_SUITE_P(
    IoUringRegFd, EventBaseTest, IoUringRegFdBackendProvider);
INSTANTIATE_TYPED_TEST_SUITE_P(
    IoUringRegFd, EventBaseTest1, IoUringRegFdBackendProvider);

// Instantiate the poll CQ tests
INSTANTIATE_TYPED_TEST_SUITE_P(
    IoUringPollCQ, EventBaseTest, IoUringPollCQBackendProvider);
INSTANTIATE_TYPED_TEST_SUITE_P(
    IoUringPollCQ, EventBaseTest1, IoUringPollCQBackendProvider);

// Instantiate the poll SQ/CQ tests
INSTANTIATE_TYPED_TEST_SUITE_P(
    IoUringPollSQCQ, EventBaseTest, IoUringPollCQBackendProvider);
INSTANTIATE_TYPED_TEST_SUITE_P(
    IoUringPollSQCQ, EventBaseTest1, IoUringPollCQBackendProvider);
} // namespace test
} // namespace folly
