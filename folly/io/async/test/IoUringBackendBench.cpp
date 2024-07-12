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
#include <sys/timerfd.h>

#include <folly/Benchmark.h>
#include <folly/FileUtil.h>
#include <folly/experimental/io/EpollBackend.h>
#include <folly/experimental/io/IoUringBackend.h>
#include <folly/init/Init.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/EventHandler.h>
#include <folly/io/async/ScopedEventBaseThread.h>
#include <folly/portability/GFlags.h>

DEFINE_bool(run_tests, false, "Run tests");
DEFINE_int32(backend_type, 0, "0 - default 1 - io_uring");
DEFINE_bool(socket_pair, false, "use socket pair");
DEFINE_bool(timer_fd, false, "use timer fd");
DEFINE_bool(async_read, true, "async read");
DEFINE_bool(async_refill, true, "async refill");

using namespace folly;

namespace {
std::chrono::time_point<std::chrono::steady_clock> sWriteTS0;
std::chrono::time_point<std::chrono::steady_clock> sWriteTS1;
std::chrono::time_point<std::chrono::steady_clock> sNotifyTS;
std::chrono::time_point<std::chrono::steady_clock> sReadTS0;
std::chrono::time_point<std::chrono::steady_clock> sReadTS1;

uint64_t sTotal = 0;
uint64_t sNum = 0;

void printTS() {
  sTotal +=
      std::chrono::duration_cast<std::chrono::nanoseconds>(sReadTS1 - sWriteTS0)
          .count();
  sNum++;
  LOG(INFO) << "backend: " << ((FLAGS_backend_type == 0) ? "epoll" : "io_uring")
            << " async read: " << FLAGS_async_read << " write time: "
            << std::chrono::duration_cast<std::chrono::nanoseconds>(
                   sWriteTS1 - sWriteTS0)
                   .count()
            << " notify time: "
            << std::chrono::duration_cast<std::chrono::nanoseconds>(
                   sNotifyTS - sWriteTS0)
                   .count()
            << " read time "
            << std::chrono::duration_cast<std::chrono::nanoseconds>(
                   sReadTS1 - sReadTS0)
                   .count()
            << " total time "
            << std::chrono::duration_cast<std::chrono::nanoseconds>(
                   sReadTS1 - sWriteTS0)
                   .count()
            << " avg time " << (sTotal / sNum);
}

class EventFD;
struct EventFDRefillInfo {
  size_t num{0};
  size_t curr{0};
  size_t refillNum{0};
  EventBase* evb_{nullptr};
  std::vector<std::unique_ptr<EventFD>>* events{nullptr};
};

class EventFD : public EventHandler, public folly::EventReadCallback {
 public:
  EventFD(
      uint64_t num,
      uint64_t& total,
      bool persist,
      EventBase* eventBase,
      EventFDRefillInfo* refillInfo)
      : EventFD(total, createFd(num), persist, eventBase, refillInfo) {}

