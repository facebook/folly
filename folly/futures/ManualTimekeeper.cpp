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

#include <folly/futures/ManualTimekeeper.h>

namespace folly {

ManualTimekeeper::ManualTimekeeper() : now_{std::chrono::steady_clock::now()} {}

SemiFuture<Unit> ManualTimekeeper::after(HighResDuration dur) {
  auto contract = folly::makePromiseContract<Unit>();
  if (dur.count() == 0) {
    contract.first.setValue(folly::unit);
  } else {
    auto handler = TimeoutHandler::create(std::move(contract.first));
    schedule_.withWLock([&handler, key = now_ + dur](auto& schedule) {
      schedule.emplace(key, std::move(handler));
    });
  }
  return std::move(contract.second);
}

void ManualTimekeeper::advance(Duration dur) {
  now_ += dur;
  schedule_.withWLock([this](auto& schedule) {
    auto start = schedule.begin();
    auto end = schedule.upper_bound(now_);
    for (auto iter = start; iter != end; iter++) {
      iter->second->trySetTimeout();
    }
    schedule.erase(start, end);
  });
}

std::chrono::steady_clock::time_point ManualTimekeeper::now() const {
  return now_;
}

std::size_t ManualTimekeeper::numScheduled() const {
  return schedule_.withRLock([](const auto& sched) { return sched.size(); });
}

/* static */ std::shared_ptr<ManualTimekeeper::TimeoutHandler>
ManualTimekeeper::TimeoutHandler::create(Promise<Unit>&& promise) {
  auto handler = std::make_shared<TimeoutHandler>(std::move(promise));
  handler->promise_.setInterruptHandler(
      [handler = std::weak_ptr(handler)](exception_wrapper ew) {
        if (auto localTimeout = handler.lock()) {
          localTimeout->trySetException(std::move(ew));
        }
      });
  return handler;
}

ManualTimekeeper::TimeoutHandler::TimeoutHandler(Promise<Unit>&& promise)
    : promise_(std::move(promise)) {}

void ManualTimekeeper::TimeoutHandler::trySetTimeout() {
  if (canSet()) {
    promise_.setValue(unit);
  }
}
void ManualTimekeeper::TimeoutHandler::trySetException(exception_wrapper&& ex) {
  if (canSet()) {
    promise_.setException(std::move(ex));
  }
}

bool ManualTimekeeper::TimeoutHandler::canSet() {
  bool expected = false;
  return fulfilled_.compare_exchange_strong(expected, /* desired */ true);
}

} // namespace folly
