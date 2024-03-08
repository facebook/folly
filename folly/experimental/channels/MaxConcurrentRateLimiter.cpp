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

#include <folly/experimental/channels/MaxConcurrentRateLimiter.h>

namespace folly {
namespace channels {

class MaxConcurrentRateLimiter::Token : public RateLimiter::Token {
 public:
  explicit Token(
      std::shared_ptr<MaxConcurrentRateLimiter> maxConcurrentRateLimiter)
      : maxConcurrentRateLimiter_{std::move(maxConcurrentRateLimiter)} {}

  Token(Token&&) = default;
  Token& operator=(Token&&) = default;
  Token(const Token&) = delete;
  Token& operator=(const Token&) = delete;

  ~Token() override {
    if (maxConcurrentRateLimiter_) {
      maxConcurrentRateLimiter_->release();
    }
  }

 private:
  std::shared_ptr<MaxConcurrentRateLimiter> maxConcurrentRateLimiter_;
};

std::shared_ptr<MaxConcurrentRateLimiter> MaxConcurrentRateLimiter::create(
    size_t maxConcurrent) {
  return std::shared_ptr<MaxConcurrentRateLimiter>(
      new MaxConcurrentRateLimiter(maxConcurrent));
}

MaxConcurrentRateLimiter::MaxConcurrentRateLimiter(size_t maxConcurrent)
    : maxConcurrent_(maxConcurrent) {}

void MaxConcurrentRateLimiter::executeWhenReady(
    folly::Function<void(std::unique_ptr<RateLimiter::Token>)> func,
    Executor::KeepAlive<SequencedExecutor> executor) {
  auto state = state_.wlock();
  if (state->running < maxConcurrent_) {
    CHECK(state->queue.empty());
    state->running++;
    executor->add(
        [func = std::move(func),
         token = std::make_unique<MaxConcurrentRateLimiter::Token>(
             std::static_pointer_cast<MaxConcurrentRateLimiter>(
                 shared_from_this()))]() mutable { func(std::move(token)); });
  } else {
    state->queue.enqueue(QueueItem{std::move(func), std::move(executor)});
  }
}

void MaxConcurrentRateLimiter::release() {
  auto state = state_.wlock();
  if (!state->queue.empty()) {
    auto queueItem = state->queue.dequeue();
    queueItem.executor->add(
        [func = std::move(queueItem.func),
         token = std::make_unique<MaxConcurrentRateLimiter::Token>(
             std::static_pointer_cast<MaxConcurrentRateLimiter>(
                 shared_from_this()))]() mutable { func(std::move(token)); });
  } else {
    state->running--;
  }
}

} // namespace channels
} // namespace folly
