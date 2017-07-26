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

#ifndef HAZPTR_TC
#define HAZPTR_TC true
#endif

#ifndef HAZPTR_TC_SIZE
#define HAZPTR_TC_SIZE 10
#endif

#ifndef HAZPTR_PRIV
#define HAZPTR_PRIV true
#endif

#ifndef HAZPTR_ONE_DOMAIN
#define HAZPTR_ONE_DOMAIN false
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

#include <folly/concurrency/CacheLocality.h>
#include <folly/experimental/AsymmetricMemoryBarrier.h>
#include <folly/experimental/hazptr/debug.h>

#include <mutex> // for thread caching
#include <unordered_set> // for hash set in bulk reclamation

namespace folly {
namespace hazptr {

/**
 * helper classes and functions
 */

/** hazptr_stats */

class hazptr_stats;
hazptr_stats& hazptr_stats();

#if HAZPTR_STATS
#define INC_HAZPTR_STATS(x) hazptr_stats().x()
#else
#define INC_HAZPTR_STATS(x)
#endif

/** hazptr_mb */

class hazptr_mb {
 public:
  static void light();
  static void heavy();
};

/** hazptr_tc
 *  Thread caching of hazptr_rec-s that belong to the default domain.
 */

class hazptr_tc_entry {
  friend class hazptr_tc;
  hazptr_rec* hprec_{nullptr};

 public:
  void fill(hazptr_rec* hprec);
  hazptr_rec* get();
  void evict();
};

class hazptr_tc {
  hazptr_tc_entry tc_[HAZPTR_TC_SIZE];
  int count_{0};

 public:
  hazptr_tc();
  ~hazptr_tc();
  hazptr_rec* get();
  bool put(hazptr_rec* hprec);
};

hazptr_tc& hazptr_tc();

/** hazptr_priv
 *  Thread private lists of retired objects that belong to the default domain.
 */

class hazptr_priv {
  hazptr_domain* domain_{&default_hazptr_domain()};
  hazptr_obj* head_{nullptr};
  hazptr_obj* tail_{nullptr};
  int rcount_{0};
  bool active_{true};

 public:
  hazptr_priv();
  ~hazptr_priv();
  void push(hazptr_obj* obj);

 private:
  void pushAllToDomain();
};

hazptr_priv& hazptr_priv();

/**
 * public functions
 */

/** hazptr_domain */

constexpr hazptr_domain::hazptr_domain(memory_resource* mr) noexcept
    : mr_(mr) {}

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
  if (HAZPTR_PRIV &&
      (HAZPTR_ONE_DOMAIN || (&domain == &default_hazptr_domain()))) {
    hazptr_priv().push(this);
  } else {
    domain.objRetire(this);
  }
}

/** hazptr_rec */

class hazptr_rec {
  friend class hazptr_domain;
  friend class hazptr_holder;
  friend class hazptr_tc_entry;

  FOLLY_ALIGN_TO_AVOID_FALSE_SHARING
  std::atomic<const void*> hazptr_{nullptr};
  hazptr_rec* next_{nullptr};
  std::atomic<bool> active_{false};

