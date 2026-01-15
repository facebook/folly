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

#include <folly/Benchmark.h>
#include <folly/FileUtil.h>
#include <folly/experimental/io/EpollBackend.h>
#include <folly/init/Init.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/EventHandler.h>
#include <folly/io/async/IoUringBackend.h>
#include <folly/io/async/ScopedEventBaseThread.h>
#include <folly/portability/GFlags.h>

DEFINE_bool(async_refill, true, "async refill");

using namespace folly;

namespace {

class EventFD;
struct EventFDRefillInfo {
  size_t num{0};
  size_t curr{0};
  size_t refillNum{0};
  EventBase* evb_{nullptr};
  std::vector<std::unique_ptr<EventFD>>* events{nullptr};
};

class EventFD : public EventHandler {
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
      fileops::close(fd_);
      fd_ = -1;
    }
  }

  void setNumReadPerLoop(size_t numReadPerLoop) {
    numReadPerLoop_ = numReadPerLoop;
  }

  ssize_t write(uint64_t val) {
    uint64_t data = val;

    return fileops::write(fd_, &data, sizeof(data));
  }

  // from folly::EventHandler
  void handlerReady(uint16_t /*events*/) noexcept override {
    for (size_t i = 0; i < numReadPerLoop_; ++i) {
      uint64_t data = 0;
      auto ret = fileops::read(fd_, &data, sizeof(data));

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
};

class EventBaseProvider {
 public:
  enum Type {
    DEFAULT,
    IO_URING,
    EPOLL,
  };

  static std::unique_ptr<folly::EventBase> getEventBase(
      Type type, size_t capacity = 1024) {
    switch (type) {
      case DEFAULT: {
        return std::make_unique<folly::EventBase>();
      }
      case IO_URING: {
        try {
          folly::PollIoBackend::Options opts;
          opts.setCapacity(capacity).setMaxSubmit(256);

          auto optsPtr =
              std::make_shared<folly::PollIoBackend::Options>(std::move(opts));
          auto factory = [optsPtr]() mutable {
            return std::make_unique<folly::IoUringBackend>(std::move(*optsPtr));
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

void runBM(
    unsigned int iters,
    EventBaseProvider::Type type,
    bool persist,
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

} // namespace

// refill
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(
    runBM,
    default_persist_1_refill,
    EventBaseProvider::Type::DEFAULT,
    true,
    1,
    1,
    1)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    default_persist_1_read_4_refill,
    EventBaseProvider::Type::DEFAULT,
    true,
    1,
    4,
    4)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    epoll_persist_1_refill,
    EventBaseProvider::Type::EPOLL,
    true,
    1,
    1,
    1)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    epoll_persist_1_read_4_refill,
    EventBaseProvider::Type::EPOLL,
    true,
    1,
    4,
    4)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    io_uring_persist_1_refill,
    EventBaseProvider::Type::IO_URING,
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
    16,
    1,
    1)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    default_persist_16_read_4_refill,
    EventBaseProvider::Type::DEFAULT,
    true,
    16,
    4,
    4)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    epoll_persist_16_refill,
    EventBaseProvider::Type::EPOLL,
    true,
    16,
    1,
    1)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    epoll_persist_16_read_4_refill,
    EventBaseProvider::Type::EPOLL,
    true,
    16,
    4,
    4)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    io_uring_persist_16_refill,
    EventBaseProvider::Type::IO_URING,
    true,
    16,
    1,
    1)
