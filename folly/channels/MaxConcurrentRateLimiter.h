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
#include <folly/experimental/channels/RateLimiter.h>

namespace folly {
namespace channels {

class MaxConcurrentRateLimiter : public RateLimiter {
 public:
  static std::shared_ptr<MaxConcurrentRateLimiter> create(size_t maxConcurrent);

  void executeWhenReady(
      folly::Function<void(std::unique_ptr<Token>)> func,
      Executor::KeepAlive<SequencedExecutor> executor) override;

 private:
  class Token;
  friend class Token;

  explicit MaxConcurrentRateLimiter(size_t maxConcurrent);
  void release();

  struct QueueItem {
    folly::Function<void(std::unique_ptr<Token>)> func;
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
