/*
 * Copyright 2017 Facebook, Inc.
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

/* override-include-guard */
#ifndef HAZPTR_H
#error "This should only be included by hazptr.h"
#endif

/* quality of implementation switches */

// NOTE: The #ifndef pattern is prone to ODR violation. Its use for
// quality of implementation options is temporary. Eventually these
// options should be added to the API in future API extensions.

#ifndef HAZPTR_AMB
#define HAZPTR_AMB true
#endif

#ifndef HAZPTR_SCAN_MULT
#define HAZPTR_SCAN_MULT 2
#endif

#ifndef HAZPTR_SCAN_THRESHOLD
#define HAZPTR_SCAN_THRESHOLD 1000
#endif

/* stats switch */
#ifndef HAZPTR_STATS
#define HAZPTR_STATS false
#endif

#include <folly/experimental/AsymmetricMemoryBarrier.h>
#include <folly/experimental/hazptr/debug.h>

#include <unordered_set>

namespace folly {
namespace hazptr {

/**
 * helper classes and functions
 */

/** hazptr_stats */

class hazptr_stats;
hazptr_stats& hazptr_stats();

/** hazptr_mb */

class hazptr_mb {
 public:
  static void light();
  static void heavy();
};

/**
 * public functions
 */

/** hazptr_domain */

constexpr hazptr_domain::hazptr_domain(memory_resource* mr) noexcept : mr_(mr) {
  hazptr_stats();
}

/** hazptr_obj_base */

template <typename T, typename D>
inline void hazptr_obj_base<T, D>::retire(hazptr_domain& domain, D deleter) {
  DEBUG_PRINT(this << " " << &domain);
  deleter_ = std::move(deleter);
  reclaim_ = [](hazptr_obj* p) {
    auto hobp = static_cast<hazptr_obj_base*>(p);
    auto obj = static_cast<T*>(hobp);
    hobp->deleter_(obj);
  };
  domain.objRetire(this);
}

/** hazptr_rec */

class hazptr_rec {
  friend class hazptr_domain;
  template <typename> friend class hazptr_owner;

  std::atomic<const void*> hazptr_ = {nullptr};
  hazptr_rec* next_ = {nullptr};
  std::atomic<bool> active_ = {false};

