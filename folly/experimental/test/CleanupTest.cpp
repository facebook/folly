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

#include <folly/experimental/Cleanup.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/ManualExecutor.h>
#include <folly/portability/GTest.h>

using namespace std::literals::chrono_literals;

class Cleaned : public folly::Cleanup {
  folly::CPUThreadPoolExecutor pool_;

 public:
  Cleaned() : pool_(4) {
    addCleanup(
        folly::makeSemiFuture().defer([this](auto&&) { this->pool_.join(); }));
  }

  using folly::Cleanup::addCleanup;
};

TEST(CleanupTest, Basic) {
  EXPECT_TRUE(folly::is_cleanup_v<Cleaned>);

  Cleaned cleaned;
  int phase = 0;
  int index = 0;

  cleaned.addCleanup(
      folly::makeSemiFuture().deferValue([&, expected = index++](folly::Unit) {
        EXPECT_EQ(phase, 1);
        EXPECT_EQ(--index, expected);
      }));
  cleaned.addCleanup(
      folly::makeSemiFuture().deferValue([&, expected = index++](folly::Unit) {
        EXPECT_EQ(phase, 1);
        EXPECT_EQ(--index, expected);
      }));
  EXPECT_EQ(index, 2);

  folly::ManualExecutor exec;
  phase = 1;
  cleaned.cleanup()
      .within(1s)
      .via(folly::getKeepAliveToken(exec))
      .getVia(&exec);
  phase = 2;
  EXPECT_EQ(index, 0);
}

TEST(CleanupTest, EnsureCleanupAfterTaskBasic) {
  Cleaned cleaned;
  int phase = 0;
  int index = 0;

  cleaned.addCleanup(
      folly::makeSemiFuture().deferValue([&, expected = index++](folly::Unit) {
        EXPECT_EQ(phase, 1);
        EXPECT_EQ(--index, expected);
      }));

  auto task =
      folly::makeSemiFuture().deferValue([&, expected = index++](folly::Unit) {
        EXPECT_EQ(phase, 1);
        EXPECT_EQ(--index, expected);
      });
  EXPECT_EQ(index, 2);

  folly::ManualExecutor exec;
  phase = 1;
  folly::ensureCleanupAfterTask(std::move(task), cleaned.cleanup())
      .within(1s)
      .via(folly::getKeepAliveToken(exec))
      .getVia(&exec);
  phase = 2;
  EXPECT_EQ(index, 0);
}

TEST(CleanupTest, Errors) {
  auto cleaned = std::make_unique<Cleaned>();

  cleaned->addCleanup(folly::makeSemiFuture().deferValue(
      [](folly::Unit) { EXPECT_TRUE(false); }));

  cleaned->addCleanup(
      folly::makeSemiFuture<folly::Unit>(std::runtime_error("failed cleanup")));

  folly::ManualExecutor exec;
  EXPECT_EXIT(
      cleaned->cleanup()
          .within(1s)
          .via(folly::getKeepAliveToken(exec))
          .getVia(&exec),
      testing::KilledBySignal(SIGABRT),
      ".*noexcept.*");

  EXPECT_EXIT(
      cleaned.reset(), testing::KilledBySignal(SIGABRT), ".*destructed.*");

  // must leak the Cleaned as its destructor will abort.
  (void)cleaned.release();
}

TEST(CleanupTest, Invariants) {
  Cleaned cleaned;

  auto ranCleanup = false;
  cleaned.addCleanup(folly::makeSemiFuture().deferValue(
      [&](folly::Unit) { ranCleanup = true; }));

  EXPECT_FALSE(ranCleanup);

  {
    folly::ManualExecutor exec;
    cleaned.cleanup()
        .within(1s)
        .via(folly::getKeepAliveToken(exec))
        .getVia(&exec);
  }

  EXPECT_TRUE(ranCleanup);

  EXPECT_EXIT(
      cleaned.addCleanup(folly::makeSemiFuture().deferValue(
          [](folly::Unit) { EXPECT_TRUE(false); })),
      testing::KilledBySignal(SIGABRT),
      ".*addCleanup.*");

  {
    folly::ManualExecutor exec;
    EXPECT_EXIT(
        cleaned.cleanup().via(folly::getKeepAliveToken(exec)).getVia(&exec),
        testing::KilledBySignal(SIGABRT),
        ".*already.*");
  }
}
