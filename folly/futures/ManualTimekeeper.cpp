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

ManualTimekeeper::ManualTimekeeper()
    : now_{std::chrono::steady_clock::now()},
      schedule_{std::make_shared<decltype(schedule_)::element_type>()} {}

SemiFuture<Unit> ManualTimekeeper::after(HighResDuration dur) {
  auto contract = folly::makePromiseContract<Unit>();
  if (dur.count() == 0) {
    contract.first.setValue(folly::unit);
  } else {
    auto key = MapKey::get(now_ + dur);
    contract.first.setInterruptHandler(
        [weakSchedule = std::weak_ptr(schedule_), key](exception_wrapper ew) {
          if (auto lockedSchedule = weakSchedule.lock()) {
            auto schedule = lockedSchedule->wlock();
            auto it = schedule->find(key);
            if (it != schedule->end()) {
              it->second.setException(std::move(ew));
              schedule->erase(it);
            }
          }
        });
    schedule_->withWLock([&contract, &key](auto& schedule) {
      schedule.try_emplace(key, std::move(contract.first));
    });
  }
  return std::move(contract.second);
}

void ManualTimekeeper::advance(Duration dur) {
  now_ += dur;
  auto mapKey = MapKey::get(now_);
  schedule_->withWLock([&mapKey](auto& schedule) {
    auto start = schedule.begin();
    auto end = schedule.upper_bound(mapKey);
    for (auto iter = start; iter != end; iter++) {
      iter->second.setValue(folly::unit);
    }
    schedule.erase(start, end);
  });
}

std::chrono::steady_clock::time_point ManualTimekeeper::now() const {
  return now_;
}

std::size_t ManualTimekeeper::numScheduled() const {
  return schedule_->withRLock([](const auto& sched) { return sched.size(); });
}

/* static */ ManualTimekeeper::MapKey ManualTimekeeper::MapKey::get(
    std::chrono::steady_clock::time_point time) {
  static std::atomic_uint64_t nextId{0};
  return MapKey{time, nextId++};
}

} // namespace folly