BENCHMARK_DRAW_LINE();
// no refill
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(
    runBM, default_persist_1, EventBaseProvider::Type::DEFAULT, true, 1)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    default_persist_1_read_4,
    EventBaseProvider::Type::DEFAULT,
    true,
    1,
    4)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM, epoll_persist_1, EventBaseProvider::Type::EPOLL, true, 1)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM, epoll_persist_1_read_4, EventBaseProvider::Type::EPOLL, true, 1, 4)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM, io_uring_persist_1, EventBaseProvider::Type::IO_URING, true, 1)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(
    runBM, default_persist_16, EventBaseProvider::Type::DEFAULT, true, 16)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    default_persist_16_read_4,
    EventBaseProvider::Type::DEFAULT,
    true,
    16,
    4)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM, epoll_persist_16, EventBaseProvider::Type::EPOLL, true, 16)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM, epoll_persist_16_read_4, EventBaseProvider::Type::EPOLL, true, 16, 4)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM, io_uring_persist_16, EventBaseProvider::Type::IO_URING, true, 16)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(
    runBM, default_persist_64, EventBaseProvider::Type::DEFAULT, true, 64)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    default_persist_64_read_4,
    EventBaseProvider::Type::DEFAULT,
    true,
    64,
    4)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM, epoll_persist_64, EventBaseProvider::Type::EPOLL, true, 64)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM, epoll_persist_64_read_4, EventBaseProvider::Type::EPOLL, true, 64, 4)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM, io_uring_persist_64, EventBaseProvider::Type::IO_URING, true, 64)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(
    runBM, default_persist_128, EventBaseProvider::Type::DEFAULT, true, 128)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    default_persist_128_read_4,
    EventBaseProvider::Type::DEFAULT,
    true,
    128,
    4)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM, epoll_persist_128, EventBaseProvider::Type::EPOLL, true, 128)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    epoll_persist_128_read_4,
    EventBaseProvider::Type::EPOLL,
    true,
    128,
    4)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM, io_uring_persist_128, EventBaseProvider::Type::IO_URING, true, 128)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(
    runBM, default_persist_256, EventBaseProvider::Type::DEFAULT, true, 256)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    default_persist_256_read_4,
    EventBaseProvider::Type::DEFAULT,
    true,
    256,
    4)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM, epoll_persist_256, EventBaseProvider::Type::EPOLL, true, 256)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    epoll_persist_256_read_4,
    EventBaseProvider::Type::EPOLL,
    true,
    256,
    4)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM, io_uring_persist_256, EventBaseProvider::Type::IO_URING, true, 256)
BENCHMARK_DRAW_LINE();

BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(
    runBM, default_no_persist_1, EventBaseProvider::Type::DEFAULT, false, 1)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM, epoll_no_persist_1, EventBaseProvider::Type::EPOLL, false, 1)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM, io_uring_no_persist_1, EventBaseProvider::Type::IO_URING, false, 1)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(
    runBM, default_no_persist_16, EventBaseProvider::Type::DEFAULT, false, 16)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM, epoll_no_persist_16, EventBaseProvider::Type::EPOLL, false, 16)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM, io_uring_no_persist_16, EventBaseProvider::Type::IO_URING, false, 16)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(
    runBM, default_no_persist_64, EventBaseProvider::Type::DEFAULT, false, 64)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM, epoll_no_persist_64, EventBaseProvider::Type::EPOLL, false, 64)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM, io_uring_no_persist_64, EventBaseProvider::Type::IO_URING, false, 64)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(
    runBM, default_no_persist_128, EventBaseProvider::Type::DEFAULT, false, 128)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM, epoll_no_persist_128, EventBaseProvider::Type::EPOLL, false, 128)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    io_uring_no_persist_128,
    EventBaseProvider::Type::IO_URING,
    false,
    128)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(
    runBM, default_no_persist_256, EventBaseProvider::Type::DEFAULT, false, 256)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM, epoll_no_persist_256, EventBaseProvider::Type::EPOLL, false, 256)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runBM,
    io_uring_no_persist_256,
    EventBaseProvider::Type::IO_URING,
    false,
    256)
BENCHMARK_DRAW_LINE();

int main(int argc, char** argv) {
  folly::Init init(&argc, &argv, true);
  runBenchmarks();
}

