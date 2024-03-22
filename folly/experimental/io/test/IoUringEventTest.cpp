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

#include <array>
#include <chrono>
#include <map>
#include <vector>

#include <folly/experimental/io/IoUringBackend.h>
#include <folly/experimental/io/IoUringEvent.h>
#include <folly/futures/Future.h>
#include <folly/futures/Promise.h>
#include <folly/io/async/EventBase.h>
#include <folly/portability/GTest.h>

namespace folly {

struct TestParams {
  bool eventfd = false;
  std::string testName() const { return eventfd ? "eventfd" : "direct"; }
};

class IoUringEventTest : public ::testing::TestWithParam<TestParams> {
 public:
  static IoUringBackend::Options options() { return IoUringBackend::Options{}; }

  EventBase eb;
  IoUringEvent event{&eb, options(), GetParam().eventfd};
};

struct Observer : EventBaseObserver {
  uint32_t getSampleRate() const override { return 1; }
  void loopSample(int64_t, int64_t) override { count++; }
  int count = 0;

  static std::shared_ptr<Observer> set(EventBase& eb) {
    auto ret = std::make_shared<Observer>();
    eb.setObserver(ret);
    return ret;
  }
};

struct NopSqe : IoSqeBase {
  void processSubmit(struct io_uring_sqe* sqe) noexcept override {
    io_uring_prep_nop(sqe);
  }
  void callback(const io_uring_cqe* cqe) noexcept override {
    prom.setValue(cqe->res);
  }
  void callbackCancelled(const io_uring_cqe*) noexcept override {
    prom.setException(FutureCancellation{});
  }

  Promise<int> prom;
};

TEST_P(IoUringEventTest, Basic) {
  NopSqe nop;
  event.backend().submit(nop);
  EXPECT_EQ(0, nop.prom.getSemiFuture().via(&eb).getVia(&eb));
}

TEST_P(IoUringEventTest, SubmitSoon) {
  NopSqe nop;
  event.backend().submitSoon(nop);
  EXPECT_EQ(0, nop.prom.getSemiFuture().via(&eb).getVia(&eb));
}

TEST_P(IoUringEventTest, StopsLooping) {
  NopSqe nop;
  event.backend().submitSoon(nop);
  EXPECT_EQ(0, nop.prom.getSemiFuture().via(&eb).getVia(&eb));

  eb.loopOnce();

  VLOG(3) << "now observing";

  auto obs = Observer::set(eb);
  folly::futures::sleep(std::chrono::milliseconds(100)).via(&eb).getVia(&eb);
  EXPECT_LE(obs->count, 2) << "should not spin with no work to do";
}

INSTANTIATE_TEST_SUITE_P(
    IoUringEventTest,
    IoUringEventTest,
    ::testing::Values(TestParams{true}, TestParams{false}),
    [](const ::testing::TestParamInfo<TestParams>& info) {
      return info.param.testName();
    });
} // namespace folly
