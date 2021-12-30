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

#include <folly/experimental/channels/RateLimiter.h>

namespace folly {
namespace channels {

std::shared_ptr<RateLimiter> RateLimiter::create(size_t maxConcurrent) {
  return std::shared_ptr<RateLimiter>(new RateLimiter(maxConcurrent));
}

RateLimiter::RateLimiter(size_t maxConcurrent)
    : maxConcurrent_(maxConcurrent) {}

void RateLimiter::executeWhenReady(
    RateLimiter::QueuedFunc func,
    Executor::KeepAlive<SequencedExecutor> executor) {
  auto state = state_.wlock();
  if (state->running < maxConcurrent_) {
    CHECK(state->queue.empty());
    state->running++;
    executor->add(
        [func = std::move(func), token = Token(shared_from_this())]() mutable {
          func(std::move(token));
        });
  } else {
    state->queue.enqueue(QueueItem{std::move(func), std::move(executor)});
  }
}

RateLimiter::Token::Token(std::shared_ptr<RateLimiter> rateLimiter)
    : rateLimiter_(std::move(rateLimiter)) {}

RateLimiter::Token::~Token() {
  if (!rateLimiter_) {
    return;
  }
  auto state = rateLimiter_->state_.wlock();
  if (!state->queue.empty()) {
    auto queueItem = state->queue.dequeue();
    queueItem.executor->add(
        [func = std::move(queueItem.func),
         token = Token(rateLimiter_->shared_from_this())]() mutable {
          func(std::move(token));
        });
  } else {
    state->running--;
  }
}
} // namespace channels
} // namespace folly
