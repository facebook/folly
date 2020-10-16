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

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

#include <fmt/chrono.h>
#include <fmt/core.h>
#include <glog/logging.h>

#include <folly/DefaultKeepAliveExecutor.h>
#include <folly/Random.h>
#include <folly/Synchronized.h>
#include <folly/executors/GlobalExecutor.h>
#include <folly/experimental/observer/Observable.h>
#include <folly/experimental/observer/Observer.h>
#include <folly/futures/Future.h>

namespace folly {
namespace observer {

template <typename T>
Observer<T> withJitter(
    Observer<T> observer,
    std::chrono::milliseconds lag,
    std::chrono::milliseconds jitter) {
  class WithJitterObservable {
   public:
    using element_type = T;

    WithJitterObservable(
        Observer<T> observer,
        std::chrono::milliseconds lag,
        std::chrono::milliseconds jitter)
        : observer_(std::move(observer)),
          state_(std::make_shared<Synchronized<State, std::mutex>>(
              State(observer_.getSnapshot().getShared()))),
          lag_(lag),
          jitter_(jitter) {}

    std::shared_ptr<const T> get() { return state_->lock()->laggingValue; }

    void subscribe(std::function<void()> callback) {
      handle_ = observer_.addCallback([state = state_,
                                       observer = observer_,
                                       callback = std::move(callback),
                                       lag = lag_,
                                       jitter = jitter_](auto /* snapshot */) {
        if (std::exchange(state->lock()->delayedRefreshPending, true)) {
          return;
        }

        const auto sleepFor = lag - jitter +
            std::chrono::milliseconds{Random::rand64(2 * jitter.count())};

        auto* executor = dynamic_cast<DefaultKeepAliveExecutor*>(
            getGlobalCPUExecutor().get());
        CHECK(executor);
        futures::sleep(sleepFor)
            .via(executor->weakRef())
            .thenValue([callback, observer, state](auto&&) mutable {
              state->withLock([&](auto& s) {
                s.laggingValue = observer.getSnapshot().getShared();
                s.delayedRefreshPending = false;
              });
              callback();
            });
      });
    }

    void unsubscribe() { handle_.cancel(); }

   private:
    struct State {
      explicit State(std::shared_ptr<const T> value)
          : laggingValue(std::move(value)) {}

      std::shared_ptr<const T> laggingValue;
      bool delayedRefreshPending{false};
    };

    Observer<T> observer_;
    CallbackHandle handle_;
    std::shared_ptr<Synchronized<State, std::mutex>> state_;
    const std::chrono::milliseconds lag_;
    const std::chrono::milliseconds jitter_;
  };

  if (lag == std::chrono::milliseconds::zero()) {
    return observer;
  }
  if (jitter > lag) {
    throw std::invalid_argument(
        fmt::format("lag ({}) cannot be less than jitter ({})", lag, jitter));
  }
  return ObserverCreator<WithJitterObservable>(std::move(observer), lag, jitter)
      .getObserver();
}

} // namespace observer
} // namespace folly
