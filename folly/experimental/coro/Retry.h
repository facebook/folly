/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <folly/CancellationToken.h>
#include <folly/ExceptionWrapper.h>
#include <folly/Random.h>
#include <folly/Try.h>
#include <folly/experimental/coro/Error.h>
#include <folly/experimental/coro/Sleep.h>
#include <folly/experimental/coro/Task.h>
#include <folly/experimental/coro/Traits.h>

#include <cstdint>
#include <random>
#include <utility>

namespace folly::coro {

// Execute a given asynchronous operation returned by func(),
// retrying it on failure, if desired, after awaiting
// retryDelay(error).
//
// If 'func()' operation succeeds or completes with OperationCancelled
// then completes immediately with that result.
//
// Otherwise, if it fails with an error then the function
// 'retryDelay()' is invoked with the exception_wrapper for
// the error and must return another Task<void>.
//
// If this task completes successfully or completes with then it will retry
// the func() operation, otherwise if it completes with an
// error then the whole operation will complete with that error.
//
// This allows you to do some asynchronous work between retries (such as
// sleeping for a given duration, but could be some reparatory work in
// response to particular errors) and the retry will be scheduled once
// the retryDelay() operation completes successfully.
template <typename Func, typename RetryDelayFunc>
auto retryWhen(Func func, RetryDelayFunc retryDelay)
    -> Task<semi_await_result_t<invoke_result_t<Func&>>> {
  while (true) {
    exception_wrapper error;
    try {
      auto result = co_await folly::coro::co_awaitTry(func());
      if (result.hasValue()) {
        co_return std::move(result).value();
      } else {
        assert(result.hasException());
        error = std::move(result.exception());
      }
    } catch (const std::exception& e) {
      error = exception_wrapper(std::current_exception(), e);
    } catch (...) {
      error = exception_wrapper(std::current_exception());
    }

    if (error.is_compatible_with<folly::OperationCancelled>()) {
      co_yield folly::coro::co_error(std::move(error));
    }

    Try<void> retryResult =
        co_await folly::coro::co_awaitTry(retryDelay(std::move(error)));
    if (retryResult.hasException()) {
      // Failure (or cancellation) of retryDelay() indicates we should stop
      // retrying.
      co_yield folly::coro::co_error(std::move(retryResult.exception()));
    }

    // Otherwise we go around the loop again.
  }
}

namespace detail {

class RetryImmediatelyWithLimit {
 public:
  explicit RetryImmediatelyWithLimit(uint32_t maxRetries) noexcept
      : retriesRemaining_(maxRetries) {}

  Task<void> operator()(exception_wrapper&& ew) & {
    if (retriesRemaining_ == 0) {
      co_yield folly::coro::co_error(std::move(ew));
    }

    const auto& cancelToken = co_await co_current_cancellation_token;
    if (cancelToken.isCancellationRequested()) {
      co_yield folly::coro::co_error(OperationCancelled{});
    }

    --retriesRemaining_;
  }

 private:
  uint32_t retriesRemaining_;
};

} // namespace detail

// Executes the operation returned by func(), retrying it up to
// 'maxRetries' times on failure with no delay between retries.
template <typename Func>
auto retryN(uint32_t maxRetries, Func&& func) {
  return folly::coro::retryWhen(
      static_cast<Func&&>(func), detail::RetryImmediatelyWithLimit{maxRetries});
}

namespace detail {

template <typename URNG>
class ExponentialBackoffWithJitter {
 public:
  template <typename URNG2>
  explicit ExponentialBackoffWithJitter(
      uint32_t maxRetries,
      Duration minBackoff,
      Duration maxBackoff,
      double relativeJitterStdDev,
      URNG2&& rng) noexcept
      : maxRetries_(maxRetries),
        retryCount_(0),
        minBackoff_(minBackoff),
        maxBackoff_(maxBackoff),
        relativeJitterStdDev_(relativeJitterStdDev),
        randomGen_(static_cast<URNG2&&>(rng)) {}

  Task<void> operator()(exception_wrapper&& ew) & {
    if (retryCount_ == maxRetries_) {
      co_yield folly::coro::co_error(std::move(ew));
    }

    ++retryCount_;

    auto dist = std::normal_distribution<double>(0.0, relativeJitterStdDev_);

    // The jitter will be a value between [e^-stdev]
    auto jitter = std::exp(dist(randomGen_));
    auto backoffRep =
        jitter * minBackoff_.count() * std::pow(2, retryCount_ - 1u);

    Duration backoff;
    if (backoffRep >= std::numeric_limits<Duration::rep>::max()) {
      backoff = maxBackoff_;
    } else {
      backoff = std::min(Duration(Duration::rep(backoffRep)), maxBackoff_);
    }
    backoff = std::max(backoff, minBackoff_);

    co_await folly::coro::sleep(backoff);

    // Check to see if we were cancelled during the sleep.
    const auto& cancelToken = co_await co_current_cancellation_token;
    if (cancelToken.isCancellationRequested()) {
      co_yield folly::coro::co_error(OperationCancelled{});
    }
  }

 private:
  const uint32_t maxRetries_;
  uint32_t retryCount_;
  const Duration minBackoff_;
  const Duration maxBackoff_;
  const double relativeJitterStdDev_;
  URNG randomGen_;
};

} // namespace detail

// Executes the operation returned from 'func()', retrying it on failure
// up to 'maxRetries' times, with an exponential backoff, doubling the backoff
// on average for each retry, applying some random jitter, up to the specified
// maximum backoff.
template <typename Func, typename URNG>
auto retryWithExponentialBackoff(
    uint32_t maxRetries,
    Duration minBackoff,
    Duration maxBackoff,
    double relativeJitterStdDev,
    URNG&& rng,
    Func&& func) {
  return folly::coro::retryWhen(
      static_cast<Func&&>(func),
      detail::ExponentialBackoffWithJitter<remove_cvref_t<URNG>>{
          maxRetries,
          minBackoff,
          maxBackoff,
          relativeJitterStdDev,
          static_cast<URNG&&>(rng)});
}

template <typename Func>
auto retryWithExponentialBackoff(
    uint32_t maxRetries,
    Duration minBackoff,
    Duration maxBackoff,
    double relativeJitterStdDev,
    Func&& func) {
  return folly::coro::retryWithExponentialBackoff(
      maxRetries,
      minBackoff,
      maxBackoff,
      relativeJitterStdDev,
      ThreadLocalPRNG(),
      static_cast<Func&&>(func));
}

} // namespace folly::coro
