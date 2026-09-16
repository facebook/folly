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

#include <folly/executors/IOThreadPoolExecutor.h>
#include <folly/executors/test/IOThreadPoolExecutorBaseTestLib.h>

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