  ~EventFD() override {
    unregisterHandler();

    if (fd_ > 0) {
      unregisterHandler();
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

  void setNumReadPerLoop(size_t numReadPerLoop) {
    numReadPerLoop_ = numReadPerLoop;
  }

  ssize_t write(uint64_t val) {
    uint64_t data = val;

    return ::write(fd_, &data, sizeof(data));
  }

  // from folly::EventHandler
  void handlerReady(uint16_t /*events*/) noexcept override {
    for (size_t i = 0; i < numReadPerLoop_; ++i) {
      if (FLAGS_run_tests) {
        sReadTS0 = sNotifyTS = std::chrono::steady_clock::now();
      }
      uint64_t data = 0;
      auto ret = ::read(fd_, &data, sizeof(data));

      if (FLAGS_run_tests) {
        sReadTS1 = std::chrono::steady_clock::now();
        printTS();
      }
      CHECK_EQ(ret, sizeof(data));
      CHECK_EQ(data, 1);
    }

    if (!persist_) {
      registerHandler(folly::EventHandler::READ);
    }
    if (total_ > 0) {
      total_ -= std::min(numReadPerLoop_, total_);
      if (total_ == 0) {
        evb_->terminateLoopSoon();
      }
    }
    if (refillInfo_ && refillInfo_->events) {
      processOne();
    }
  }

  void processOne() {
    ++refillInfo_->curr;
    if (refillInfo_->curr >= refillInfo_->num) {
      refillInfo_->curr = 0;
      refillInfo_->evb_->runInEventBaseThreadAlwaysEnqueue([&]() {
        for (auto& ev : *refillInfo_->events) {
          ev->write(refillInfo_->refillNum);
        }
      });
    }
  }

  struct EFDIoVec : public folly::EventReadCallback::IoVec {
    EFDIoVec() = delete;
    ~EFDIoVec() override = default;
    explicit EFDIoVec(EventFD* eventFd) {
      arg_ = eventFd;
      freeFunc_ = EFDIoVec::free;
      cbFunc_ = EFDIoVec::cb;
      data_.iov_base = &eventData_;
      data_.iov_len = sizeof(eventData_);
    }

    static void free(EventReadCallback::IoVec* ioVec) { delete ioVec; }

    static void cb(EventReadCallback::IoVec* ioVec, int res) {
      if (FLAGS_run_tests) {
        sReadTS0 = sReadTS1 = sNotifyTS = std::chrono::steady_clock::now();
        printTS();
      }
      reinterpret_cast<EventFD*>(ioVec->arg_)
          ->cb(reinterpret_cast<EFDIoVec*>(ioVec), res);
    }

    uint64_t eventData_{0};
  };

  void cb(EFDIoVec* ioVec, int res) {
    CHECK_EQ(res, sizeof(EFDIoVec::eventData_));
    CHECK_EQ(ioVec->eventData_, 1);
    // reset it
    ioVec->eventData_ = 0;
    // save it for future use
    ioVecPtr_.reset(ioVec);
    if (!persist_) {
      registerHandler(folly::EventHandler::READ);
    }

    if (refillInfo_ && refillInfo_->events) {
      processOne();
    }

    if (total_ > 0) {
      --total_;
      if (total_ == 0) {
        evb_->terminateLoopSoon();
      }
    }
  }

  // from folly::EventReadCallback
  folly::EventReadCallback::IoVec* allocateData() noexcept override {
    auto* ret = ioVecPtr_.release();
    return (ret ? ret : new EFDIoVec(this));
  }

 private:
  static int createFd(uint64_t num) {
    // we want it a semaphore
    int fd = ::eventfd(0, EFD_CLOEXEC | EFD_SEMAPHORE);
    CHECK_GT(fd, 0);
    CHECK_EQ(writeNoInt(fd, &num, sizeof(num)), sizeof(num));
    CHECK_EQ(fcntl(fd, F_SETFL, O_NONBLOCK), 0);

    return fd;
  }

  EventFD(
      uint64_t& total,
      int fd,
      bool persist,
      EventBase* eventBase,
      EventFDRefillInfo* refillInfo)
      : EventHandler(eventBase, NetworkSocket::fromFd(fd)),
        total_(total),
        fd_(fd),
        persist_(persist),
        evb_(eventBase),
        refillInfo_(refillInfo) {
    if (persist_) {
      registerHandler(folly::EventHandler::READ | folly::EventHandler::PERSIST);
    } else {
      registerHandler(folly::EventHandler::READ);
    }
  }
  uint64_t& total_;
  size_t numReadPerLoop_{1};
  int fd_{-1};
  bool persist_;
  EventBase* evb_;
  EventFDRefillInfo* refillInfo_;
  std::unique_ptr<IoVec> ioVecPtr_;
};

class EventBaseProvider {
 public:
  enum Type {
    DEFAULT,
    IO_URING,
    EPOLL,
  };

  static std::unique_ptr<folly::EventBase> getEventBase(
      Type type, size_t capacity = 32 * 1024) {
    switch (type) {
      case DEFAULT: {
        return std::make_unique<folly::EventBase>();
      }
      case IO_URING: {
        try {
          folly::PollIoBackend::Options opts;
          opts.setCapacity(capacity).setMaxSubmit(256);

          auto factory = [opts] {
            return std::make_unique<folly::IoUringBackend>(opts);
          };
          return std::make_unique<folly::EventBase>(
              folly::EventBase::Options().setBackendFactory(
                  std::move(factory)));
        } catch (const folly::IoUringBackend::NotAvailable&) {
          return nullptr;
        }
      }
      case EPOLL: {
        folly::EpollBackend::Options opts;
        opts.setNumLoopEvents(256);

        auto factory = [opts] {
          return std::make_unique<folly::EpollBackend>(opts);
        };
        return std::make_unique<folly::EventBase>(
            folly::EventBase::Options().setBackendFactory(std::move(factory)));
      }
    }

    CHECK(false);
    return nullptr;
  }
};

class SocketPair : public EventHandler, public folly::EventReadCallback {
 public:
  SocketPair(
      int readFd,
      int writeFd,
      uint64_t& total,
      bool persist,
      EventBase* eventBase)
      : EventHandler(eventBase, NetworkSocket::fromFd(readFd)),
        readFd_(readFd),
        writeFd_(writeFd),
        total_(total),
        persist_(persist),
        evb_(eventBase) {
    if (persist_) {
      registerHandler(folly::EventHandler::READ | folly::EventHandler::PERSIST);
    } else {
      registerHandler(folly::EventHandler::READ);
    }
  }

  ~SocketPair() override {
    unregisterHandler();

    if (readFd_ > 0) {
      changeHandlerFD(NetworkSocket());
      ::close(readFd_);
      ::close(writeFd_);
    }
  }

  static int socketpair(int& readFd, int& writeFd) {
    int fd[2];
    int ret = ::socketpair(AF_UNIX, SOCK_STREAM, 0, fd);
    if (ret) {
      readFd = writeFd = -1;
      return ret;
    }

    readFd = fd[0];
    writeFd = fd[1];

    return 0;
  }

  void useAsyncReadCallback(bool val) {
    if (val) {
      setEventCallback(this);
    } else {
      resetEventCallback();
    }
  }

  ssize_t write(uint8_t val) {
    uint8_t data = val;

    return ::write(writeFd_, &data, sizeof(data));
  }

  // from folly::EventHandler
  void handlerReady(uint16_t /*events*/) noexcept override {
    if (FLAGS_run_tests) {
      sReadTS0 = sNotifyTS = std::chrono::steady_clock::now();
    }
    uint8_t data = 0;
    auto ret = ::read(readFd_, &data, sizeof(data));
    if (FLAGS_run_tests) {
      sReadTS1 = std::chrono::steady_clock::now();
      printTS();
    }
    CHECK_EQ(ret, sizeof(data));
    CHECK_EQ(data, 1);

    if (!persist_) {
      registerHandler(folly::EventHandler::READ);
    }
    if (total_ > 0) {
      --total_;
      if (total_ == 0) {
        evb_->terminateLoopSoon();
      }
    }
  }

  struct SPIoVec : public folly::EventReadCallback::IoVec {
    SPIoVec() = delete;
    ~SPIoVec() override = default;
    explicit SPIoVec(SocketPair* sp) {
      arg_ = sp;
      freeFunc_ = SPIoVec::free;
      cbFunc_ = SPIoVec::cb;
      data_.iov_base = &rdata_;
      data_.iov_len = sizeof(rdata_);
    }

    static void free(EventReadCallback::IoVec* ioVec) { delete ioVec; }

    static void cb(EventReadCallback::IoVec* ioVec, int res) {
      if (FLAGS_run_tests) {
        sReadTS0 = sReadTS1 = sNotifyTS = std::chrono::steady_clock::now();
        printTS();
      }
      reinterpret_cast<SocketPair*>(ioVec->arg_)
          ->cb(reinterpret_cast<SPIoVec*>(ioVec), res);
    }

    uint8_t rdata_{0};
  };

  void cb(SPIoVec* ioVec, int res) {
    CHECK_EQ(res, sizeof(SPIoVec::rdata_));
    CHECK_EQ(ioVec->rdata_, 1);
    // reset it
    ioVec->rdata_ = 0;
    // save it for future use
    ioVecPtr_.reset(ioVec);
    if (!persist_) {
      registerHandler(folly::EventHandler::READ);
    }

    if (total_ > 0) {
      --total_;
      if (total_ == 0) {
        evb_->terminateLoopSoon();
      }
    }
  }

  // from folly::EventReadCallback
  folly::EventReadCallback::IoVec* allocateData() noexcept override {
    auto* ret = ioVecPtr_.release();
    return (ret ? ret : new SPIoVec(this));
  }

 private:
  int readFd_{-1}, writeFd_{-1};
  uint64_t& total_;
  bool persist_;
  EventBase* evb_;
  std::unique_ptr<IoVec> ioVecPtr_;
};

class TimerFD : public EventHandler, public folly::EventReadCallback {
 public:
  TimerFD(uint64_t& total, bool persist, EventBase* eventBase)
      : TimerFD(total, createFd(), persist, eventBase) {}
  ~TimerFD() override {
    unregisterHandler();

    if (fd_ > 0) {
      changeHandlerFD(NetworkSocket());
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

  ssize_t write(uint64_t /*unused*/) {
    struct itimerspec val;
    val.it_interval = {0, 0};
    val.it_value.tv_sec = 0;
    val.it_value.tv_nsec = 1000;

    return (0 == ::timerfd_settime(fd_, 0, &val, nullptr)) ? sizeof(uint64_t)
                                                           : -1;
  }

  // from folly::EventHandler
  void handlerReady(uint16_t /*events*/) noexcept override {
    if (FLAGS_run_tests) {
      sReadTS0 = sNotifyTS = std::chrono::steady_clock::now();
    }
    uint64_t data = 0;
    auto ret = ::read(fd_, &data, sizeof(data));
    if (FLAGS_run_tests) {
      sReadTS1 = std::chrono::steady_clock::now();
      printTS();
    }
    CHECK_EQ(ret, sizeof(data));
    CHECK_EQ(data, 1);

    if (!persist_) {
      registerHandler(folly::EventHandler::READ);
    }
    if (total_ > 0) {
      --total_;
      if (total_ == 0) {
        evb_->terminateLoopSoon();
      }
    }
  }

  struct TimerFDIoVec : public folly::EventReadCallback::IoVec {
    TimerFDIoVec() = delete;
    ~TimerFDIoVec() override = default;
    explicit TimerFDIoVec(TimerFD* timerFd) {
      arg_ = timerFd;
      freeFunc_ = TimerFDIoVec::free;
      cbFunc_ = TimerFDIoVec::cb;
      data_.iov_base = &timerData_;
      data_.iov_len = sizeof(timerData_);
    }

    static void free(EventReadCallback::IoVec* ioVec) { delete ioVec; }

    static void cb(EventReadCallback::IoVec* ioVec, int res) {
      if (FLAGS_run_tests) {
        sReadTS0 = sReadTS1 = sNotifyTS = std::chrono::steady_clock::now();
        printTS();
      }
      reinterpret_cast<TimerFD*>(ioVec->arg_)
          ->cb(reinterpret_cast<TimerFDIoVec*>(ioVec), res);
    }

    uint64_t timerData_{0};
  };

  void cb(TimerFDIoVec* ioVec, int res) {
    CHECK_EQ(res, sizeof(TimerFDIoVec::timerData_));
    CHECK_NE(ioVec->timerData_, 0);
    // reset it
    ioVec->timerData_ = 0;
    // save it for future use
    ioVecPtr_.reset(ioVec);
    if (!persist_) {
      registerHandler(folly::EventHandler::READ);
    }

    if (total_ > 0) {
      --total_;
      if (total_ == 0) {
        evb_->terminateLoopSoon();
      }
    }
  }

  // from folly::EventReadCallback
  folly::EventReadCallback::IoVec* allocateData() noexcept override {
    auto* ret = ioVecPtr_.release();
    return (ret ? ret : new TimerFDIoVec(this));
  }

 private:
  static int createFd() {
    int fd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    CHECK_GT(fd, 0);
    return fd;
  }

  TimerFD(uint64_t& total, int fd, bool persist, EventBase* eventBase)
      : EventHandler(eventBase, NetworkSocket::fromFd(fd)),
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
  uint64_t& total_;
  int fd_{-1};
  bool persist_;
  EventBase* evb_;
  std::unique_ptr<IoVec> ioVecPtr_;
};

void runBM(
    unsigned int iters,
    EventBaseProvider::Type type,
    bool persist,
    bool asyncRead,
    size_t numEvents,
    size_t numReadPerLoop = 1,
    size_t refillNum = 0) {
  BenchmarkSuspender suspender;
  static constexpr uint64_t kNum = 2000000000;
  if (iters > kNum) {
    iters = kNum;
  }
  uint64_t total = numEvents;
  auto evb = EventBaseProvider::getEventBase(type);
  evb->runAfterDelay([]() {}, 1000 * 1000); // just a long timeout
  std::unique_ptr<ScopedEventBaseThread> refillThread;
  EventFDRefillInfo refillInfo;
  std::vector<std::unique_ptr<EventFD>> eventsVec;
  eventsVec.reserve(numEvents);
  uint64_t num = refillNum ? refillNum : kNum;
  if (refillNum) {
    refillInfo.events = &eventsVec;
    refillInfo.num = eventsVec.size();
    refillInfo.refillNum = refillNum;
    if (FLAGS_async_refill) {
      refillThread = std::make_unique<ScopedEventBaseThread>();
      refillInfo.evb_ = refillThread->getEventBase();
    } else {
      refillInfo.evb_ = evb.get();
    }
  }
  for (size_t i = 0; i < numEvents; i++) {
    auto ev =
        std::make_unique<EventFD>(num, total, persist, evb.get(), &refillInfo);
    ev->setNumReadPerLoop(numReadPerLoop);
    ev->useAsyncReadCallback(asyncRead);
    eventsVec.emplace_back(std::move(ev));
  }
  // reset the total
  total = iters * numEvents;
  suspender.dismiss();
  evb->loopForever();
  suspender.rehire();
  refillThread.reset();
  eventsVec.clear();
  evb.reset();
}

void runTestsEFD() {
  auto evb = EventBaseProvider::getEventBase(
      FLAGS_backend_type ? EventBaseProvider::Type::IO_URING
                         : EventBaseProvider::DEFAULT);
  uint64_t total = (uint64_t)(-1);
  EventFDRefillInfo refillInfo;
  EventFD evFd(0, total, true, evb.get(), &refillInfo);
  evFd.useAsyncReadCallback(FLAGS_async_read);
  std::thread setThread([&]() {
    ::pthread_setcanceltype(PTHREAD_CANCEL_DISABLE, nullptr);
    while (true) {
      sleep(1);
      sWriteTS0 = std::chrono::steady_clock::now();
      evFd.write(1);
      sWriteTS1 = std::chrono::steady_clock::now();
    }
  });
  evb->loopForever();
}

void runTestsTimerFD() {
  auto evb = EventBaseProvider::getEventBase(
      FLAGS_backend_type ? EventBaseProvider::Type::IO_URING
                         : EventBaseProvider::DEFAULT);
  uint64_t total = (uint64_t)(-1);
  TimerFD timerFd(total, true, evb.get());
  timerFd.useAsyncReadCallback(FLAGS_async_read);
  std::thread setThread([&]() {
    ::pthread_setcanceltype(PTHREAD_CANCEL_DISABLE, nullptr);
    while (true) {
      sleep(1);
      sWriteTS0 = std::chrono::steady_clock::now();
      timerFd.write(1);
      sWriteTS1 = std::chrono::steady_clock::now();
    }
  });
  evb->loopForever();
}

void runTestsSP() {
  auto evb = EventBaseProvider::getEventBase(
      FLAGS_backend_type ? EventBaseProvider::Type::IO_URING
                         : EventBaseProvider::DEFAULT);
  uint64_t total = (uint64_t)(-1);
  int readFd = -1, writeFd = -1;
  CHECK_EQ(SocketPair::socketpair(readFd, writeFd), 0);
  SocketPair sp(readFd, writeFd, total, true, evb.get());
  sp.useAsyncReadCallback(FLAGS_async_read);
  std::thread setThread([&]() {
    ::pthread_setcanceltype(PTHREAD_CANCEL_DISABLE, nullptr);
    while (true) {
      sleep(1);
      sWriteTS0 = std::chrono::steady_clock::now();
      sp.write(1);
      sWriteTS1 = std::chrono::steady_clock::now();
    }
  });
  evb->loopForever();
}

} // namespace

// refill
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(
    runBM,
    default_persist_1_refill,
    EventBaseProvider::Type::DEFAULT,
    true,
    false,
    1,
    1,
    1)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    default_persist_1_read_4_refill,
    EventBaseProvider::Type::DEFAULT,
    true,
    false,
    1,
    4,
    4)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    epoll_persist_1_refill,
    EventBaseProvider::Type::EPOLL,
    true,
    false,
    1,
    1,
    1)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    epoll_persist_1_read_4_refill,
    EventBaseProvider::Type::EPOLL,
    true,
    false,
    1,
    4,
    4)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    io_uring_persist_1_refill,
    EventBaseProvider::Type::IO_URING,
    true,
    false,
    1,
    1,
    1)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    io_uring_persist_async_read_1_refill,
    EventBaseProvider::Type::IO_URING,
    true,
    true,
    1,
    1,
    1)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(
    runBM,
    default_persist_16_refill,
    EventBaseProvider::Type::DEFAULT,
    true,
    false,
    16,
    1,
    1)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    default_persist_16_read_4_refill,
    EventBaseProvider::Type::DEFAULT,
    true,
    false,
    16,
    4,
    4)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    epoll_persist_16_refill,
    EventBaseProvider::Type::EPOLL,
    true,
    false,
    16,
    1,
    1)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    epoll_persist_16_read_4_refill,
    EventBaseProvider::Type::EPOLL,
    true,
    false,
    16,
    4,
    4)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    io_uring_persist_16_refill,
    EventBaseProvider::Type::IO_URING,
    true,
    false,
    16,
    1,
    1)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    io_uring_persist_async_read_16_refill,
    EventBaseProvider::Type::IO_URING,
    true,
    true,
    16)
