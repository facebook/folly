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

#include <atomic>
#include <mutex>
#include <folly/SmallLocks.h>

namespace folly { namespace wangle { namespace detail {

/// Finite State Machine helper base class.
/// Inherit from this.
/// For best results, use an "enum class" for Enum.
template <class Enum>
class FSM {
private:
  // I am not templatizing this because folly::MicroSpinLock needs to be
  // zero-initialized (or call init) which isn't generic enough for something
  // that behaves like std::mutex. :(
  using Mutex = folly::MicroSpinLock;
  Mutex mutex_ {0};

  // This might not be necessary for all Enum types, e.g. anything
  // that is atomically updated in practice on this CPU and there's no risk
  // of returning a bogus state because of tearing.
  // An optimization would be to use a static conditional on the Enum type.
  std::atomic<Enum> state_;

public:
  FSM(Enum startState) : state_(startState) {}

  Enum getState() const {
    return state_.load(std::memory_order_relaxed);
  }

  // transition from state A to state B, and then perform action while the
  // lock is still held.
  //
  // If the current state is not A, returns false.
  template <class F>
  bool updateState(Enum A, Enum B, F const& action) {
    std::lock_guard<Mutex> lock(mutex_);
    if (state_ != A) return false;
    state_ = B;
    action();
    return true;
  }
};

#define FSM_START \
  retry: \
    switch (getState()) {

#define FSM_UPDATE2(a, b, action, unlocked_code) \
    case a: \
      if (!updateState((a), (b), (action))) goto retry; \
      { unlocked_code ; } \
      break;

#define FSM_UPDATE(a, b, action) FSM_UPDATE2((a), (b), (action), {})

#define FSM_END \
    }


}}}
