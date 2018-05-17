/*
 * Copyright 2018-present Facebook, Inc.
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

#include <folly/synchronization/Hazptr-fwd.h>

#include <folly/CPortability.h>
#include <folly/Portability.h>

#include <glog/logging.h>

#include <atomic>
#include <memory>

///
/// Classes related to objects protected by hazard pointers.
///

namespace folly {

/**
 *  hazptr_obj
 *
 *  Object protected by hazard pointers.
 */
template <template <typename> class Atom>
class hazptr_obj {
  using ReclaimFnPtr = void (*)(hazptr_obj*);

  ReclaimFnPtr reclaim_;
  hazptr_obj<Atom>* next_;

 public:
  /** Constructors */
  /* All constructors set next_ to this in order to catch misuse bugs
      such as double retire. */

  hazptr_obj() noexcept : next_(this) {}

  hazptr_obj(const hazptr_obj<Atom>&) noexcept : next_(this) {}

  hazptr_obj(hazptr_obj<Atom>&&) noexcept : next_(this) {}

  /** Copy operator */
  hazptr_obj<Atom>& operator=(const hazptr_obj<Atom>&) noexcept {
    return *this;
  }

  /** Move operator */
  hazptr_obj<Atom>& operator=(hazptr_obj<Atom>&&) noexcept {
    return *this;
  }

 private:
  friend class hazptr_domain<Atom>;
  template <typename, template <typename> class, typename>
  friend class hazptr_obj_base;
  template <typename, template <typename> class, typename>
  friend class hazptr_obj_base_refcounted;
  friend class hazptr_priv<Atom>;

  hazptr_obj<Atom>* next() const noexcept {
    return next_;
  }

  void set_next(hazptr_obj* obj) noexcept {
    next_ = obj;
  }

  ReclaimFnPtr reclaim() noexcept {
    return reclaim_;
  }

  const void* raw_ptr() const {
    return this;
  }

  void pre_retire_check() noexcept {
    // Only for catching misuse bugs like double retire
    if (next_ != this) {
      pre_retire_check_fail();
    }
  }

  void do_retire(hazptr_domain<Atom>& domain) {
#if FOLLY_HAZPTR_THR_LOCAL
    if (&domain == &default_hazptr_domain<Atom>()) {
      hazptr_priv_tls<Atom>().push(this);
      return;
    }
#endif
    hazptr_domain_push_retired(this, this, 1, domain);
  }

  FOLLY_NOINLINE void pre_retire_check_fail() noexcept {
    CHECK_EQ(next_, this);
  }
}; // hazptr_obj

/**
 *  hazptr_obj_base
 *
 *  Base template for objects protected by hazard pointers.
 */
template <typename T, template <typename> class Atom, typename D>
class hazptr_obj_base : public hazptr_obj<Atom> {
  D deleter_; // TODO: EBO

 public:
  /* Retire a removed object and pass the responsibility for
   * reclaiming it to the hazptr library */
  void retire(
      D deleter = {},
      hazptr_domain<Atom>& domain = default_hazptr_domain<Atom>()) {
    pre_retire(std::move(deleter));
    set_reclaim();
    this->do_retire(domain); // defined in hazptr_obj
  }

  void retire(hazptr_domain<Atom>& domain) {
    retire({}, domain);
  }

 private:
  void pre_retire(D deleter) {
    this->pre_retire_check(); // defined in hazptr_obj
    deleter_ = std::move(deleter);
  }

  void set_reclaim() {
    this->reclaim_ = [](hazptr_obj<Atom>* p) {
      auto hobp = static_cast<hazptr_obj_base<T, Atom, D>*>(p);
      auto obj = static_cast<T*>(hobp);
      hobp->deleter_(obj);
    };
  }
}; // hazptr_obj_base

/**
 *  hazptr_obj_base_refcounted
 *
 *  Base template for reference counted objects protected by hazard
 *  pointers.
 */
template <typename T, template <typename> class Atom, typename D>
class hazptr_obj_base_refcounted : public hazptr_obj<Atom> {
  Atom<uint32_t> refcount_{0};
  D deleter_;

 public:
  /* Retire a removed object and pass the responsibility for
   * reclaiming it to the hazptr library */
  void retire(
      D deleter = {},
      hazptr_domain<Atom>& domain = default_hazptr_domain<Atom>()) {
    this->pre_retire(std::move(deleter)); // defined in hazptr_obj
    set_reclaim();
    this->do_retire(domain); // defined in hazptr_obj
  }

  void retire(hazptr_domain<Atom>& domain) {
    retire({}, domain);
  }

  /* Increments the reference count. */
  void acquire_ref() noexcept {
    refcount_.fetch_add(1u, std::memory_order_acq_rel);
  }

  /* The same as acquire_ref() except that in addition the caller
   * guarantees that the call is made in a thread-safe context, e.g.,
   * the object is not yet shared. This is just an optimization to
   * save an atomic read-modify-write operation. */
  void acquire_ref_safe() noexcept {
    auto oldval = refcount_.load(std::memory_order_acquire);
    refcount_.store(oldval + 1u, std::memory_order_release);
  }

  /* Decrements the reference count and returns true if the object is
   * safe to reclaim. */
  bool release_ref() noexcept {
    auto oldval = refcount_.load(std::memory_order_acquire);
    if (oldval > 0u) {
      oldval = refcount_.fetch_sub(1u, std::memory_order_acq_rel);
    } else {
      if (kIsDebug) {
        refcount_.store(~0u);
      }
    }
    return oldval == 0;
  }

 private:
  void pre_retire(D deleter) {
    this->pre_retire_check(); // defined in hazptr_obj
    deleter_ = std::move(deleter);
  }

  void set_reclaim() {
    this->reclaim_ = [](hazptr_obj<Atom>* p) {
      auto hrobp = static_cast<hazptr_obj_base_refcounted<T, Atom, D>*>(p);
      if (hrobp->release_ref()) {
        auto obj = static_cast<T*>(hrobp);
        hrobp->deleter_(obj);
      }
    };
  }
}; // hazptr_obj_base_refcounted

} // namespace folly
