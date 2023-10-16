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

#include <folly/Singleton.h>
#include <folly/futures/Future.h>
#include <folly/portability/GTest.h>

using namespace folly;
using namespace std::chrono_literals;

// These tests only exercise the timekeeper plumbing when the default timekeeper
// is replaced/mocked. Timekeeper functionality and future integration is tested
// separately for each implementation in their respective test suites.
class TimekeeperBase : public testing::Test {
 protected:
  using TimekeeperSingleton =
      Singleton<Timekeeper, detail::TimekeeperSingletonTag>;

  void TearDown() override {
    // Invalidate any mocks that were installed.
    folly::SingletonVault::singleton()->destroyInstances();
    folly::SingletonVault::singleton()->reenableInstances();
  }
};

TEST_F(TimekeeperBase, FutureSleepHandlesNullTimekeeperSingleton) {
  TimekeeperSingleton::make_mock([] { return nullptr; });
  EXPECT_THROW(futures::sleep(1ms).get(), FutureNoTimekeeper);
}

TEST_F(TimekeeperBase, FutureWithinHandlesNullTimekeeperSingleton) {
  TimekeeperSingleton::make_mock([] { return nullptr; });
  Promise<int> p;
  auto f = p.getFuture().within(1ms);
  EXPECT_THROW(std::move(f).get(), FutureNoTimekeeper);
}

TEST_F(TimekeeperBase, SemiFutureWithinHandlesNullTimekeeperSingleton) {
  TimekeeperSingleton::make_mock([] { return nullptr; });
  Promise<int> p;
  auto f = p.getSemiFuture().within(1ms);
  EXPECT_THROW(std::move(f).get(), FutureNoTimekeeper);
}

TEST_F(TimekeeperBase, SemiFutureWithinCancelsTimeout) {
  struct MockTimekeeper : Timekeeper {
    MockTimekeeper() {
      p_.setInterruptHandler([this](const exception_wrapper& ew) {
        ew.handle([this](const FutureCancellation&) { cancelled_ = true; });
        p_.setException(ew);
      });
    }

    SemiFuture<Unit> after(HighResDuration) override {
      return p_.getSemiFuture();
    }

    Promise<Unit> p_;
    bool cancelled_{false};
  };

  MockTimekeeper tk;

  Promise<int> p;
  auto f = p.getSemiFuture().within(10s, static_cast<Timekeeper*>(&tk));
  p.setValue(1);
  f.wait();
  EXPECT_TRUE(tk.cancelled_);
}

TEST_F(TimekeeperBase, SemiFutureWithinInlineAfter) {
  struct MockTimekeeper : Timekeeper {
    SemiFuture<Unit> after(HighResDuration) override {
      return folly::makeSemiFuture<folly::Unit>(folly::FutureNoTimekeeper());
    }
  };

  MockTimekeeper tk;

  Promise<int> p;
  auto f = p.getSemiFuture().within(10s, static_cast<Timekeeper*>(&tk));
  EXPECT_THROW(std::move(f).get(), folly::FutureNoTimekeeper);
}

TEST_F(TimekeeperBase, SemiFutureWithinReady) {
  struct MockTimekeeper : Timekeeper {
    SemiFuture<Unit> after(HighResDuration) override {
      called_ = true;
      return folly::makeSemiFuture<folly::Unit>(folly::FutureNoTimekeeper());
    }

    bool called_{false};
  };

  MockTimekeeper tk;

  Promise<int> p;
  p.setValue(1);
  auto f = p.getSemiFuture().within(10s, static_cast<Timekeeper*>(&tk));
  f.wait();
  EXPECT_FALSE(tk.called_);
}
