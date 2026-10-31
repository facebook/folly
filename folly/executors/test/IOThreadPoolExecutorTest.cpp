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

#include <atomic>
#include <stdexcept>
#include <system_error>
#include <thread>

#include <folly/executors/IOThreadPoolExecutor.h>
#include <folly/executors/test/IOThreadPoolExecutorBaseTestLib.h>
#include <folly/synchronization/Baton.h>

namespace folly {
namespace test {

namespace {

[[noreturn]] std::unique_ptr<EventBaseBackendBase>
throwEventBaseConstructionFailure() {
  throw std::system_error(std::make_error_code(std::errc::too_many_files_open));
}

} // namespace

TEST(IOThreadPoolExecutor, EventBaseConstructionFailurePropagates) {
  auto options =
      EventBase::Options{}.setBackendFactory(throwEventBaseConstructionFailure);
  auto manager = EventBaseManager{std::move(options)};
  auto constructExecutor = [&] {
    auto executor = IOThreadPoolExecutor{
        1, 1, std::make_shared<NamedThreadFactory>("IOThreadPool"), &manager};
  };

  EXPECT_THROW(constructExecutor(), std::system_error);
}

TEST(IOThreadPoolExecutor, DynamicEventBaseConstructionFailureIsHandled) {
  auto options =
      EventBase::Options{}.setBackendFactory(throwEventBaseConstructionFailure);
  auto manager = EventBaseManager{std::move(options)};
  auto executor = IOThreadPoolExecutor{
      1, 0, std::make_shared<NamedThreadFactory>("IOThreadPool"), &manager};

  EXPECT_THROW(executor.add([] {}), std::runtime_error);
  EXPECT_EQ(executor.numActiveThreads(), 0);
}

TEST(IOThreadPoolExecutor, GetAllEventBasesRecoversFromStartupFailure) {
  std::atomic<int> attempts{0};
  auto options = EventBase::Options{}.setBackendFactory(
      [&attempts]() -> std::unique_ptr<EventBaseBackendBase> {
        if (attempts.fetch_add(1) == 0) {
          throwEventBaseConstructionFailure();
        }
        return EventBase::getDefaultBackend();
      });
  auto manager = EventBaseManager{std::move(options)};
  auto executor = IOThreadPoolExecutor{
      1, 0, std::make_shared<NamedThreadFactory>("IOThreadPool"), &manager};

  EXPECT_THROW(executor.getAllEventBases(), std::system_error);
  EXPECT_EQ(executor.numActiveThreads(), 0);

  auto eventBases = executor.getAllEventBases();
  EXPECT_EQ(eventBases.size(), 1);
  EXPECT_EQ(executor.numActiveThreads(), 1);
}

TEST(IOThreadPoolExecutor, GetEventBaseKeepAliveStartsOnlyOneThread) {
  auto executor = IOThreadPoolExecutor{4, 0};
  auto eventBase = static_cast<IOExecutor&>(executor).getEventBaseKeepAlive();

  ASSERT_TRUE(eventBase);
  EXPECT_EQ(1, executor.numActiveThreads());
  eventBase->runInEventBaseThreadAndWait([&] {
    EXPECT_TRUE(eventBase->isInEventBaseThread());
  });
}

TEST(IOThreadPoolExecutor, GetEventBaseKeepAliveEmptyPoolThrows) {
  auto executor = IOThreadPoolExecutor{0};

  EXPECT_THROW(executor.getEventBaseKeepAlive(), std::runtime_error);
}

TEST(IOThreadPoolExecutor, GetEventBaseKeepAliveStartupFailureIsHandled) {
  auto options =
      EventBase::Options{}.setBackendFactory(throwEventBaseConstructionFailure);
  auto manager = EventBaseManager{std::move(options)};
  auto executor = IOThreadPoolExecutor{
      1, 0, std::make_shared<NamedThreadFactory>("IOThreadPool"), &manager};

  EXPECT_THROW(executor.getEventBaseKeepAlive(), std::runtime_error);
  EXPECT_EQ(0, executor.numActiveThreads());
}

TEST(IOThreadPoolExecutor, GetEventBaseKeepAliveSurvivesDownsize) {
  auto executor = IOThreadPoolExecutor{1, 1};
  auto eventBase = static_cast<IOExecutor&>(executor).getEventBaseKeepAlive();
  ASSERT_TRUE(eventBase);

  Baton<> destructionStarted;
  std::atomic<bool> destructionFinished{false};
  eventBase->runOnDestructionStart([&] { destructionStarted.post(); });
  eventBase->runOnDestruction([&] { destructionFinished = true; });

  std::thread downsizing([&] { executor.setNumThreads(0); });
  destructionStarted.wait();
  EXPECT_FALSE(destructionFinished);

  bool ran = false;
  eventBase->runInEventBaseThreadAndWait([&] { ran = true; });
  EXPECT_TRUE(ran);

  auto copy = eventBase;
  eventBase.reset();
  EXPECT_FALSE(destructionFinished);
  copy.reset();
  downsizing.join();

  EXPECT_TRUE(destructionFinished);
  EXPECT_EQ(0, executor.numActiveThreads());
  EXPECT_THROW(executor.getEventBaseKeepAlive(), std::runtime_error);
}

TEST(IOThreadPoolExecutor, GetEventBaseKeepAliveConcurrentDownsize) {
  auto executor = IOThreadPoolExecutor{2, 2};
  Baton<> start;
  size_t completed = 0;
  constexpr size_t kIterations = 100;

  std::thread caller([&] {
    start.wait();
    for (size_t i = 0; i < kIterations; ++i) {
      auto eventBase = executor.getEventBaseKeepAlive();
      eventBase->runInEventBaseThreadAndWait([&] { ++completed; });
    }
  });
  start.post();
  for (size_t i = 0; i < 10; ++i) {
    executor.setNumThreads(1);
    executor.setNumThreads(2);
  }
  caller.join();

  EXPECT_EQ(kIterations, completed);
}

TEST(IOThreadPoolExecutor, MaxReadAtOnce) {
  {
    auto executor = IOThreadPoolExecutor{1};
    EXPECT_EQ(executor.getEventBase()->getMaxReadAtOnce(), 10);
  }

  {
    auto saver = FlagSaver{};
    FLAGS_folly_iothreadpoolexecutor_max_read_at_once = 0;
    auto executor = IOThreadPoolExecutor{1};
    EXPECT_EQ(executor.getEventBase()->getMaxReadAtOnce(), 0);
  }

  {
    auto saver = FlagSaver{};
    FLAGS_folly_iothreadpoolexecutor_max_read_at_once = 0;
    auto options = IOThreadPoolExecutor::Options{};
    options.setMaxReadAtOnce(42);
    auto executor = IOThreadPoolExecutor{
        1,
        std::make_shared<NamedThreadFactory>("IOThreadPool"),
        EventBaseManager::get(),
        std::move(options)};

    EXPECT_EQ(executor.getEventBase()->getMaxReadAtOnce(), 42);
  }
}

INSTANTIATE_TYPED_TEST_SUITE_P(
    IOThreadPoolExecutorTest,
    IOThreadPoolExecutorBaseTest,
    IOThreadPoolExecutor);

} // namespace test
} // namespace folly
