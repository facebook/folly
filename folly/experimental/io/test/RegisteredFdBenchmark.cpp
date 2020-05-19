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

#include <folly/Benchmark.h>
#include <folly/FileUtil.h>
#include <folly/experimental/io/IoUringBackend.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/EventHandler.h>
#include <folly/portability/GFlags.h>

using namespace folly;

namespace {
class EventFD : public EventHandler {
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

  // from folly::EventHandler
  void handlerReady(uint16_t /*events*/) noexcept override {
    // we do not read to leave the fd signalled
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

 private:
  static int createFd(uint64_t num) {
    // we want a semaphore
    int fd = ::eventfd(0, EFD_CLOEXEC | EFD_SEMAPHORE | EFD_NONBLOCK);
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
};

// static constexpr size_t kAsyncIoEvents = 2048;
class BackendEventBase : public EventBase {
 public:
  explicit BackendEventBase(bool useRegisteredFds, size_t capacity = 32 * 1024)
      : EventBase(getBackend(useRegisteredFds, capacity), false) {}

 private:
  static std::unique_ptr<folly::EventBaseBackendBase> getBackend(
      bool useRegisteredFds,
      size_t capacity) {
    folly::PollIoBackend::Options options;
    options.setCapacity(capacity)
        .setMaxSubmit(256)
        .setMaxGet(128)
        .setUseRegisteredFds(useRegisteredFds);
    return std::make_unique<IoUringBackend>(options);
  }
};

void runTest(
    unsigned int iters,
    bool persist,
    bool useRegisteredFds,
    size_t numEvents) {
  BenchmarkSuspender suspender;
  uint64_t total = iters * numEvents;
  BackendEventBase evb(useRegisteredFds);
  std::vector<std::unique_ptr<EventFD>> eventsVec;
  eventsVec.reserve(numEvents);
  for (size_t i = 0; i < numEvents; i++) {
    eventsVec.emplace_back(std::make_unique<EventFD>(1, total, persist, &evb));
  }

  suspender.dismiss();
  evb.loopForever();
  suspender.rehire();
}

} // namespace
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(runTest, io_uring_persist_1, true, false, 1)
BENCHMARK_RELATIVE_NAMED_PARAM(runTest, io_uring_persist_reg_1, true, true, 1)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(runTest, io_uring_persist_64, true, false, 64)
BENCHMARK_RELATIVE_NAMED_PARAM(runTest, io_uring_persist_reg_64, true, true, 64)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(runTest, io_uring_persist_128, true, false, 128)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runTest,
    io_uring_persist_reg_128,
    true,
    true,
    128)
BENCHMARK_DRAW_LINE();
// add a non persistent benchamrk too
// so we can see the useRegisteredFds flag
// does not have any effect
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(runTest, io_uring_no_persist_128, false, false, 128)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runTest,
    io_uring_no_persist_reg_128,
    false,
    true,
    128)
BENCHMARK_DRAW_LINE();

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  runBenchmarks();
}
