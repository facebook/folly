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
#include <memory>

#include <folly/Function.h>
#include <folly/Range.h>
#include <folly/Synchronized.h>

namespace folly::detail {

/**
 * EventBasePoller centralizes the blocking wait for events across multiple
 * EventBases in a process. The singleton calls the provided ReadyCallback on
 * ready EventBases, so they can be driven without blocking. This enables
 * control over which threads drive the EventBases, as opposed to the standard
 * blocking loop that requires one thread per EventBase.
 *
 * EventBases' pollable fds are registered in groups, so that the callback can
 * batch processing of ready EventBases that belong to the same group.
 *
 * When the EventBase is ready it can be driven until it would block again, and
 * then handoff() must be called to resume polling the fd. Neither the driving
 * of the EventBase or the call to handoff() should happen inline in the
 * callback, but delegated to another thread without blocking; the callback must
 * return control quickly, as it executes in the main polling loop and can slow
 * down the handling of all other registered EventBases.
 *
 * Note that none of the implementation is specific to EventBases, in fact this
 * is a lightweight implementation of an event loop specialized on polling read
 * events, and which supports grouping of the fds for batch-handling. The class
 * could be easily generalized if other applications arise.
 */
class EventBasePoller {
 public:
  struct Stats {
    using Duration = std::chrono::steady_clock::duration;

    // Track number of loop wake-ups and number of events returned.
    int minNumEvents{std::numeric_limits<int>::max()};
    int maxNumEvents{std::numeric_limits<int>::min()};
    size_t totalNumEvents{0};
    size_t totalWakeups{0};

    Duration totalWait{0};
    Duration minWait{Duration::max()};
    Duration maxWait{Duration::min()};

    Duration totalBusy{0};
    Duration minBusy{Duration::max()};
    Duration maxBusy{Duration::min()};

    void update(int numEvents, Duration wait, Duration busy);
  };

  class Handle {
   public:
    virtual ~Handle();

    template <class T>
    T* getUserData() const {
      return reinterpret_cast<T*>(userData_);
    }

    // If done is set to true, the handle is not re-armed and can be reclaimed
    // with reclaim().
    virtual void handoff(bool done) = 0;

   protected:
    friend class EventBasePoller;

    explicit Handle(void* userData) : userData_(userData) {}

    void* userData_;
  };

  // FdGroup method invocations must be serialized.
  class FdGroup {
   public:
    virtual ~FdGroup();

    // All added handles must be reclaimed before the group is destroyed.
    virtual std::unique_ptr<Handle> add(int fd, void* userData) = 0;
    // Blocks until handoff(true) is called on the handle.
    virtual void reclaim(std::unique_ptr<Handle> handle) = 0;
  };

  using ReadyCallback =
      Function<void(Range<Handle**> readyHandles) const noexcept>;

  static EventBasePoller& get();

  virtual ~EventBasePoller();

  virtual std::unique_ptr<FdGroup> makeFdGroup(ReadyCallback readyCallback) = 0;

  Stats getStats() { return stats_.copy(); }

 protected:
  folly::Synchronized<Stats> stats_;
};

} // namespace folly::detail