BENCHMARK_DRAW_LINE();
// no refill
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(
    runBM, default_persist_1, EventBaseProvider::Type::DEFAULT, true, false, 1)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    default_persist_1_read_4,
    EventBaseProvider::Type::DEFAULT,
    true,
    false,
    1,
    4)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM, epoll_persist_1, EventBaseProvider::Type::EPOLL, true, false, 1)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    epoll_persist_1_read_4,
    EventBaseProvider::Type::EPOLL,
    true,
    false,
    1,
    4)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    io_uring_persist_1,
    EventBaseProvider::Type::IO_URING,
    true,
    false,
    1)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    io_uring_persist_async_read_1,
    EventBaseProvider::Type::IO_URING,
    true,
    true,
    1)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(
    runBM,
    default_persist_16,
    EventBaseProvider::Type::DEFAULT,
    true,
    false,
    16)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    default_persist_16_read_4,
    EventBaseProvider::Type::DEFAULT,
    true,
    false,
    16,
    4)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM, epoll_persist_16, EventBaseProvider::Type::EPOLL, true, false, 16)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    epoll_persist_16_read_4,
    EventBaseProvider::Type::EPOLL,
    true,
    false,
    16,
    4)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    io_uring_persist_16,
    EventBaseProvider::Type::IO_URING,
    true,
    false,
    16)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    io_uring_persist_async_read_16,
    EventBaseProvider::Type::IO_URING,
    true,
    true,
    16)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(
    runBM,
    default_persist_64,
    EventBaseProvider::Type::DEFAULT,
    true,
    false,
    64)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    default_persist_64_read_4,
    EventBaseProvider::Type::DEFAULT,
    true,
    false,
    64,
    4)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM, epoll_persist_64, EventBaseProvider::Type::EPOLL, true, false, 64)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    epoll_persist_64_read_4,
    EventBaseProvider::Type::EPOLL,
    true,
    false,
    64,
    4)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    io_uring_persist_64,
    EventBaseProvider::Type::IO_URING,
    true,
    false,
    64)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    io_uring_persist_async_read_64,
    EventBaseProvider::Type::IO_URING,
    true,
    true,
    64)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(
    runBM,
    default_persist_128,
    EventBaseProvider::Type::DEFAULT,
    true,
    false,
    128)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    default_persist_128_read_4,
    EventBaseProvider::Type::DEFAULT,
    true,
    false,
    128,
    4)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM, epoll_persist_128, EventBaseProvider::Type::EPOLL, true, false, 128)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    epoll_persist_128_read_4,
    EventBaseProvider::Type::EPOLL,
    true,
    false,
    128,
    4)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    io_uring_persist_128,
    EventBaseProvider::Type::IO_URING,
    true,
    false,
    128)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    io_uring_persist_async_read_128,
    EventBaseProvider::Type::IO_URING,
    true,
    true,
    128)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(
    runBM,
    default_persist_256,
    EventBaseProvider::Type::DEFAULT,
    true,
    false,
    256)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    default_persist_256_read_4,
    EventBaseProvider::Type::DEFAULT,
    true,
    false,
    256,
    4)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM, epoll_persist_256, EventBaseProvider::Type::EPOLL, true, false, 256)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    epoll_persist_256_read_4,
    EventBaseProvider::Type::EPOLL,
    true,
    false,
    256,
    4)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    io_uring_persist_256,
    EventBaseProvider::Type::IO_URING,
    true,
    false,
    256)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    io_uring_persist_async_read_256,
    EventBaseProvider::Type::IO_URING,
    true,
    true,
    256)