/*
$ buck run @mode/opt //folly/io/async/test:io_uring_backend_bench -- \
--bm_min_iters=100000
============================================================================
[...]io/async/test/IoUringBackendBench.cpp     relative  time/iter   iters/s
============================================================================
runBM(default_persist_1_refill)                             9.51us   105.10K
runBM(default_persist_1_read_4_refill)          395.25%     2.41us   415.41K
runBM(epoll_persist_1_refill)                   103.49%     9.19us   108.77K
runBM(epoll_persist_1_read_4_refill)            385.35%     2.47us   405.01K
runBM(io_uring_persist_1_refill)                100.60%     9.46us   105.73K
----------------------------------------------------------------------------
runBM(default_persist_16_refill)                           17.97us    55.65K
runBM(default_persist_16_read_4_refill)         237.58%     7.56us   132.21K
runBM(epoll_persist_16_refill)                  116.06%    15.48us    64.59K
runBM(epoll_persist_16_read_4_refill)           256.37%     7.01us   142.67K
runBM(io_uring_persist_16_refill)               98.152%    18.31us    54.62K
----------------------------------------------------------------------------
runBM(default_persist_1)                                  544.64ns     1.84M
runBM(default_persist_1_read_4)                 224.31%   242.80ns     4.12M
runBM(epoll_persist_1)                          137.28%   396.73ns     2.52M
runBM(epoll_persist_1_read_4)                   265.08%   205.46ns     4.87M
runBM(io_uring_persist_1)                       109.05%   499.44ns     2.00M
----------------------------------------------------------------------------
runBM(default_persist_16)                                   3.44us   290.54K
runBM(default_persist_16_read_4)                133.56%     2.58us   388.05K
runBM(epoll_persist_16)                         107.09%     3.21us   311.13K
runBM(epoll_persist_16_read_4)                  136.62%     2.52us   396.93K
runBM(io_uring_persist_16)                      78.378%     4.39us   227.72K
----------------------------------------------------------------------------
runBM(default_persist_64)                                  12.74us    78.48K
runBM(default_persist_64_read_4)                126.81%    10.05us    99.51K
runBM(epoll_persist_64)                         104.11%    12.24us    81.70K
runBM(epoll_persist_64_read_4)                  128.57%     9.91us   100.89K
runBM(io_uring_persist_64)                      75.697%    16.83us    59.40K
----------------------------------------------------------------------------
runBM(default_persist_128)                                 25.66us    38.96K
runBM(default_persist_128_read_4)               127.24%    20.17us    49.58K
runBM(epoll_persist_128)                        103.57%    24.78us    40.36K
runBM(epoll_persist_128_read_4)                 128.75%    19.93us    50.17K
runBM(io_uring_persist_128)                     76.656%    33.48us    29.87K
----------------------------------------------------------------------------
runBM(default_persist_256)                                 50.93us    19.63K
runBM(default_persist_256_read_4)               126.92%    40.13us    24.92K
runBM(epoll_persist_256)                        103.13%    49.39us    20.25K
runBM(epoll_persist_256_read_4)                 128.29%    39.70us    25.19K
runBM(io_uring_persist_256)                     75.215%    67.72us    14.77K
----------------------------------------------------------------------------
runBM(default_no_persist_1)                                 1.06us   944.53K
runBM(epoll_no_persist_1)                       118.53%   893.20ns     1.12M
runBM(io_uring_no_persist_1)                    208.61%   507.51ns     1.97M
----------------------------------------------------------------------------
runBM(default_no_persist_16)                               12.46us    80.28K
runBM(epoll_no_persist_16)                      106.36%    11.71us    85.38K
runBM(io_uring_no_persist_16)                   267.24%     4.66us   214.53K
----------------------------------------------------------------------------
runBM(default_no_persist_64)                               51.91us    19.26K
runBM(epoll_no_persist_64)                      110.21%    47.11us    21.23K
runBM(io_uring_no_persist_64)                   296.14%    17.53us    57.04K
----------------------------------------------------------------------------
runBM(default_no_persist_128)                             102.67us     9.74K
runBM(epoll_no_persist_128)                     107.42%    95.57us    10.46K
runBM(io_uring_no_persist_128)                  291.41%    35.23us    28.38K
----------------------------------------------------------------------------
runBM(default_no_persist_256)                             218.32us     4.58K
runBM(epoll_no_persist_256)                     111.42%   195.95us     5.10K
runBM(io_uring_no_persist_256)                  312.72%    69.81us    14.32K
----------------------------------------------------------------------------
*/
