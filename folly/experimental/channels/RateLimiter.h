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

#pragma once

#include <folly/Synchronized.h>
#include <folly/concurrency/UnboundedQueue.h>
#include <folly/executors/SequencedExecutor.h>

namespace folly {
namespace channels {

class RateLimiter : public std::enable_shared_from_this<RateLimiter> {
 public:
  static std::shared_ptr<RateLimiter> create(size_t maxConcurrent);

  class Token {
   public:
    explicit Token(std::shared_ptr<RateLimiter> rateLimiter);
    ~Token();

    Token(const Token&) = delete;
    Token& operator=(const Token&) = delete;
    Token(Token&&) = default;
    Token& operator=(Token&&) = default;

   private:
    std::shared_ptr<RateLimiter> rateLimiter_;
  };

  using QueuedFunc = folly::Function<void(Token)>;

  void executeWhenReady(
      QueuedFunc func, Executor::KeepAlive<SequencedExecutor> executor);

 private:
  explicit RateLimiter(size_t maxConcurrent);

  struct QueueItem {
    QueuedFunc func;
    Executor::KeepAlive<SequencedExecutor> executor;
  };

  struct State {
    USPSCQueue<QueueItem, false /* MayBlock */, 6 /* LgSegmentSize */> queue;
    size_t running{0};
  };

  const size_t maxConcurrent_;
  folly::Synchronized<State> state_;
};
} // namespace channels
} // namespace folly
