/*
 * Copyright 2014 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <boost/noncopyable.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>

namespace folly { namespace wangle {
  /// An Executor accepts units of work with add(), which should be
  /// threadsafe.
  /// Like an Rx Scheduler. We should probably rename it to match now that it
  /// has scheduling semantics too, but that's a codemod for another lazy
  /// summer afternoon.
  class Executor : boost::noncopyable {
   public:
     typedef std::function<void()> Action;
     // Reality is that better than millisecond resolution is very hard to
     // achieve. However, we reserve the right to be incredible.
     typedef std::chrono::microseconds Duration;
     typedef std::chrono::steady_clock::time_point TimePoint;

     virtual ~Executor() = default;

     /// Enqueue an action to be performed by this executor. This and all
     /// schedule variants must be threadsafe.
     virtual void add(Action&&) = 0;

     /// A convenience function for shared_ptr to legacy functors.
     ///
     /// Sometimes you have a functor that is move-only, and therefore can't be
     /// converted to a std::function (e.g. std::packaged_task). In that case,
     /// wrap it in a shared_ptr (or maybe folly::MoveWrapper) and use this.
     template <class P>
     void addPtr(P fn) {
       this->add([fn]() mutable { (*fn)(); });
     }

     /// Alias for add() (for Rx consistency)
     void schedule(Action&& a) { add(std::move(a)); }

     /// Schedule an action to be executed after dur time has elapsed
     /// Expect millisecond resolution at best.
     void schedule(Action&& a, Duration const& dur) {
       scheduleAt(std::move(a), now() + dur);
     }

     /// Schedule an action to be executed at time t, or as soon afterward as
     /// possible. Expect millisecond resolution at best. Must be threadsafe.
     virtual void scheduleAt(Action&& a, TimePoint const& t) {
       throw std::logic_error("unimplemented");
     }

     /// Get this executor's notion of time. Must be threadsafe.
     virtual TimePoint now() {
       return std::chrono::steady_clock::now();
     }
  };
}}
