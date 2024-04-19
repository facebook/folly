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

#include <folly/Function.h>
#include <folly/executors/SequencedExecutor.h>

namespace folly {
namespace channels {

/**
 * A rate-limiter used by the channels framework to limit the number of
 * in-flight requests.
 *
 * A default implementation is provided in MaxConcurrentRateLimiter.h but users
 * can provide custom rate-limiters.
 */
class RateLimiter : public std::enable_shared_from_this<RateLimiter> {
 public:
  class Token;
  virtual ~RateLimiter() = default;

  /**
   * Executes the given function when there is capacity available in the
   * rate-limiter.
   *
   * The function is considered finished when the token is destroyed.
   */
  virtual void executeWhenReady(
      folly::Function<void(std::unique_ptr<Token>)> function,
      Executor::KeepAlive<SequencedExecutor> executor) = 0;
};

/**
 * A token on destruction signals termination of the user provided function. So
 * it's expected that a derived class override the destructor to provide the
 * desired functionality.  Or piggyback on destruction of the compiler generated
 * overridden destructor.
 */
class RateLimiter::Token {
 public:
  virtual ~Token() = default;
};

} // namespace channels
} // namespace folly
