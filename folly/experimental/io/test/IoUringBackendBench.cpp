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
#include <sys/timerfd.h>

#include <folly/Benchmark.h>
#include <folly/FileUtil.h>
#include <folly/experimental/io/IoUringBackend.h>
#include <folly/init/Init.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/EventHandler.h>
#include <folly/portability/GFlags.h>

DEFINE_bool(run_tests, false, "Run tests");
DEFINE_int32(backend_type, 0, "0 - default 1 - io_uring");
DEFINE_bool(socket_pair, false, "use socket pair");
DEFINE_bool(timer_fd, false, "use timer fd");
DEFINE_bool(async_read, true, "async read");

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

class EventFD : public EventHandler, public folly::EventReadCallback {
 public:
  EventFD(uint64_t num, uint64_t& total, bool persist, EventBase* eventBase)
      : EventFD(total, createFd(num), persist, eventBase) {}
  ~EventFD() override {
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

  ssize_t write(uint64_t val) {
    uint64_t data = val;

    return ::write(fd_, &data, sizeof(data));
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

    if (total_ > 0) {
      --total_;
      if (total_ == 0) {
        evb_->terminateLoopSoon();
      }
    }
  }

  // from folly::EventReadCallback
  folly::EventReadCallback::IoVec* allocateData() override {
    auto* ret = ioVecPtr_.release();
    return (ret ? ret : new EFDIoVec(this));
  }

 private:
  static int createFd(uint64_t num) {
    // we want it a semaphore
    int fd = ::eventfd(0, EFD_CLOEXEC | EFD_SEMAPHORE);
    CHECK_GT(fd, 0);
    CHECK_EQ(writeNoInt(fd, &num, sizeof(num)), sizeof(num));
    return fd;
  }

  EventFD(uint64_t& total, int fd, bool persist, EventBase* eventBase)
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

class EventBaseProvider {
 public:
  enum Type {
    DEFAULT,
    IO_URING,
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
  folly::EventReadCallback::IoVec* allocateData() override {
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
  folly::EventReadCallback::IoVec* allocateData() override {
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
    size_t numEvents) {
  BenchmarkSuspender suspender;
  static constexpr uint64_t kNum = 2000000000;
  if (iters > kNum) {
    iters = kNum;
  }
  uint64_t total = numEvents;
  auto evb = EventBaseProvider::getEventBase(type);
  std::vector<std::unique_ptr<EventFD>> eventsVec;
  eventsVec.reserve(numEvents);
  for (size_t i = 0; i < numEvents; i++) {
    auto ev = std::make_unique<EventFD>(kNum, total, persist, evb.get());
    ev->useAsyncReadCallback(asyncRead);
    eventsVec.emplace_back(std::move(ev));
  }
  // reset the total
  total = iters * numEvents;
  suspender.dismiss();
  evb->loopForever();
  suspender.rehire();
}

void runTestsEFD() {
  auto evb = EventBaseProvider::getEventBase(
      FLAGS_backend_type ? EventBaseProvider::Type::IO_URING
                         : EventBaseProvider::DEFAULT);
  uint64_t total = (uint64_t)(-1);
  EventFD evFd(0, total, true, evb.get());
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

BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(
    runBM, default_persist_1, EventBaseProvider::Type::DEFAULT, true, false, 1)
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
  folly::init(&argc, &argv, true);
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
folly/experimental/io/test/IoUringBackendBench.cpprelative  time/iter
iters/s
============================================================================
----------------------------------------------------------------------------
runBM(default_persist_1)                                     1.08us  923.02K
runBM(io_uring_persist_1)                         87.81%     1.23us  810.49K
runBM(io_uring_persist_async_read_1)             140.61%   770.50ns    1.30M
----------------------------------------------------------------------------
runBM(default_persist_16)                                    9.89us  101.08K
runBM(io_uring_persist_16)                        83.26%    11.88us   84.16K
runBM(io_uring_persist_async_read_16)            209.88%     4.71us  212.14K
----------------------------------------------------------------------------
runBM(default_persist_64)                                   39.12us   25.56K
runBM(io_uring_persist_64)                        84.50%    46.30us   21.60K
runBM(io_uring_persist_async_read_64)            223.12%    17.53us   57.03K
----------------------------------------------------------------------------
runBM(default_persist_128)                                  77.54us   12.90K
runBM(io_uring_persist_128)                       84.65%    91.60us   10.92K
runBM(io_uring_persist_async_read_128)           218.13%    35.55us   28.13K
----------------------------------------------------------------------------
runBM(default_persist_256)                                 152.62us    6.55K
runBM(io_uring_persist_256)                       83.51%   182.75us    5.47K
runBM(io_uring_persist_async_read_256)           218.24%    69.94us   14.30K
----------------------------------------------------------------------------
----------------------------------------------------------------------------
runBM(default_no_persist_1)                                  2.41us  414.21K
runBM(io_uring_no_persist_1)                     191.12%     1.26us  791.66K
runBM(io_uring_no_persist_async_read_1)          296.14%   815.23ns    1.23M
----------------------------------------------------------------------------
runBM(default_no_persist_16)                                32.58us   30.70K
runBM(io_uring_no_persist_16)                    267.32%    12.19us   82.06K
runBM(io_uring_no_persist_async_read_16)         594.11%     5.48us  182.37K
----------------------------------------------------------------------------
runBM(default_no_persist_64)                               136.17us    7.34K
runBM(io_uring_no_persist_64)                    282.68%    48.17us   20.76K
runBM(io_uring_no_persist_async_read_64)         603.59%    22.56us   44.33K
----------------------------------------------------------------------------
runBM(default_no_persist_128)                              275.07us    3.64K
runBM(io_uring_no_persist_128)                   283.90%    96.89us   10.32K
runBM(io_uring_no_persist_async_read_128)        636.71%    43.20us   23.15K
----------------------------------------------------------------------------
runBM(default_no_persist_256)                              550.00us    1.82K
runBM(io_uring_no_persist_256)                   292.57%   187.99us    5.32K
runBM(io_uring_no_persist_async_read256)         641.95%    85.68us   11.67K
----------------------------------------------------------------------------
============================================================================
*/
