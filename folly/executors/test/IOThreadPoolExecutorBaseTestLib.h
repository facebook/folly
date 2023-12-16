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
#include <memory>
#include <optional>

#include <folly/container/F14Set.h>
#include <folly/executors/IOThreadPoolExecutor.h>
#include <folly/portability/GTest.h>

namespace folly {
namespace test {

template <typename T>
class IOThreadPoolExecutorBaseTest : public ::testing::Test {
 public:
  IOThreadPoolExecutorBaseTest() = default;
};

TYPED_TEST_SUITE_P(IOThreadPoolExecutorBaseTest);

TYPED_TEST_P(IOThreadPoolExecutorBaseTest, IOObserver) {
  struct EventBaseAccumulator : IOThreadPoolExecutorBase::IOObserver {
    void registerEventBase(EventBase& evb) override { evbs.insert(&evb); }
    void unregisterEventBase(EventBase& evb) override { evbs.erase(&evb); }

    F14FastSet<EventBase*> evbs;
  };

  static constexpr size_t kNumThreads = 16;

  std::optional<TypeParam> ex{std::in_place, kNumThreads};
  auto observer1 = std::make_shared<EventBaseAccumulator>();
  auto observer2 = std::make_shared<EventBaseAccumulator>();
  ex->addObserver(observer1);
  ex->addObserver(observer2);
  EXPECT_EQ(observer1->evbs.size(), kNumThreads);
  EXPECT_EQ(observer2->evbs.size(), kNumThreads);
  ex->removeObserver(observer1);
  EXPECT_EQ(observer1->evbs.size(), 0);
  EXPECT_EQ(observer2->evbs.size(), kNumThreads);
  ex.reset();
  EXPECT_EQ(observer1->evbs.size(), 0);
  EXPECT_EQ(observer2->evbs.size(), 0);
}

REGISTER_TYPED_TEST_SUITE_P(IOThreadPoolExecutorBaseTest, IOObserver);

} // namespace test
} // namespace folly