  void set(const void* p) noexcept;
  const void* get() const noexcept;
  void clear() noexcept;
  void release() noexcept;
};

/** hazptr_owner */

template <typename T>
inline hazptr_owner<T>::hazptr_owner(hazptr_domain& domain) {
  domain_ = &domain;
  hazptr_ = domain_->hazptrAcquire();
  DEBUG_PRINT(this << " " << domain_ << " " << hazptr_);
  if (hazptr_ == nullptr) { std::bad_alloc e; throw e; }
}

template <typename T>
hazptr_owner<T>::~hazptr_owner() {
  DEBUG_PRINT(this);
  domain_->hazptrRelease(hazptr_);
}

template <typename T>
template <typename A>
inline bool hazptr_owner<T>::try_protect(T*& ptr, const A& src) noexcept {
  static_assert(
      std::is_same<decltype(std::declval<A>().load()), T*>::value,
      "Return type of A::load() must be T*");
  DEBUG_PRINT(this << " " << ptr << " " << &src);
  set(ptr);
  /*** Full fence ***/ hazptr_mb::light();
  T* p = src.load(std::memory_order_acquire);
  if (p != ptr) {
    ptr = p;
    clear();
    return false;
  }
  return true;
}

template <typename T>
template <typename A>
inline T* hazptr_owner<T>::get_protected(const A& src) noexcept {
  static_assert(
      std::is_same<decltype(std::declval<A>().load()), T*>::value,
      "Return type of A::load() must be T*");
  T* p = src.load(std::memory_order_relaxed);
  while (!try_protect(p, src)) {}
  DEBUG_PRINT(this << " " << p << " " << &src);
  return p;
}

template <typename T>
inline void hazptr_owner<T>::set(const T* ptr) noexcept {
  auto p = static_cast<hazptr_obj*>(const_cast<T*>(ptr));
  DEBUG_PRINT(this << " " << ptr << " p:" << p);
  hazptr_->set(p);
}

template <typename T>
inline void hazptr_owner<T>::clear() noexcept {
  DEBUG_PRINT(this);
  hazptr_->clear();
}

template <typename T>
inline void hazptr_owner<T>::swap(hazptr_owner<T>& rhs) noexcept {
  DEBUG_PRINT(
    this << " " <<  this->hazptr_ << " " << this->domain_ << " -- "
    << &rhs << " " << rhs.hazptr_ << " " << rhs.domain_);
  std::swap(this->domain_, rhs.domain_);
  std::swap(this->hazptr_, rhs.hazptr_);
}

template <typename T>
inline void swap(hazptr_owner<T>& lhs, hazptr_owner<T>& rhs) noexcept {
  lhs.swap(rhs);
}

////////////////////////////////////////////////////////////////////////////////
// [TODO]:
// - Thread caching of hazptr_rec-s
// - Private storage of retired objects
// - Control of reclamation (when and by whom)

/** Definition of default_hazptr_domain() */
inline hazptr_domain& default_hazptr_domain() {
  static hazptr_domain d;
  DEBUG_PRINT(&d);
  return d;
}

/** hazptr_rec */

inline void hazptr_rec::set(const void* p) noexcept {
  DEBUG_PRINT(this << " " << p);
  hazptr_.store(p, std::memory_order_release);
}

inline const void* hazptr_rec::get() const noexcept {
  auto p = hazptr_.load(std::memory_order_acquire);
  DEBUG_PRINT(this << " " << p);
  return p;
}

inline void hazptr_rec::clear() noexcept {
  DEBUG_PRINT(this);
  hazptr_.store(nullptr, std::memory_order_release);
}

inline void hazptr_rec::release() noexcept {
  DEBUG_PRINT(this);
  clear();
  active_.store(false, std::memory_order_release);
}

/** hazptr_obj */

inline const void* hazptr_obj::getObjPtr() const {
  DEBUG_PRINT(this);
  return this;
}

/** hazptr_domain */

inline hazptr_domain::~hazptr_domain() {
  DEBUG_PRINT(this);
  { /* reclaim all remaining retired objects */
    hazptr_obj* next;
    auto retired = retired_.exchange(nullptr);
    while (retired) {
      for (auto p = retired; p; p = next) {
        next = p->next_;
        (*(p->reclaim_))(p);
      }
      retired = retired_.exchange(nullptr);
    }
  }
  { /* free all hazptr_rec-s */
    hazptr_rec* next;
    for (auto p = hazptrs_.load(std::memory_order_acquire); p; p = next) {
      next = p->next_;
      mr_->deallocate(static_cast<void*>(p), sizeof(hazptr_rec));
    }
  }
}

inline hazptr_rec* hazptr_domain::hazptrAcquire() {
  hazptr_rec* p;
  hazptr_rec* next;
  for (p = hazptrs_.load(std::memory_order_acquire); p; p = next) {
    next = p->next_;
    bool active = p->active_.load(std::memory_order_acquire);
    if (!active) {
      if (p->active_.compare_exchange_weak(
              active,
              true,
              std::memory_order_release,
              std::memory_order_relaxed)) {
        DEBUG_PRINT(this << " " << p);
        return p;
      }
    }
  }
  p = static_cast<hazptr_rec*>(mr_->allocate(sizeof(hazptr_rec)));
  if (p == nullptr) {
    return nullptr;
  }
  p->active_.store(true, std::memory_order_relaxed);
  p->next_ = hazptrs_.load(std::memory_order_acquire);
  do {
    if (hazptrs_.compare_exchange_weak(
            p->next_,
            p,
            std::memory_order_release,
            std::memory_order_acquire)) {
      break;
    }
  } while (true);
  auto hcount = hcount_.fetch_add(1);
  DEBUG_PRINT(this << " " << p << " " << sizeof(hazptr_rec) << " " << hcount);
  return p;
}

inline void hazptr_domain::hazptrRelease(hazptr_rec* p) noexcept {
  DEBUG_PRINT(this << " " << p);
  p->release();
}

inline int
hazptr_domain::pushRetired(hazptr_obj* head, hazptr_obj* tail, int count) {
  /*** Full fence ***/ hazptr_mb::light();
  tail->next_ = retired_.load(std::memory_order_acquire);
  while (!retired_.compare_exchange_weak(
      tail->next_,
      head,
      std::memory_order_release,
      std::memory_order_acquire)) {
  }
  return rcount_.fetch_add(count);
}

inline void hazptr_domain::objRetire(hazptr_obj* p) {
  auto rcount = pushRetired(p, p, 1) + 1;
  if (rcount >= HAZPTR_SCAN_THRESHOLD &&
      rcount >= HAZPTR_SCAN_MULT * hcount_.load(std::memory_order_acquire)) {
    tryBulkReclaim();
  }
}

inline void hazptr_domain::tryBulkReclaim() {
  DEBUG_PRINT(this);
  do {
    auto hcount = hcount_.load(std::memory_order_acquire);
    auto rcount = rcount_.load(std::memory_order_acquire);
    if (rcount < HAZPTR_SCAN_THRESHOLD || rcount < HAZPTR_SCAN_MULT * hcount) {
      return;
    }
    if (rcount_.compare_exchange_weak(
            rcount, 0, std::memory_order_release, std::memory_order_relaxed)) {
      break;
    }
  } while (true);
  bulkReclaim();
}

inline void hazptr_domain::bulkReclaim() {
  DEBUG_PRINT(this);
  /*** Full fence ***/ hazptr_mb::heavy();
  auto p = retired_.exchange(nullptr, std::memory_order_acquire);
  auto h = hazptrs_.load(std::memory_order_acquire);
  std::unordered_set<const void*> hs; // TODO lock-free alternative
  for (; h; h = h->next_) {
    hs.insert(h->hazptr_.load(std::memory_order_relaxed));
  }
  int rcount = 0;
  hazptr_obj* retired = nullptr;
  hazptr_obj* tail = nullptr;
  hazptr_obj* next;
  for (; p; p = next) {
    next = p->next_;
    if (hs.count(p->getObjPtr()) == 0) {
      DEBUG_PRINT(this << " " << p << " " << p->reclaim_);
      (*(p->reclaim_))(p);
    } else {
      p->next_ = retired;
      retired = p;
      if (tail == nullptr) {
        tail = p;
      }
      ++rcount;
    }
  }
  if (tail) {
    pushRetired(retired, tail, rcount);
  }
}

/** hazptr_stats */

class hazptr_stats {
 public:
  ~hazptr_stats();
  void light();
  void heavy();
  void seq_cst();

