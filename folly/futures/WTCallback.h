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

#include <future>

#include <folly/Chrono.h>
#include <folly/futures/Future.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/HHWheelTimer.h>

namespace folly {
// Our Callback object for HHWheelTimer
template <class TBase>
struct WTCallback : public TBase::Callback {
  struct PrivateConstructorTag {};

 public:
  WTCallback(PrivateConstructorTag, EventBase* base) : base_(base) {}

  // Only allow creation by this factory, to ensure heap allocation.
  static std::shared_ptr<WTCallback> create(EventBase* base) {
    // optimization opportunity: memory pool
    auto cob = std::make_shared<WTCallback>(PrivateConstructorTag{}, base);
    // Capture shared_ptr of cob in lambda so that Core inside Promise will
    // hold a ref count to it. The ref count will be released when Core goes
    // away which happens when both Promise and Future go away
    cob->promise_.setInterruptHandler([cob](exception_wrapper ew) mutable {
      interruptHandler(std::move(cob), std::move(ew));
    });
    return cob;
  }

  SemiFuture<Unit> getSemiFuture() { return promise_.getSemiFuture(); }

  FOLLY_NODISCARD Promise<Unit> stealPromise() {
    // Don't need promise anymore. Break the circular reference as promise_
    // is holding a ref count to us via Core. Core won't go away until both
    // Promise and Future go away.
    return std::move(promise_);
  }

 protected:
  folly::Synchronized<EventBase*> base_;
  Promise<Unit> promise_;

  void timeoutExpired() noexcept override {
    base_ = nullptr;
    // Don't need Promise anymore, break the circular reference
    auto promise = stealPromise();
    if (!promise.isFulfilled()) {
      promise.setValue();
    }
  }

  void callbackCanceled() noexcept override {
    base_ = nullptr;
    // Don't need Promise anymore, break the circular reference
    auto promise = stealPromise();
    if (!promise.isFulfilled()) {
      promise.setException(FutureNoTimekeeper{});
    }
  }

  static void interruptHandler(
      std::shared_ptr<WTCallback> self, exception_wrapper ew) {
    auto rBase = self->base_.rlock();
    if (!*rBase) {
      return;
    }

    (*rBase)->runInEventBaseThreadAlwaysEnqueue(
        [self, ew = std::move(ew)]() mutable {
          self->cancelTimeout();
          // Don't need Promise anymore, break the circular reference
          auto promise = self->stealPromise();
          if (!promise.isFulfilled()) {
            promise.setException(std::move(ew));
          }
        });
  }
};

} // namespace folly
