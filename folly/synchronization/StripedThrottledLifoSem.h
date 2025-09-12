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

#include <algorithm>
#include <optional>
#include <tuple>

#include <folly/Function.h>
#include <folly/GLog.h>
#include <folly/Unit.h>
#include <folly/container/F14Map.h>
#include <folly/container/small_vector.h>
#include <folly/memory/Malloc.h>
#include <folly/synchronization/RelaxedAtomic.h>
#include <folly/synchronization/ThrottledLifoSem.h>

namespace folly {

/**
 * A striped version of ThrottledLifoSem.
 *
 * post() accepts an arbitrary stripe identifier; waits can consume posts from
 * any stripe, and return the stripe identifier that was passed to the
 * corresponding post().
 *
 * Waits specify a preferred stripe: posts from that stripe will be consumed
 * before posts from other stripes.
 *
 * In other words, this behaves like an array of semaphores where posts are done
 * on a specific entry while waits wait for any semaphore in the array, and
 * return the entry from which the post was consumed.
 *
 * This can be used to implement queues sharded by some notion of stripe, for
 * example based on CPU cache topology.
 *
 * Cross-stripe handoffs are best-effort, but not guaranteed: it is possible to
 * have a stable state where there are both nonzero readers and positive value.
 * This is because of a race in wait functions when trying to steal posts from
 * other slices. For example, consider the situation where there are 2 stripes,
 * both with no posts and no waiters. Then two threads perform concurrent
 * post()/wait():
 *
 * Thread 0                             Thread 1
 * --------                             --------
 * wait(0):
 *   try_wait on all stripes (fails)
 *                                      post(1):
 *                                        try_post on all stripes (fails)
 *                                        post on stripe 1
 *   wait on stripe 0
 *
 * In this case, the waiter on thread 0 will not wake up even though there is an
 * available post on stripe 1, until some other operation unblocks this.
 *
 * This implies that if this semaphore is used to control the queue of an
 * executor, the executor is not technically work-conserving. However, forward
 * progress will happen as long as at least one thread eventually waits on each
 * stripe, and if the executor has a steady submission rate such imbalanced
 * states should only persist for very short time. This can be mitigated
 * unconditionally by subscribing the semaphore to
 * StripedThrottledLifoSemBalancer, which will periodically correct imbalances.
 *
 * StripedThrottledLifoSem optionally supports storing a per-stripe payload that
 * is colocated with the semaphore state of the stripe, which can be useful to
 * ensure that they share the cache line. The payload is initialized with the
 * payloadArgs tuple passed to the constructor.
 */
template <class Payload = Unit>
class StripedThrottledLifoSem {
 public:
  template <class PayloadArgs>
  StripedThrottledLifoSem(
      size_t numStripes,
      const PayloadArgs& payloadArgs,
      const ThrottledLifoSem::Options& options = {})
      : numStripes_(numStripes) {
    CHECK_GT(numStripes, 0);
    stripes_ =
        reinterpret_cast<Stripe*>(checkedMalloc(sizeof(Stripe) * numStripes_));
    for (auto* stripe = stripes_; stripe != stripes_ + numStripes_; ++stripe) {
      new (stripe) Stripe(payloadArgs, options);
    }
  }

  explicit StripedThrottledLifoSem(
      size_t numStripes, const ThrottledLifoSem::Options& options = {})
      : StripedThrottledLifoSem(numStripes, std::tuple{}, options) {}

  ~StripedThrottledLifoSem() {
    uint32_t value = 0;
    uint32_t posts = 0;
    for (auto* stripe = stripes_; stripe != stripes_ + numStripes_; ++stripe) {
      value += stripe->value.load();
      posts += stripe->sem.valueGuess();
      stripe->~Stripe();
    }
    DCHECK_EQ(value, posts);
    sizedFree(stripes_, sizeof(Stripe) * numStripes_);
  }

