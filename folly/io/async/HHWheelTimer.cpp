/*
 * Copyright 2015 Facebook, Inc.
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements. See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership. The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
#include <folly/io/async/HHWheelTimer.h>
#include <folly/io/async/Request.h>

#include <folly/ScopeGuard.h>

#include <cassert>

using std::chrono::milliseconds;

namespace folly {

/**
 * We want to select the default interval carefully.
 * An interval of 10ms will give us 10ms * WHEEL_SIZE^WHEEL_BUCKETS
 * for the largest timeout possible, or about 497 days.
 *
 * For a lower bound, we want a reasonable limit on local IO, 10ms
 * seems short enough
 *
 * A shorter interval also has CPU implications, less than 1ms might
 * start showing up in cpu perf.  Also, it might not be possible to set
 * tick interval less than 10ms on older kernels.
 */
int HHWheelTimer::DEFAULT_TICK_INTERVAL = 10;

HHWheelTimer::Callback::~Callback() {
  if (isScheduled()) {
    cancelTimeout();
  }
}

void HHWheelTimer::Callback::setScheduled(HHWheelTimer* wheel,
                                          std::chrono::milliseconds timeout) {
  assert(wheel_ == nullptr);
  assert(expiration_ == milliseconds(0));

  wheel_ = wheel;

  // Only update the now_ time if we're not in a timeout expired callback
  if (wheel_->count_  == 0 && !wheel_->processingCallbacksGuard_) {
    wheel_->now_ = getCurTime();
  }

  expiration_ = wheel_->now_ + timeout;
}

void HHWheelTimer::Callback::cancelTimeoutImpl() {
  if (--wheel_->count_ <= 0) {
    assert(wheel_->count_ == 0);
    wheel_->AsyncTimeout::cancelTimeout();
  }
  hook_.unlink();

  wheel_ = nullptr;
  expiration_ = milliseconds(0);
}

HHWheelTimer::HHWheelTimer(folly::EventBase* eventBase,
                           std::chrono::milliseconds intervalMS)
  : AsyncTimeout(eventBase)
  , interval_(intervalMS)
  , nextTick_(1)
  , count_(0)
  , catchupEveryN_(DEFAULT_CATCHUP_EVERY_N)
  , expirationsSinceCatchup_(0)
  , processingCallbacksGuard_(false)
{
}

HHWheelTimer::~HHWheelTimer() {
}

void HHWheelTimer::destroy() {
  assert(count_ == 0);
  DelayedDestruction::destroy();
}

void HHWheelTimer::scheduleTimeoutImpl(Callback* callback,
                                       std::chrono::milliseconds timeout) {
  int64_t due = timeToWheelTicks(timeout) + nextTick_;
  int64_t diff = due - nextTick_;
  CallbackList* list;

  if (diff < 0) {
    list = &buckets_[0][nextTick_ & WHEEL_MASK];
  } else if (diff < WHEEL_SIZE) {
    list = &buckets_[0][due & WHEEL_MASK];
  } else if (diff < 1 << (2 * WHEEL_BITS)) {
    list = &buckets_[1][(due >> WHEEL_BITS) & WHEEL_MASK];
  } else if (diff < 1 << (3 * WHEEL_BITS)) {
    list = &buckets_[2][(due >> 2 * WHEEL_BITS) & WHEEL_MASK];
  } else {
    /* in largest slot */
    if (diff > LARGEST_SLOT) {
      diff = LARGEST_SLOT;
      due = diff + nextTick_;
    }
    list = &buckets_[3][(due >> 3 * WHEEL_BITS) & WHEEL_MASK];
  }
  list->push_back(*callback);
}

void HHWheelTimer::scheduleTimeout(Callback* callback,
                                   std::chrono::milliseconds timeout) {
  // Cancel the callback if it happens to be scheduled already.
  callback->cancelTimeout();

  callback->context_ = RequestContext::saveContext();

  if (count_ == 0 && !processingCallbacksGuard_) {
    this->AsyncTimeout::scheduleTimeout(interval_.count());
  }

  callback->setScheduled(this, timeout);
  scheduleTimeoutImpl(callback, timeout);
  count_++;
}

bool HHWheelTimer::cascadeTimers(int bucket, int tick) {
  CallbackList cbs;
  cbs.swap(buckets_[bucket][tick]);
  while (!cbs.empty()) {
    auto* cb = &cbs.front();
    cbs.pop_front();
    scheduleTimeoutImpl(cb, cb->getTimeRemaining(now_));
  }

  // If tick is zero, timeoutExpired will cascade the next bucket.
  return tick == 0;
}

void HHWheelTimer::timeoutExpired() noexcept {
  // If destroy() is called inside timeoutExpired(), delay actual destruction
  // until timeoutExpired() returns
  DestructorGuard dg(this);
  // If scheduleTimeout is called from a callback in this function, it may
  // cause inconsistencies in the state of this object. As such, we need
  // to treat these calls slightly differently.
  processingCallbacksGuard_ = true;
  auto reEntryGuard = folly::makeGuard([&] {
    processingCallbacksGuard_ = false;
  });

  // timeoutExpired() can only be invoked directly from the event base loop.
  // It should never be invoked recursively.
  //
  milliseconds catchup = now_ + interval_;
  // If catchup is enabled, we may have missed multiple intervals, use
  // currentTime() to check exactly.
  if (++expirationsSinceCatchup_ >= catchupEveryN_) {
    catchup = std::chrono::duration_cast<milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch());
    expirationsSinceCatchup_ = 0;
  }
  while (now_ < catchup) {
    now_ += interval_;

    int idx = nextTick_ & WHEEL_MASK;
    if (0 == idx) {
      // Cascade timers
      if (cascadeTimers(1, (nextTick_ >> WHEEL_BITS) & WHEEL_MASK) &&
          cascadeTimers(2, (nextTick_ >> (2 * WHEEL_BITS)) & WHEEL_MASK)) {
        cascadeTimers(3, (nextTick_ >> (3 * WHEEL_BITS)) & WHEEL_MASK);
      }
    }

    nextTick_++;
    CallbackList* cbs = &buckets_[0][idx];
    while (!cbs->empty()) {
      auto* cb = &cbs->front();
      cbs->pop_front();
      count_--;
      cb->wheel_ = nullptr;
      cb->expiration_ = milliseconds(0);
      auto old_ctx =
        RequestContext::setContext(cb->context_);
      cb->timeoutExpired();
      RequestContext::setContext(old_ctx);
    }
  }
  if (count_ > 0) {
    this->AsyncTimeout::scheduleTimeout(interval_.count());
  }
}

size_t HHWheelTimer::cancelAll() {
  decltype(buckets_) buckets;

// Work around std::swap() bug in libc++
//
// http://llvm.org/bugs/show_bug.cgi?id=22106
#if FOLLY_USE_LIBCPP
  for (size_t i = 0; i < WHEEL_BUCKETS; ++i) {
    for (size_t ii = 0; i < WHEEL_SIZE; ++ii) {
      std::swap(buckets_[i][ii], buckets[i][ii]);
    }
  }
#else
  std::swap(buckets, buckets_);
#endif

  size_t count = 0;

  for (auto& tick : buckets) {
    for (auto& bucket : tick) {
      while (!bucket.empty()) {
        auto& cb = bucket.front();
        cb.cancelTimeout();
        cb.callbackCanceled();
        count++;
      }
    }
  }

  return count;
}

} // folly