BENCHMARK_DRAW_LINE();

BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(
    runBM,
    default_no_persist_1,
    EventBaseProvider::Type::DEFAULT,
    false,
    false,
    1)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM, epoll_no_persist_1, EventBaseProvider::Type::EPOLL, false, false, 1)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    io_uring_no_persist_1,
    EventBaseProvider::Type::IO_URING,
    false,
    false,
    1)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    io_uring_no_persist_async_read_1,
    EventBaseProvider::Type::IO_URING,
    false,
    true,
    1)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(
    runBM,
    default_no_persist_16,
    EventBaseProvider::Type::DEFAULT,
    false,
    false,
    16)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    epoll_no_persist_16,
    EventBaseProvider::Type::EPOLL,
    false,
    false,
    16)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    io_uring_no_persist_16,
    EventBaseProvider::Type::IO_URING,
    false,
    false,
    16)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    io_uring_no_persist_async_read_16,
    EventBaseProvider::Type::IO_URING,
    false,
    true,
    16)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(
    runBM,
    default_no_persist_64,
    EventBaseProvider::Type::DEFAULT,
    false,
    false,
    64)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    epoll_no_persist_64,
    EventBaseProvider::Type::EPOLL,
    false,
    false,
    64)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    io_uring_no_persist_64,
    EventBaseProvider::Type::IO_URING,
    false,
    false,
    64)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    io_uring_no_persist_async_read_64,
    EventBaseProvider::Type::IO_URING,
    false,
    true,
    64)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(
    runBM,
    default_no_persist_128,
    EventBaseProvider::Type::DEFAULT,
    false,
    false,
    128)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    epoll_no_persist_128,
    EventBaseProvider::Type::EPOLL,
    false,
    false,
    128)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    io_uring_no_persist_128,
    EventBaseProvider::Type::IO_URING,
    false,
    false,
    128)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    io_uring_no_persist_async_read_128,
    EventBaseProvider::Type::IO_URING,
    false,
    true,
    128)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(
    runBM,
    default_no_persist_256,
    EventBaseProvider::Type::DEFAULT,
    false,
    false,
    256)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    epoll_no_persist_256,
    EventBaseProvider::Type::EPOLL,
    false,
    false,
    256)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    io_uring_no_persist_256,
    EventBaseProvider::Type::IO_URING,
    false,
    false,
    256)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    io_uring_no_persist_async_read256,
    EventBaseProvider::Type::IO_URING,
    false,
    true,
    256)
