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

#include <folly/executors/ManualExecutor.h>
#include <folly/experimental/channels/MaxConcurrentRateLimiter.h>

#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

using namespace testing;

namespace folly {
namespace channels {

class MaxConcurrentRateLimiterTest : public Test {
 public:
  MaxConcurrentRateLimiterTest() {
    manualExecutor_ = std::make_unique<ManualExecutor>();

    maxConcurrentRateLimiter_ = MaxConcurrentRateLimiter::create(5);
  }

  std::unique_ptr<ManualExecutor> manualExecutor_;
  std::shared_ptr<MaxConcurrentRateLimiter> maxConcurrentRateLimiter_;
};

TEST_F(MaxConcurrentRateLimiterTest, Basic) {
  auto tokens = std::vector<std::unique_ptr<RateLimiter::Token>>{};

  maxConcurrentRateLimiter_->executeWhenReady(
      [&](auto token) { tokens.push_back(std::move(token)); },
      manualExecutor_.get());

  manualExecutor_->drain();
  EXPECT_EQ(tokens.size(), 1);
}

TEST_F(MaxConcurrentRateLimiterTest, UnderQuota) {
  auto tokens = std::vector<std::unique_ptr<RateLimiter::Token>>{};

  for (auto i = 0; i < 5; ++i) {
    maxConcurrentRateLimiter_->executeWhenReady(
        [&](auto token) { tokens.push_back(std::move(token)); },
        manualExecutor_.get());
  }

  manualExecutor_->drain();
  EXPECT_EQ(tokens.size(), 5);
}

TEST_F(MaxConcurrentRateLimiterTest, OverQuota) {
  auto tokens = std::vector<std::unique_ptr<RateLimiter::Token>>{};

  for (auto i = 0; i < 10; ++i) {
    maxConcurrentRateLimiter_->executeWhenReady(
        [&](auto token) { tokens.push_back(std::move(token)); },
        manualExecutor_.get());
  }

  manualExecutor_->drain();
  EXPECT_EQ(tokens.size(), 5);

  for (auto i = 0; i < 5; ++i) {
    tokens.pop_back();
    manualExecutor_->drain();
    EXPECT_EQ(tokens.size(), 5);
  }

  tokens.clear();
  manualExecutor_->drain();

  EXPECT_TRUE(tokens.empty());
}

} // namespace channels
} // namespace folly
