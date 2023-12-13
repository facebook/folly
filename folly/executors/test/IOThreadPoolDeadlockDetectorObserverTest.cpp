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

#include <memory>

#include <fmt/core.h>

#include <glog/logging.h>
#include <folly/concurrency/DeadlockDetector.h>
#include <folly/executors/IOThreadPoolDeadlockDetectorObserver.h>
#include <folly/executors/IOThreadPoolExecutor.h>
#include <folly/portability/GTest.h>

using namespace folly;

class DeadlockDetectorMock : public DeadlockDetector {
 public:
  DeadlockDetectorMock(std::shared_ptr<std::atomic<int32_t>> counter)
      : counter_(counter) {
    counter_->fetch_add(1);
  }
  ~DeadlockDetectorMock() override { counter_->fetch_sub(1); }

 private:
  std::shared_ptr<std::atomic<int32_t>> counter_;
};

class DeadlockDetectorFactoryMock : public DeadlockDetectorFactory {
 public:
  std::unique_ptr<DeadlockDetector> create(
      folly::Executor* executor, const std::string& name) override {
    EXPECT_TRUE(executor != nullptr);
    auto tid = folly::getOSThreadID();
    std::string expectedName = fmt::format("TestPool:{}", tid);
    EXPECT_EQ(expectedName, name);
    {
      auto* getter =
          CHECK_NOTNULL(dynamic_cast<GetThreadIdCollector*>(executor));
      auto* tidCollector = CHECK_NOTNULL(getter->getThreadIdCollector());
      auto tids = tidCollector->collectThreadIds();
      EXPECT_EQ(1, tids.threadIds.size());
      EXPECT_EQ(tid, tids.threadIds[0]);
    }
    name_ = name;
    auto retval = std::make_unique<DeadlockDetectorMock>(counter_);
    baton.post();
    return retval;
  }

  int32_t getCounter() { return counter_->load(); }

  std::string getName() const { return name_.copy(); }

  folly::Baton<> baton;

 private:
  std::shared_ptr<std::atomic<int32_t>> counter_ =
      std::make_shared<std::atomic<int32_t>>(0);
  folly::Synchronized<std::string> name_;
};

TEST(
    IOThreadPoolDeadlockDetectorObserverTest,
    VerifyDeadlockDetectorCreatedAndDestroyedOnAddRemoveObserver) {
  auto deadlockDetectorFactory =
      std::make_shared<DeadlockDetectorFactoryMock>();

  auto executor = std::make_shared<IOThreadPoolExecutor>(
      1, std::make_shared<folly::NamedThreadFactory>("TestPool"));
  pid_t thid;
  executor->getEventBase()->runInEventBaseThreadAndWait(
      [&] { thid = folly::getOSThreadID(); });

  auto observer = std::make_shared<folly::IOThreadPoolDeadlockDetectorObserver>(
      deadlockDetectorFactory.get(), "TestPool");

  ASSERT_EQ(0, deadlockDetectorFactory->getCounter())
      << "Deadlock detector not yet registered";

  executor->addObserver(observer);
  deadlockDetectorFactory->baton.wait();
  deadlockDetectorFactory->baton.reset();
  ASSERT_EQ(1, deadlockDetectorFactory->getCounter())
      << "Deadlock detector must be registered by observer";
  EXPECT_EQ(
      fmt::format("TestPool:{}", thid), deadlockDetectorFactory->getName());

  executor->removeObserver(observer);
  ASSERT_EQ(0, deadlockDetectorFactory->getCounter())
      << "Removing observer must destroy deadlock detector";

  executor->addObserver(observer);
  deadlockDetectorFactory->baton.wait();
  deadlockDetectorFactory->baton.reset();
  ASSERT_EQ(1, deadlockDetectorFactory->getCounter())
      << "Deadlock detector must be registered by observer";

  executor->stop();
  ASSERT_EQ(0, deadlockDetectorFactory->getCounter())
      << "Stopping threadpool must deregister all observers";
}