BENCHMARK_DRAW_LINE();

int main(int argc, char** argv) {
  folly::Init init(&argc, &argv, true);
  if (FLAGS_run_tests) {
    if (FLAGS_socket_pair) {
      runTestsSP();
    } else {
      if (FLAGS_timer_fd) {
        runTestsTimerFD();
      } else {
        runTestsEFD();
      }
    }
  } else {
    runBenchmarks();
  }
}

/*
./io_uring_backend_bench --bm_min_iters=100000
============================================================================
folly/experimental/io/test/IoUringBackendBench.cpprelative  time/iter  iters/s
============================================================================
----------------------------------------------------------------------------
runBM(default_persist_1)                                   599.99ns    1.67M
runBM(default_persist_1_read_4)                  176.18%   340.56ns    2.94M
runBM(io_uring_persist_1)                         89.12%   673.23ns    1.49M
runBM(io_uring_persist_async_read_1)             132.72%   452.06ns    2.21M
----------------------------------------------------------------------------
runBM(default_persist_16)                                    5.46us  183.04K
runBM(default_persist_16_read_4)                 120.65%     4.53us  220.85K
runBM(io_uring_persist_16)                        86.63%     6.31us  158.58K
runBM(io_uring_persist_async_read_16)            205.47%     2.66us  376.10K
----------------------------------------------------------------------------
runBM(default_persist_64)                                   22.29us   44.87K
runBM(default_persist_64_read_4)                 118.33%    18.83us   53.10K
runBM(io_uring_persist_64)                        90.85%    24.53us   40.77K
runBM(io_uring_persist_async_read_64)            218.56%    10.20us   98.07K
----------------------------------------------------------------------------
runBM(default_persist_128)                                  42.10us   23.75K
runBM(default_persist_128_read_4)                119.11%    35.35us   28.29K
runBM(io_uring_persist_128)                       88.84%    47.39us   21.10K
runBM(io_uring_persist_async_read_128)           196.95%    21.38us   46.78K
----------------------------------------------------------------------------
runBM(default_persist_256)                                  84.18us   11.88K
runBM(default_persist_256_read_4)                113.78%    73.98us   13.52K
runBM(io_uring_persist_256)                       87.72%    95.96us   10.42K
runBM(io_uring_persist_async_read_256)           199.68%    42.16us   23.72K
----------------------------------------------------------------------------
----------------------------------------------------------------------------
runBM(default_no_persist_1)                                  1.37us  730.68K
runBM(io_uring_no_persist_1)                     207.09%   660.87ns    1.51M
runBM(io_uring_no_persist_async_read_1)          294.15%   465.27ns    2.15M
----------------------------------------------------------------------------
runBM(default_no_persist_16)                                19.82us   50.46K
runBM(io_uring_no_persist_16)                    314.58%     6.30us  158.73K
runBM(io_uring_no_persist_async_read_16)         638.66%     3.10us  322.25K
----------------------------------------------------------------------------
runBM(default_no_persist_64)                                85.58us   11.69K
runBM(io_uring_no_persist_64)                    342.52%    24.98us   40.03K
runBM(io_uring_no_persist_async_read_64)         595.89%    14.36us   69.63K
----------------------------------------------------------------------------
runBM(default_no_persist_128)                              170.66us    5.86K
runBM(io_uring_no_persist_128)                   341.57%    49.96us   20.01K
runBM(io_uring_no_persist_async_read_128)        602.88%    28.31us   35.33K
----------------------------------------------------------------------------
runBM(default_no_persist_256)                              349.99us    2.86K
runBM(io_uring_no_persist_256)                   340.59%   102.76us    9.73K
runBM(io_uring_no_persist_async_read256)         642.10%    54.51us   18.35K
----------------------------------------------------------------------------
*/
