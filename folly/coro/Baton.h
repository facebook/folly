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

#include <folly/experimental/coro/Coroutine.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

/// A baton is a synchronisation primitive for coroutines that allows a
/// coroutine to co_await the baton and suspend until the baton is posted by
/// some thread via a call to .post().
///
/// This primitive is typically used in the construction of larger library types
/// rather than directly in user code.
///
/// As a primitive, this is not cancellation-aware.
///
/// The Baton supports being awaited by multiple coroutines at a time. If the
/// baton is not ready at the time it is awaited then an awaiting coroutine
/// suspends. All suspended coroutines waiting for the baton to be posted will
/// be resumed when some thread next calls .post().
///
/// Example usage:
///
///   folly::coro::Baton baton;
///   std::string sharedValue;
///
///   folly::coro::Task<void> consumer()
///   {
///     // Wait until the baton is posted.
///     co_await baton;
///
///     // Now safe to read shared state.
///     std::cout << sharedValue << std::cout;
///   }
///
///   void producer()
///   {
///     // Write to shared state
///     sharedValue = "some result";
///
///     // Publish the value by 'posting' the baton.
///     // This will resume the consumer if it was currently suspended.
///     baton.post();
///   }
class Baton {
 public:
  class WaitOperation;

  /// Initialise the Baton to either the signalled or non-signalled state.
  explicit Baton(bool initiallySignalled = false) noexcept;

  ~Baton();

  /// Query whether the Baton is currently in the signalled state.
  bool ready() const noexcept;

  /// Asynchronously wait for the Baton to enter the signalled state.
  ///
  /// The returned object must be co_awaited from a coroutine. If the Baton
  /// is already signalled then the awaiting coroutine will continue without
  /// suspending. Otherwise, if the Baton is not yet signalled then the
  /// awaiting coroutine will suspend execution and will be resumed when some
  /// thread later calls post().
  [[nodiscard]] WaitOperation operator co_await() const noexcept;

  /// Set the Baton to the signalled state if it is not already signalled.
  ///
  /// This will resume any coroutines that are currently suspended waiting
  /// for the Baton inside 'co_await baton'.
  void post() noexcept;

  /// Atomically reset the baton back to the non-signalled state.
  ///
  /// This is a no-op if the baton was already in the non-signalled state.
  void reset() noexcept;

  class WaitOperation {
   public:
    explicit WaitOperation(const Baton& baton) noexcept : baton_(baton) {}

    bool await_ready() const noexcept { return baton_.ready(); }

    bool await_suspend(coroutine_handle<> awaitingCoroutine) noexcept {
      awaitingCoroutine_ = awaitingCoroutine;
      return baton_.waitImpl(this);
    }

    void await_resume() noexcept {}

   protected:
    friend class Baton;

    const Baton& baton_;
    coroutine_handle<> awaitingCoroutine_;
    WaitOperation* next_;
  };

 private:
  // Try to register the awaiter as
  bool waitImpl(WaitOperation* awaiter) const noexcept;

  // this  - Baton is in the signalled/posted state.
  // other - Baton is not signalled/posted and this is a pointer to the head
  //         of a potentially empty linked-list of Awaiter nodes that were
  //         waiting for the baton to become signalled.
  mutable std::atomic<void*> state_;
};

inline Baton::Baton(bool initiallySignalled) noexcept
    : state_(initiallySignalled ? static_cast<void*>(this) : nullptr) {}

inline bool Baton::ready() const noexcept {
  return state_.load(std::memory_order_acquire) ==
      static_cast<const void*>(this);
}

inline Baton::WaitOperation Baton::operator co_await() const noexcept {
  return Baton::WaitOperation{*this};
}

inline void Baton::reset() noexcept {
  // Transition from 'signalled' (ie. 'this') to not-signalled (ie. nullptr).
  void* oldState = this;
  (void)state_.compare_exchange_strong(
      oldState, nullptr, std::memory_order_acq_rel, std::memory_order_relaxed);
}

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
