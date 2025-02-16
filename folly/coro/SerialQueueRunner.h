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
#include <mutex>
#include <queue>
#include <tuple>
#include <vector>

#include <folly/ExceptionWrapper.h>
#include <folly/coro/Baton.h>
#include <folly/coro/Task.h>

#if FOLLY_HAS_COROUTINES

namespace folly::coro {

/// SerialQueueRunner
///
/// Runs coroutine work items submitted via add() in sequence, i.e. with no
/// overlapping.
///
/// Different from scheduling via SequencedExecutor, which runs the *parts* of
/// the work items between resume- and suspend-points in sequence, but which
/// overlaps work items.
class SerialQueueRunner {
 private:
  using Work = Task<>;

 public:
  void add(Work task); // task must not throw when awaited!
  void done();

  Task<> run();

 private:
  using PullResult = std::pair<bool, std::vector<Work>>;

  struct Mutex : std::mutex {
    // to suppress lint advice about holding lock objects alive across co_await
    using folly_coro_aware_mutex = void;
  };

  void cancel();
  Task<> await();
  Task<PullResult> pull();

  Mutex mut_{};
  Baton* baton_{};
  std::vector<Work> tasks_{};
  bool done_{};
  std::atomic<bool> running_{};
  exception_wrapper exn_{};
};

} // namespace folly::coro

#endif