  void set(const void* p) noexcept;
  const void* get() const noexcept;
  void clear() noexcept;
  bool isActive() noexcept;
  bool tryAcquire() noexcept;
  void release() noexcept;
};

/** hazptr_holder */

FOLLY_ALWAYS_INLINE hazptr_holder::hazptr_holder(hazptr_domain& domain) {
  domain_ = &domain;
  hazptr_ = domain_->hazptrAcquire();
  DEBUG_PRINT(this << " " << domain_ << " " << hazptr_);
  if (hazptr_ == nullptr) { std::bad_alloc e; throw e; }
}

inline hazptr_holder::hazptr_holder(std::nullptr_t) {
  domain_ = nullptr;
  hazptr_ = nullptr;
  DEBUG_PRINT(this << " " << domain_ << " " << hazptr_);
}

FOLLY_ALWAYS_INLINE hazptr_holder::~hazptr_holder() {
  DEBUG_PRINT(this);
  if (domain_) {
    hazptr_->clear();
    domain_->hazptrRelease(hazptr_);
  }
}

inline hazptr_holder::hazptr_holder(hazptr_holder&& rhs) noexcept {
  domain_ = rhs.domain_;
  hazptr_ = rhs.hazptr_;
  rhs.domain_ = nullptr;
  rhs.hazptr_ = nullptr;
}

inline hazptr_holder& hazptr_holder::operator=(hazptr_holder&& rhs) noexcept {
  /* Self-move is a no-op.  */
  if (this != &rhs) {
    this->~hazptr_holder();
    new (this) hazptr_holder(std::move(rhs));
  }
  return *this;
}

template <typename T>
FOLLY_ALWAYS_INLINE bool hazptr_holder::try_protect(
    T*& ptr,
    const std::atomic<T*>& src) noexcept {
  return try_protect(ptr, src, [](T* t) { return t; });
}

template <typename T, typename Func>
FOLLY_ALWAYS_INLINE bool hazptr_holder::try_protect(
    T*& ptr,
    const std::atomic<T*>& src,
    Func f) noexcept {
  DEBUG_PRINT(this << " " << ptr << " " << &src);
  reset(f(ptr));
  /*** Full fence ***/ hazptr_mb::light();
  T* p = src.load(std::memory_order_acquire);
  if (p != ptr) {
    ptr = p;
    reset();
    return false;
  }
  return true;
}

template <typename T>
FOLLY_ALWAYS_INLINE T* hazptr_holder::get_protected(
    const std::atomic<T*>& src) noexcept {
  return get_protected(src, [](T* t) { return t; });
}

template <typename T, typename Func>
FOLLY_ALWAYS_INLINE T* hazptr_holder::get_protected(
    const std::atomic<T*>& src,
    Func f) noexcept {
  T* p = src.load(std::memory_order_relaxed);
  while (!try_protect(p, src, f)) {
  }
  DEBUG_PRINT(this << " " << p << " " << &src);
  return p;
}

template <typename T>
FOLLY_ALWAYS_INLINE void hazptr_holder::reset(const T* ptr) noexcept {
  auto p = static_cast<hazptr_obj*>(const_cast<T*>(ptr));
  DEBUG_PRINT(this << " " << ptr << " p:" << p);
  DCHECK(hazptr_); // UB if *this is empty
  hazptr_->set(p);
}

FOLLY_ALWAYS_INLINE void hazptr_holder::reset(std::nullptr_t) noexcept {
  DEBUG_PRINT(this);
  DCHECK(hazptr_); // UB if *this is empty
  hazptr_->clear();
}

FOLLY_ALWAYS_INLINE void hazptr_holder::swap(hazptr_holder& rhs) noexcept {
  DEBUG_PRINT(
    this << " " <<  this->hazptr_ << " " << this->domain_ << " -- "
    << &rhs << " " << rhs.hazptr_ << " " << rhs.domain_);
  if (!HAZPTR_ONE_DOMAIN) {
    std::swap(this->domain_, rhs.domain_);
  }
  std::swap(this->hazptr_, rhs.hazptr_);
}

FOLLY_ALWAYS_INLINE void swap(hazptr_holder& lhs, hazptr_holder& rhs) noexcept {
  lhs.swap(rhs);
}

////////////////////////////////////////////////////////////////////////////////
// [TODO]:
// - Control of reclamation (when and by whom)
// - End-to-end lock-free implementation

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

inline bool hazptr_rec::isActive() noexcept {
  return active_.load(std::memory_order_acquire);
}

inline bool hazptr_rec::tryAcquire() noexcept {
  bool active = isActive();
  if (!active &&
      active_.compare_exchange_strong(
          active, true, std::memory_order_release, std::memory_order_relaxed)) {
    DEBUG_PRINT(this);
    return true;
  }
  return false;
}

inline void hazptr_rec::release() noexcept {
  DEBUG_PRINT(this);
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
      DCHECK(!p->isActive());
      mr_->deallocate(static_cast<void*>(p), sizeof(hazptr_rec));
    }
  }
}

FOLLY_ALWAYS_INLINE hazptr_rec* hazptr_domain::hazptrAcquire() {
  if (HAZPTR_TC && (HAZPTR_ONE_DOMAIN || this == &default_hazptr_domain())) {
    auto hprec = hazptr_tc().get();
    if (hprec) {
      return hprec;
    }
  }
  hazptr_rec* p;
  hazptr_rec* next;
  for (p = hazptrs_.load(std::memory_order_acquire); p; p = next) {
    next = p->next_;
    if (p->tryAcquire()) {
      return p;
    }
  }
  p = static_cast<hazptr_rec*>(mr_->allocate(sizeof(hazptr_rec)));
  if (p == nullptr) {
    return nullptr;
  }
  p->active_.store(true, std::memory_order_relaxed);
  p->next_ = hazptrs_.load(std::memory_order_acquire);
  while (!hazptrs_.compare_exchange_weak(
      p->next_, p, std::memory_order_release, std::memory_order_acquire))
    /* keep trying */;
  auto hcount = hcount_.fetch_add(1);
  DEBUG_PRINT(this << " " << p << " " << sizeof(hazptr_rec) << " " << hcount);
  return p;
}

