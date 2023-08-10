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
#include <memory>

#include <folly/Synchronized.h>
#include <folly/experimental/observer/Observer.h>
#include <folly/experimental/observer/detail/ObserverManager.h>
#include <folly/synchronization/Hazptr.h>

namespace folly {
namespace observer {

/**
 * HazptrObserver implements a read-optimized Observer which caches an
 * Observer's snapshot and protects access to it using hazptrs. The cached
 * snapshot is kept up to date using a callback which fires when the original
 * observer changes. This implementation incurs an additional allocation
 * on updates making it less suitable for write-heavy workloads.
 *
 * There are 2 main APIs:
 * 1) getSnapshot: Returns a Snapshot containing a const pointer to T and guards
 *    access to it using folly::hazptr_holder. The pointer is only safe to use
 *    while the returned Snapshot object is alive.
 * 2) getLocalSnapshot: Same as getSnapshot but backed by folly::hazptr_local.
 *    This API is ~3ns faster than getSnapshot but is unsafe for the current
 *    thread to construct any other hazptr holder type objects (hazptr_holder,
 *    hazptr_array and other hazptr_local) while the returned snapshot exists.
 *
 * See folly/synchronization/Hazptr.h for more details on hazptrs.
 */
template <typename T, template <typename> class Atom = std::atomic>
class HazptrObserver {
  template <typename Holder>
  struct HazptrSnapshot {
    template <typename State>
    explicit HazptrSnapshot(
        const Atom<State*>& state, hazptr_domain<Atom>& domain)
        : holder_() {
      make(holder_, domain);
      ptr_ = get(holder_).protect(state)->snapshot_.get();
    }

    const T& operator*() const { return *get(); }
    const T* operator->() const { return get(); }
    const T* get() const { return ptr_; }

   private:
    static void make(hazptr_holder<Atom>& holder, hazptr_domain<Atom>& domain) {
      holder = folly::make_hazard_pointer(domain);
    }
    static void make(hazptr_local<1, Atom>&, hazptr_domain<Atom>&) {}
    static hazptr_holder<Atom>& get(hazptr_holder<Atom>& holder) {
      return holder;
    }
    static hazptr_holder<Atom>& get(hazptr_local<1>& holder) {
      return holder[0];
    }

    Holder holder_;
    const T* ptr_;
  };

 public:
  using DefaultSnapshot = HazptrSnapshot<hazptr_holder<Atom>>;
  using LocalSnapshot = HazptrSnapshot<hazptr_local<1>>;

  explicit HazptrObserver(
      Observer<T> observer,
      hazptr_domain<Atom>& domain = default_hazptr_domain<Atom>())
      : domain_{domain},
        observer_(observer),
        updateObserver_(
            makeObserver([o = std::move(observer), alive = alive_, this]() {
              auto snapshot = o.getSnapshot();
              auto oldState = static_cast<State*>(nullptr);
              alive->withRLock([&](auto vAlive) {
                if (vAlive) { // otherwise state_ may be out-of-scope
                  oldState = state_.exchange(
                      new State(snapshot), std::memory_order_acq_rel);
                }
              });
              if (oldState) {
                oldState->retire(domain_);
              }
              return folly::unit;
            })) {}

  // updateObserver_ captures this, so we cannot move it, hence only the copy
  // constructor is defined (moves will fall back to copy).
  HazptrObserver(const HazptrObserver& r)
      : HazptrObserver(r.observer_, r.domain_) {}
  HazptrObserver& operator=(const HazptrObserver& r) {
    if (&r != this) {
      this->~HazptrObserver();
      new (this) HazptrObserver(r);
    }
    return *this;
  }

  ~HazptrObserver() {
    *alive_->wlock() = false;
    auto* state = state_.load(std::memory_order_acquire);
    if (state) {
      state->retire(domain_);
    }
  }

  DefaultSnapshot getSnapshot() const {
    if (FOLLY_UNLIKELY(observer_detail::ObserverManager::inManagerThread())) {
      // Wait for updates
      updateObserver_.getSnapshot();
    }
    return DefaultSnapshot(state_, domain_);
  }

  LocalSnapshot getLocalSnapshot() const {
    if (FOLLY_UNLIKELY(observer_detail::ObserverManager::inManagerThread())) {
      // Wait for updates
      updateObserver_.getSnapshot();
    }
    return LocalSnapshot(state_, domain_);
  }

 private:
  struct State : public hazptr_obj_base<State, Atom> {
    explicit State(Snapshot<T> snapshot) : snapshot_(std::move(snapshot)) {}

    Snapshot<T> snapshot_;
  };

  Atom<State*> state_{nullptr};
  std::shared_ptr<Synchronized<bool>> alive_{
      std::make_shared<Synchronized<bool>>(true)};
  hazptr_domain<Atom>& domain_;
  Observer<T> observer_;
  Observer<folly::Unit> updateObserver_;
};

/**
 * Same as makeObserver(...), but creates HazptrObserver.
 */
template <typename T>
HazptrObserver<T> makeHazptrObserver(Observer<T> observer) {
  return HazptrObserver<T>(std::move(observer));
}

template <typename F>
auto makeHazptrObserver(F&& creator) {
  return makeHazptrObserver(makeObserver(std::forward<F>(creator)));
}

} // namespace observer
} // namespace folly
