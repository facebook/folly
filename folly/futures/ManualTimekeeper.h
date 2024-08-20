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

#include <atomic>
#include <chrono>
#include <map>
#include <memory>

#include <folly/Synchronized.h>
#include <folly/futures/Future.h>
#include <folly/futures/Promise.h>

namespace folly {

// Manually controlled Timekeeper for unit testing.
//
// We assume advance(), now(), and numScheduled() are called from only a single
// thread, while after() can safely be called from multiple threads.
class ManualTimekeeper : public folly::Timekeeper {
 public:
  explicit ManualTimekeeper();

  /// The returned future is completed when someone calls advance and pushes the
  /// executor's clock to a value greater than or equal to (now() + dur)
  SemiFuture<Unit> after(folly::HighResDuration dur) override;

  /// Advance the timekeeper's clock to (now() + dur).  All futures with target
  /// time points less than or equal to (now() + dur) are fulfilled after the
  /// call to advance() returns
  void advance(folly::Duration dur);

  /// Returns the current clock value in the timekeeper.  This is advanced only
  /// when someone calls advance()
  std::chrono::steady_clock::time_point now() const;

  /// Returns the number of futures that are pending and have not yet been
  /// fulfilled
  std::size_t numScheduled() const;

 private:
  class TimeoutHandler {
   public:
    explicit TimeoutHandler(Promise<Unit>&& promise);

    static std::shared_ptr<TimeoutHandler> create(Promise<Unit>&& promise);

    void trySetTimeout();
    void trySetException(exception_wrapper&& ex);

   private:
    std::atomic_bool fulfilled_{false};
    Promise<Unit> promise_;

    bool canSet();
  };

  std::atomic<std::chrono::steady_clock::time_point> now_;
  folly::Synchronized<std::multimap<
      std::chrono::steady_clock::time_point,
      std::shared_ptr<TimeoutHandler>>>
      schedule_;
};

} // namespace folly
