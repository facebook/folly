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

#include <chrono>
#include <thread>

#include <folly/futures/Future.h>

namespace folly {

/**
 * A Timekeeper with a dedicated thread that manages the timeouts using a
 * heap. Timeouts can be scheduled with microsecond resolution, though in
 * practice the accuracy depends on the OS scheduler's ability to wake up the
 * worker thread in a timely fashion.
 */
class HeapTimekeeper : public Timekeeper {
 public:
  HeapTimekeeper();
  ~HeapTimekeeper() override;

  SemiFuture<Unit> after(HighResDuration) override;

 private:
  using Clock = std::chrono::steady_clock;

  class Timeout;
  class State;

  // Shared with the futures, so that they can survive the timekeeper.
  std::shared_ptr<State> state_;
  std::thread thread_;
};

} // namespace folly