 private:
  std::atomic<uint64_t> light_{0};
  std::atomic<uint64_t> heavy_{0};
  std::atomic<uint64_t> seq_cst_{0};
};

inline hazptr_stats::~hazptr_stats() {
  DEBUG_PRINT(this << " light " << light_.load());
  DEBUG_PRINT(this << " heavy " << heavy_.load());
  DEBUG_PRINT(this << " seq_cst " << seq_cst_.load());
}

inline void hazptr_stats::light() {
  if (HAZPTR_STATS) {
    /* atomic */ ++light_;
  }
}

inline void hazptr_stats::heavy() {
  if (HAZPTR_STATS) {
    /* atomic */ ++heavy_;
  }
}

inline void hazptr_stats::seq_cst() {
  if (HAZPTR_STATS) {
    /* atomic */ ++seq_cst_;
  }
}

inline class hazptr_stats& hazptr_stats() {
  static class hazptr_stats stats_;
  DEBUG_PRINT(&stats_);
  return stats_;
}

/** hazptr_mb */

inline void hazptr_mb::light() {
  DEBUG_PRINT("");
  if (HAZPTR_AMB) {
    folly::asymmetricLightBarrier();
    hazptr_stats().light();
  } else {
    atomic_thread_fence(std::memory_order_seq_cst);
    hazptr_stats().seq_cst();
  }
}

inline void hazptr_mb::heavy() {
  DEBUG_PRINT("");
  if (HAZPTR_AMB) {
    folly::asymmetricHeavyBarrier();
    hazptr_stats().heavy();
  } else {
    atomic_thread_fence(std::memory_order_seq_cst);
    hazptr_stats().seq_cst();
  }
}

} // namespace folly
} // namespace hazptr