  // TODO(ott): How to support n > 1? Would need explicit support in
  // ThrottledLifoSem for splitting the post value.
  bool post(size_t stripeIdx) {
    DCHECK_LT(stripeIdx, numStripes_);
    stripes_[stripeIdx].value.fetch_add(1, std::memory_order_release);

    // Post a wake-up to the first stripe that has an available waiter, starting
    // from the local one.
    for (auto tryStripeIdx = stripeIdx;;) {
      if (stripes_[tryStripeIdx].sem.try_post()) {
        return true;
      }
      if (++tryStripeIdx == numStripes_) {
        tryStripeIdx = 0;
      }
      if (tryStripeIdx == stripeIdx) {
        // All stripes are saturated, post to the local one.
        return stripes_[stripeIdx].sem.post();
      }
    }
  }

  std::optional<size_t> try_wait(size_t preferredStripeIdx) {
    // If we can consume a wake-up (from any stripe) we are entitled to one
    // post (from any stripe). In both cases we honor preferredStripeIdx.
    for (auto tryStripeIdx = preferredStripeIdx;;) {
      if (stripes_[tryStripeIdx].sem.try_wait()) {
        break;
      }
      if (++tryStripeIdx == numStripes_) {
        tryStripeIdx = 0;
      }
      if (tryStripeIdx == preferredStripeIdx) {
        // No posts available.
        return std::nullopt;
      }
    }

    return consumePost(preferredStripeIdx);
  }

  size_t wait(size_t preferredStripeIdx, const WaitOptions& opt = {}) {
    auto const deadline = std::chrono::steady_clock::time_point::max();
    auto res = try_wait_until(preferredStripeIdx, deadline, opt);
    FOLLY_SAFE_DCHECK(res, "infinity time has passed");
    return *res;
  }

  template <typename Rep, typename Period>
  std::optional<size_t> try_wait_for(
      size_t preferredStripeIdx,
      const std::chrono::duration<Rep, Period>& timeout,
      const WaitOptions& opt = {}) {
    return try_wait_until(
        preferredStripeIdx, timeout + std::chrono::steady_clock::now(), opt);
  }

  template <typename Clock, typename Duration>
  std::optional<size_t> try_wait_until(
      size_t preferredStripeIdx,
      const std::chrono::time_point<Clock, Duration>& deadline,
      const WaitOptions& opt = {}) {
    // First see if we can get a post from any stripe.
    if (auto res = try_wait(preferredStripeIdx)) {
      return res;
    }

    // No luck. Wait on the local stripe.
    if (!stripes_[preferredStripeIdx].sem.try_wait_until(deadline, opt)) {
      return std::nullopt; // Timed out.
    }
    // We got a wake-up, we're entitled to a post.
    return consumePost(preferredStripeIdx);
  }

  size_t numStripes() const { return numStripes_; }

  uint32_t valueGuess() const {
    uint32_t ret = 0;
    for (auto* stripe = stripes_; stripe != stripes_ + numStripes_; ++stripe) {
      ret += stripe->value.load(std::memory_order_relaxed);
    }
    return ret;
  }

  Payload& payload(size_t stripeIdx) {
    DCHECK_LT(stripeIdx, numStripes_);
    return stripes_[stripeIdx].payload;
  }

  // Run a step of load balancing: if some stripes have pending posts and others
  // have waiters, transfer some posts from the former to the latter. This does
  // not try to achieve perfect balance, it just makes progress towards that,
  // and it returns the number of successful transfers. The method can be called
  // repeatedly until no transfers happen anymore.
  //
  // Note that this doesn't change the value of any stripe: it only affects
  // which waiters are awoken. The waiters will still receive the index of the
  // stripe where the post originally happened.
  size_t balanceStep() {
    // Should be enough for several CPU generations.
    constexpr size_t kInlineSize = 64;
    // Each pair is (excess, stripe index).
    small_vector<std::pair<int64_t, size_t>, kInlineSize> donors;
    small_vector<std::pair<int64_t, size_t>, kInlineSize> recipients;

    for (size_t i = 0; i < numStripes(); ++i) {
      auto excess = stripes_[i].sem.excessValueGuess();
      if (excess > 0) {
        donors.emplace_back(excess, i);
      } else if (excess < 0) {
        recipients.emplace_back(excess, i);
      }
    }
    std::sort(donors.begin(), donors.end(), std::greater<>{});
    std::sort(recipients.begin(), recipients.end());

    size_t transfers = 0;
    for (size_t i = 0; i < std::min(donors.size(), recipients.size()); ++i) {
      auto& from = stripes_[donors[i].second].sem;
      auto& to = stripes_[recipients[i].second].sem;
      if (from.try_wait()) {
        if (to.try_post()) {
          ++transfers;
        } else {
          // to became busy, return the post to from.
          from.post();
        }
      }
    }
    return transfers;
  }

