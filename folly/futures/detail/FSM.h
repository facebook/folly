/*
 * Copyright 2014-present Facebook, Inc.
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

#include <folly/synchronization/MicroSpinLock.h>

namespace folly {
namespace futures {
namespace detail {

/// Finite State Machine helper base class.
/// Inherit from this.
/// For best results, use an "enum class" for Enum.
template <class Enum, class Mutex>
class FSM {
 private:
  Mutex mutex_;

  // This might not be necessary for all Enum types, e.g. anything
  // that is atomically updated in practice on this CPU and there's no risk
  // of returning a bogus state because of tearing.
  // An optimization would be to use a static conditional on the Enum type.
  std::atomic<Enum> state_;

 public:
  explicit FSM(Enum startState) : state_(startState) {}

  Enum getState() const noexcept {
    return state_.load(std::memory_order_acquire);
  }

  /// Atomically do a state transition with accompanying action.
  /// The action will see the old state.
  /// @returns true on success, false and action unexecuted otherwise
  template <class F>
  bool tryUpdateState(Enum A, Enum B, F const& action) {
    std::lock_guard<Mutex> lock(mutex_);
    if (state_.load(std::memory_order_acquire) != A) {
      return false;
    }
    action();
    state_.store(B, std::memory_order_release);
    return true;
  }

  /// Atomically do a state transition with accompanying action. Then do the
  /// unprotected action without holding the lock. If the atomic transition
  /// fails, returns false and neither action was executed.
  ///
  /// This facilitates code like this:
  ///   bool done = false;
  ///   while (!done) {
  ///     switch (getState()) {
  ///     case State::Foo:
  ///       done = tryUpdateState(State::Foo, State::Bar,
  ///           [&]{ /* do protected stuff */ },
  ///           [&]{ /* do unprotected stuff */});
  ///       break;
  ///
  /// Which reads nicer than code like this:
  ///   while (true) {
  ///     switch (getState()) {
  ///     case State::Foo:
  ///       if (!tryUpdateState(State::Foo, State::Bar,
  ///           [&]{ /* do protected stuff */ })) {
  ///         continue;
  ///       }
  ///       /* do unprotected stuff */
  ///       return; // or otherwise break out of the loop
  ///
  /// The protected action will see the old state, and the unprotected action
  /// will see the new state.
  template <class F1, class F2>
  bool tryUpdateState(
      Enum A,
      Enum B,
      F1 const& protectedAction,
      F2 const& unprotectedAction) {
    bool result = tryUpdateState(A, B, protectedAction);
    if (result) {
      unprotectedAction();
    }
    return result;
  }

  template <class F>
  void transition(F f) {
    while (!f(getState())) {
    }
  }
};

} // namespace detail
} // namespace futures
} // namespace folly