FOLLY_ALWAYS_INLINE void hazptr_domain::hazptrRelease(hazptr_rec* p) noexcept {
  if (HAZPTR_TC && (HAZPTR_ONE_DOMAIN || this == &default_hazptr_domain()) &&
      hazptr_tc().put(p)) {
    return;
  }
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
  return rcount_.fetch_add(count) + count;
}

inline bool hazptr_domain::reachedThreshold(int rcount) {
  return (
      rcount >= HAZPTR_SCAN_THRESHOLD &&
      rcount >= HAZPTR_SCAN_MULT * hcount_.load(std::memory_order_acquire));
}

inline void hazptr_domain::objRetire(hazptr_obj* p) {
  auto rcount = pushRetired(p, p, 1);
  if (reachedThreshold(rcount)) {
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
    hs.insert(h->get());
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

FOLLY_ALWAYS_INLINE void hazptr_stats::light() {
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
    INC_HAZPTR_STATS(light);
  } else {
    atomic_thread_fence(std::memory_order_seq_cst);
    INC_HAZPTR_STATS(seq_cst);
  }
}

inline void hazptr_mb::heavy() {
  DEBUG_PRINT("");
  if (HAZPTR_AMB) {
    folly::asymmetricHeavyBarrier(AMBFlags::EXPEDITED);
    INC_HAZPTR_STATS(heavy);
  } else {
    atomic_thread_fence(std::memory_order_seq_cst);
    INC_HAZPTR_STATS(seq_cst);
  }
}

/** hazptr_tc - functions */

inline void hazptr_tc_entry::fill(hazptr_rec* hprec) {
  hprec_ = hprec;
  DEBUG_PRINT(this << " " << hprec);
}

inline hazptr_rec* hazptr_tc_entry::get() {
  auto hprec = hprec_;
  hprec_ = nullptr;
  DEBUG_PRINT(this << " " << hprec);
  return hprec;
}

inline void hazptr_tc_entry::evict() {
  auto hprec = hprec_;
  hprec_ = nullptr;
  hprec->release();
  DEBUG_PRINT(this << " " << hprec);
}

inline hazptr_tc::hazptr_tc() {
  DEBUG_PRINT(this);
}

inline hazptr_tc::~hazptr_tc() {
  DEBUG_PRINT(this);
  for (int i = 0; i < count_; ++i) {
    tc_[i].evict();
  }
}

inline hazptr_rec* hazptr_tc::get() {
  if (count_ > 0) {
    auto hprec = tc_[--count_].get();
    DEBUG_PRINT(this << " " << hprec);
    return hprec;
  }
  DEBUG_PRINT(this << " nullptr");
  return nullptr;
}

inline bool hazptr_tc::put(hazptr_rec* hprec) {
  if (count_ < HAZPTR_TC_SIZE) {
    tc_[count_++].fill(hprec);
    DEBUG_PRINT(this << " " << count_ - 1);
    return true;
  }
  return false;
}

FOLLY_ALWAYS_INLINE class hazptr_tc& hazptr_tc() {
  static thread_local class hazptr_tc tc;
  DEBUG_PRINT(&tc);
  return tc;
}

/** hazptr_priv - functions */

inline hazptr_priv::hazptr_priv() {
  DEBUG_PRINT(this);
}

inline hazptr_priv::~hazptr_priv() {
  DEBUG_PRINT(this);
  DCHECK(active_);
  active_ = false;
  if (tail_) {
    pushAllToDomain();
  }
}

inline void hazptr_priv::push(hazptr_obj* obj) {
  obj->next_ = nullptr;
  if (tail_) {
    tail_->next_ = obj;
  } else {
    if (!active_) {
      default_hazptr_domain().objRetire(obj);
      return;
    }
    head_ = obj;
  }
  tail_ = obj;
  ++rcount_;
  if (domain_->reachedThreshold(rcount_)) {
    pushAllToDomain();
  }
}

inline void hazptr_priv::pushAllToDomain() {
  domain_->pushRetired(head_, tail_, rcount_);
  head_ = nullptr;
  tail_ = nullptr;
  rcount_ = 0;
  domain_->tryBulkReclaim();
}

inline class hazptr_priv& hazptr_priv() {
  static thread_local class hazptr_priv priv;
  DEBUG_PRINT(&priv);
  return priv;
}

} // namespace folly
} // namespace hazptr