 private:
  StripedThrottledLifoSem(const StripedThrottledLifoSem&) = delete;
  StripedThrottledLifoSem(StripedThrottledLifoSem&&) = delete;
  StripedThrottledLifoSem& operator=(const StripedThrottledLifoSem&) = delete;
  StripedThrottledLifoSem& operator=(StripedThrottledLifoSem&&) = delete;

  size_t consumePost(size_t preferredStripeIdx) {
    // We consumed a wake-up to get here, so we are entitled to one post, but we
    // could be racing with other waiters to get it, so spin until we find one.
    for (auto tryStripeIdx = preferredStripeIdx;;) {
      auto& stripe = stripes_[tryStripeIdx];
      auto value = stripe.value.load(std::memory_order_relaxed);
      if (value > 0) {
        if (stripe.value.compare_exchange_weak(
                value,
                value - 1,
                std::memory_order_acquire,
                std::memory_order_relaxed)) {
          return tryStripeIdx;
        } else {
          continue; // Retry same stripe.
        }
      }
      if (++tryStripeIdx == numStripes_) {
        tryStripeIdx = 0;
      }
    }
  }

  // Each stripe has a value (number of posts to that stripe) and a semaphore; a
  // post will always increase the value for the requested stripe, but may post
  // to a different stripe's semaphore if this ensures we can wake up a waiter
  // in bounded time. Globally, it should always be the case that
  //
  // sum_stripe value >= sum_stripe sem
  //
  // and in absence of concurrent operations this should be an equality.
  struct alignas(cacheline_align_v) Stripe {
    template <class PayloadArgs>
    explicit Stripe(
        const PayloadArgs& payloadArgs,
        const ThrottledLifoSem::Options& options)
        : sem(options), payload(std::make_from_tuple<Payload>(payloadArgs)) {}

    std::atomic<uint32_t> value{0};
    ThrottledLifoSem sem;
    Payload payload;
  };

  const size_t numStripes_;
  Stripe* stripes_;
};

class StripedThrottledLifoSemBalancer {
  struct PrivateTag {};

 public:
  explicit StripedThrottledLifoSemBalancer(PrivateTag) {}

  template <class Payload>
  static void subscribe(StripedThrottledLifoSem<Payload>& sem) {
    ensureWorker();
    auto& self = instance();
    std::lock_guard g(self.mutex_);
    auto [_, inserted] = self.sems_.try_emplace(
        reinterpret_cast<void*>(&sem), [&sem] { return sem.balanceStep(); });
    CHECK(inserted) << "sem already subscribed";
  }

  template <class Payload>
  static void unsubscribe(StripedThrottledLifoSem<Payload>& sem) {
    auto& self = instance();
    std::lock_guard g(self.mutex_);
    auto erased = self.sems_.erase(reinterpret_cast<void*>(&sem));
    CHECK_EQ(erased, 1) << "sem was not subscribed";
  }

  static size_t totalTransfers() { return instance().totalTransfers_.load(); }

 private:
  class Worker;

  static StripedThrottledLifoSemBalancer& instance();

  // The worker thread has separate lifetime from the
  // StripedThrottledLifoSemBalancer singleton: the latter is a leaky singleton
  // so that it can be unconditionally accessed during shutdown, while the
  // thread is joined on shutdown. Also, the thread is only started on the
  // first subscribe() call: just accessing the stats should not start it.
  static void ensureWorker();

  std::mutex mutex_;
  std::condition_variable cv_;
  folly::F14FastMap<void*, folly::Function<int64_t() const>> sems_;

  folly::relaxed_atomic<size_t> totalTransfers_{0};
};

} // namespace folly
