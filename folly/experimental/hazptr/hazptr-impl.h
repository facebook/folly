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
#include <folly/experimental/hazptr/debug.h>
#include <folly/synchronization/AsymmetricMemoryBarrier.h>

#include <mutex> // for thread caching
#include <unordered_set> // for hash set in bulk reclamation

namespace folly {
namespace hazptr {

/**
 *  Helper classes and functions
 */

/** hazptr_stats */

class hazptr_stats;

#if HAZPTR_STATS
#define INC_HAZPTR_STATS(x) hazptr_stats_.x()
#else
#define INC_HAZPTR_STATS(x)
#endif

/** hazptr_mb */

class hazptr_mb {
 public:
  static void light();
  static void heavy();
};

/**
 *  TLS structures
 */

/** TLS life state */

enum hazptr_tls_state { TLS_ALIVE, TLS_UNINITIALIZED, TLS_DESTROYED };

/** hazptr_tc structures
 *  Thread caching of hazptr_rec-s that belong to the default domain.
 */

struct hazptr_tc_entry {
  hazptr_rec* hprec_;

  void fill(hazptr_rec* hprec);
  hazptr_rec* get();
  void evict();
};

static_assert(
    std::is_trivial<hazptr_tc_entry>::value,
    "hazptr_tc_entry must be trivial"
    " to avoid a branch to check initialization");

struct hazptr_tc {
  hazptr_tc_entry entry_[HAZPTR_TC_SIZE];
  size_t count_;
  bool local_; // for debug mode only

 public:
  hazptr_tc_entry& operator[](size_t i);
  hazptr_rec* get();
  bool put(hazptr_rec* hprec);
  size_t count();
};

static_assert(
    std::is_trivial<hazptr_tc>::value,
    "hazptr_tc must be trivial to avoid a branch to check initialization");

hazptr_tc* hazptr_tc_tls();
void hazptr_tc_init();
void hazptr_tc_shutdown();
hazptr_rec* hazptr_tc_try_get();
bool hazptr_tc_try_put(hazptr_rec* hprec);

/** hazptr_priv structures
 *  Thread private lists of retired objects that belong to the default domain.
 */

struct hazptr_priv {
  hazptr_obj* head_;
  hazptr_obj* tail_;
  int rcount_;
  bool active_;

  void push(hazptr_obj* obj);
  void pushAllToDomain();
};

static_assert(
    std::is_trivial<hazptr_priv>::value,
    "hazptr_priv must be trivial to avoid a branch to check initialization");

void hazptr_priv_init();
void hazptr_priv_shutdown();
bool hazptr_priv_try_retire(hazptr_obj* obj);

/** hazptr_tls_life */

struct hazptr_tls_life {
  hazptr_tls_life();
  ~hazptr_tls_life();
};

void tls_life_odr_use();

/** tls globals */

extern thread_local hazptr_tls_state tls_state_;
extern thread_local hazptr_tc tls_tc_data_;
extern thread_local hazptr_priv tls_priv_data_;
extern thread_local hazptr_tls_life tls_life_; // last

/**
 *  hazptr_domain
 */

inline constexpr hazptr_domain::hazptr_domain(memory_resource* mr) noexcept
    : mr_(mr) {}

/**
 *  hazptr_obj_base
 */

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
    if (hazptr_priv_try_retire(this)) {
      return;
    }
  }
  domain.objRetire(this);
}

/**
 *  hazptr_obj_base_refcounted
 */

template <typename T, typename D>
inline void hazptr_obj_base_refcounted<T, D>::retire(
    hazptr_domain& domain,
    D deleter) {
  DEBUG_PRINT(this << " " << &domain);
  deleter_ = std::move(deleter);
  reclaim_ = [](hazptr_obj* p) {
    auto hrobp = static_cast<hazptr_obj_base_refcounted*>(p);
    if (hrobp->release_ref()) {
      auto obj = static_cast<T*>(hrobp);
      hrobp->deleter_(obj);
    }
  };
  if (HAZPTR_PRIV &&
      (HAZPTR_ONE_DOMAIN || (&domain == &default_hazptr_domain()))) {
    if (hazptr_priv_try_retire(this)) {
      return;
    }
  }
  domain.objRetire(this);
}

template <typename T, typename D>
inline void hazptr_obj_base_refcounted<T, D>::acquire_ref() {
  DEBUG_PRINT(this);
  auto oldval = refcount_.fetch_add(1);
  DCHECK(oldval >= 0);
}

template <typename T, typename D>
inline void hazptr_obj_base_refcounted<T, D>::acquire_ref_safe() {
  DEBUG_PRINT(this);
  auto oldval = refcount_.load(std::memory_order_acquire);
  DCHECK(oldval >= 0);
  refcount_.store(oldval + 1, std::memory_order_release);
}

template <typename T, typename D>
inline bool hazptr_obj_base_refcounted<T, D>::release_ref() {
  DEBUG_PRINT(this);
  auto oldval = refcount_.load(std::memory_order_acquire);
  if (oldval > 0) {
    oldval = refcount_.fetch_sub(1);
  } else {
    if (kIsDebug) {
      refcount_.store(-1);
    }
  }
  DEBUG_PRINT(this << " " << oldval);
  DCHECK(oldval >= 0);
  return oldval == 0;
}

/**
 *  hazptr_rec
 */

class alignas(hardware_destructive_interference_size) hazptr_rec {
  friend class hazptr_domain;
  friend class hazptr_holder;
  friend struct hazptr_tc_entry;

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

/**
 *  hazptr_holder
 */

FOLLY_ALWAYS_INLINE hazptr_holder::hazptr_holder(hazptr_domain& domain) {
  domain_ = &domain;
  if (LIKELY(
          HAZPTR_TC &&
          (HAZPTR_ONE_DOMAIN || &domain == &default_hazptr_domain()))) {
    auto hprec = hazptr_tc_try_get();
    if (LIKELY(hprec != nullptr)) {
      hazptr_ = hprec;
      DEBUG_PRINT(this << " " << domain_ << " " << hazptr_);
      return;
    }
  }
  hazptr_ = domain_->hazptrAcquire();
  DEBUG_PRINT(this << " " << domain_ << " " << hazptr_);
  if (hazptr_ == nullptr) { std::bad_alloc e; throw e; }
}

FOLLY_ALWAYS_INLINE hazptr_holder::hazptr_holder(std::nullptr_t) noexcept {
  domain_ = nullptr;
  hazptr_ = nullptr;
  DEBUG_PRINT(this << " " << domain_ << " " << hazptr_);
}

FOLLY_ALWAYS_INLINE hazptr_holder::~hazptr_holder() {
  DEBUG_PRINT(this);
  if (LIKELY(hazptr_ != nullptr)) {
    hazptr_->clear();
    if (LIKELY(
            HAZPTR_TC &&
            (HAZPTR_ONE_DOMAIN || domain_ == &default_hazptr_domain()))) {
      if (LIKELY(hazptr_tc_try_put(hazptr_))) {
        return;
      }
    }
    domain_->hazptrRelease(hazptr_);
  }
}

FOLLY_ALWAYS_INLINE hazptr_holder::hazptr_holder(hazptr_holder&& rhs) noexcept {
  domain_ = rhs.domain_;
  hazptr_ = rhs.hazptr_;
  rhs.domain_ = nullptr;
  rhs.hazptr_ = nullptr;
}

FOLLY_ALWAYS_INLINE
hazptr_holder& hazptr_holder::operator=(hazptr_holder&& rhs) noexcept {
  /* Self-move is a no-op.  */
  if (LIKELY(this != &rhs)) {
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
  if (UNLIKELY(p != ptr)) {
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

/**
 *  hazptr_array
 */

template <size_t M>
FOLLY_ALWAYS_INLINE hazptr_array<M>::hazptr_array() {
  auto h = reinterpret_cast<hazptr_holder*>(&raw_);
  if (HAZPTR_TC) {
    auto ptc = hazptr_tc_tls();
    if (LIKELY(ptc != nullptr)) {
      auto& tc = *ptc;
      auto count = tc.count();
      if (M <= count) {
        size_t offset = count - M;
        for (size_t i = 0; i < M; ++i) {
          auto hprec = tc[offset + i].hprec_;
          DCHECK(hprec != nullptr);
          DEBUG_PRINT(i << " " << &h[i]);
          new (&h[i]) hazptr_holder(nullptr);
          h[i].hazptr_ = hprec;
          DEBUG_PRINT(
              i << " " << &h[i] << " " << h[i].domain_ << " " << h[i].hazptr_);
        }
        tc.count_ = offset;
        return;
      }
    }
  }
  // slow path
  for (size_t i = 0; i < M; ++i) {
    new (&h[i]) hazptr_holder;
    DEBUG_PRINT(
        i << " " << &h[i] << " " << h[i].domain_ << " " << h[i].hazptr_);
  }
}

template <size_t M>
FOLLY_ALWAYS_INLINE hazptr_array<M>::hazptr_array(
    hazptr_array&& other) noexcept {
  DEBUG_PRINT(this << " " << M << " " << &other);
  auto h = reinterpret_cast<hazptr_holder*>(&raw_);
  auto hother = reinterpret_cast<hazptr_holder*>(&other.raw_);
  for (size_t i = 0; i < M; ++i) {
    new (&h[i]) hazptr_holder(std::move(hother[i]));
    DEBUG_PRINT(i << " " << &h[i] << " " << &hother[i]);
  }
  empty_ = other.empty_;
  other.empty_ = true;
}

template <size_t M>
FOLLY_ALWAYS_INLINE hazptr_array<M>::hazptr_array(std::nullptr_t) noexcept {
  DEBUG_PRINT(this << " " << M);
  auto h = reinterpret_cast<hazptr_holder*>(&raw_);
  for (size_t i = 0; i < M; ++i) {
    new (&h[i]) hazptr_holder(nullptr);
    DEBUG_PRINT(i << " " << &h[i]);
  }
  empty_ = true;
}

template <size_t M>
FOLLY_ALWAYS_INLINE hazptr_array<M>::~hazptr_array() {
  if (empty_) {
    return;
  }
  auto h = reinterpret_cast<hazptr_holder*>(&raw_);
  if (HAZPTR_TC) {
    auto ptc = hazptr_tc_tls();
    if (LIKELY(ptc != nullptr)) {
      auto& tc = *ptc;
      auto count = tc.count();
      if ((M <= HAZPTR_TC_SIZE) && (count + M <= HAZPTR_TC_SIZE)) {
        for (size_t i = 0; i < M; ++i) {
          tc[count + i].hprec_ = h[i].hazptr_;
          DEBUG_PRINT(i << " " << &h[i]);
          new (&h[i]) hazptr_holder(nullptr);
          DEBUG_PRINT(
              i << " " << &h[i] << " " << h[i].domain_ << " " << h[i].hazptr_);
        }
        tc.count_ = count + M;
        return;
      }
    }
  }
  // slow path
  for (size_t i = 0; i < M; ++i) {
    h[i].~hazptr_holder();
  }
}

template <size_t M>
FOLLY_ALWAYS_INLINE hazptr_array<M>& hazptr_array<M>::operator=(
    hazptr_array&& other) noexcept {
  DEBUG_PRINT(this << " " << M << " " << &other);
  auto h = reinterpret_cast<hazptr_holder*>(&raw_);
  for (size_t i = 0; i < M; ++i) {
    h[i] = std::move(other[i]);
    DEBUG_PRINT(i << " " << &h[i] << " " << &other[i]);
  }
  empty_ = other.empty_;
  other.empty_ = true;
  return *this;
}

template <size_t M>
FOLLY_ALWAYS_INLINE hazptr_holder& hazptr_array<M>::operator[](
    size_t i) noexcept {
  auto h = reinterpret_cast<hazptr_holder*>(&raw_);
  DCHECK(i < M);
  return h[i];
}

/**
 *  hazptr_local
 */

template <size_t M>
FOLLY_ALWAYS_INLINE hazptr_local<M>::hazptr_local() {
  auto h = reinterpret_cast<hazptr_holder*>(&raw_);
  if (HAZPTR_TC) {
    auto ptc = hazptr_tc_tls();
    if (LIKELY(ptc != nullptr)) {
      auto& tc = *ptc;
      auto count = tc.count();
      if (M <= count) {
        if (kIsDebug) {
          DCHECK(!tc.local_);
          tc.local_ = true;
        }
        // Fast path
        for (size_t i = 0; i < M; ++i) {
          auto hprec = tc[i].hprec_;
          DCHECK(hprec != nullptr);
          DEBUG_PRINT(i << " " << &h[i]);
          new (&h[i]) hazptr_holder(nullptr);
          h[i].hazptr_ = hprec;
          DEBUG_PRINT(
              i << " " << &h[i] << " " << h[i].domain_ << " " << h[i].hazptr_);
        }
        return;
      }
    }
  }
  // Slow path
  need_destruct_ = true;
  for (size_t i = 0; i < M; ++i) {
    new (&h[i]) hazptr_holder;
    DEBUG_PRINT(
        i << " " << &h[i] << " " << h[i].domain_ << " " << h[i].hazptr_);
  }
}

template <size_t M>
FOLLY_ALWAYS_INLINE hazptr_local<M>::~hazptr_local() {
  if (LIKELY(!need_destruct_)) {
    if (kIsDebug) {
      auto ptc = hazptr_tc_tls();
      DCHECK(ptc != nullptr);
      auto& tc = *ptc;
      DCHECK(tc.local_);
      tc.local_ = false;
    }
    return;
  }
  // Slow path
  auto h = reinterpret_cast<hazptr_holder*>(&raw_);
  for (size_t i = 0; i < M; ++i) {
    h[i].~hazptr_holder();
  }
}

template <size_t M>
FOLLY_ALWAYS_INLINE hazptr_holder& hazptr_local<M>::operator[](
    size_t i) noexcept {
  auto h = reinterpret_cast<hazptr_holder*>(&raw_);
  DCHECK(i < M);
  return h[i];
}

////////////////////////////////////////////////////////////////////////////////
// [TODO]:
// - Control of reclamation (when and by whom)
// - End-to-end lock-free implementation

/** Definition of default_hazptr_domain() */

FOLLY_ALWAYS_INLINE hazptr_domain& default_hazptr_domain() {
  DEBUG_PRINT(&default_domain_);
  return default_domain_;
}

template <typename T, typename D>
FOLLY_ALWAYS_INLINE void hazptr_retire(T* obj, D reclaim) {
  default_hazptr_domain().retire(obj, std::move(reclaim));
}

/** hazptr_rec */

FOLLY_ALWAYS_INLINE void hazptr_rec::set(const void* p) noexcept {
  DEBUG_PRINT(this << " " << p);
  hazptr_.store(p, std::memory_order_release);
}

inline const void* hazptr_rec::get() const noexcept {
  auto p = hazptr_.load(std::memory_order_acquire);
  DEBUG_PRINT(this << " " << p);
  return p;
}

FOLLY_ALWAYS_INLINE void hazptr_rec::clear() noexcept {
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

template <typename T, typename D>
void hazptr_domain::retire(T* obj, D reclaim) {
  struct hazptr_retire_node : hazptr_obj {
    std::unique_ptr<T, D> obj_;

    hazptr_retire_node(T* obj, D reclaim) : obj_{obj, std::move(reclaim)} {}
  };

  auto node = new hazptr_retire_node(obj, std::move(reclaim));
  node->reclaim_ = [](hazptr_obj* p) {
    delete static_cast<hazptr_retire_node*>(p);
  };
  objRetire(node);
}

inline hazptr_domain::~hazptr_domain() {
  DEBUG_PRINT(this);
  { /* reclaim all remaining retired objects */
    hazptr_obj* next;
    auto retired = retired_.exchange(nullptr);
    while (retired) {
      for (auto p = retired; p; p = next) {
        next = p->next_;
        DEBUG_PRINT(this << " " << p << " " << p->reclaim_);
        (*(p->reclaim_))(p);
      }
      retired = retired_.exchange(nullptr);
    }
  }
  /* Leak the data for the default domain to avoid destruction order
   * issues with thread caches.
   */
  if (this != &default_hazptr_domain()) {
    /* free all hazptr_rec-s */
    hazptr_rec* next;
    for (auto p = hazptrs_.load(std::memory_order_acquire); p; p = next) {
      next = p->next_;
      DCHECK(!p->isActive());
      mr_->deallocate(static_cast<void*>(p), sizeof(hazptr_rec));
    }
  }
}

inline hazptr_rec* hazptr_domain::hazptrAcquire() {
  hazptr_rec* p;
  hazptr_rec* next;
  for (p = hazptrs_.load(std::memory_order_acquire); p; p = next) {
    next = p->next_;
    if (p->tryAcquire()) {
      return p;
    }
  }
  p = static_cast<hazptr_rec*>(mr_->allocate(sizeof(hazptr_rec)));
  DEBUG_PRINT(this << " " << p << " " << sizeof(hazptr_rec));
  if (p == nullptr) {
    return nullptr;
  }
  p->active_.store(true, std::memory_order_relaxed);
  p->next_ = hazptrs_.load(std::memory_order_acquire);
  while (!hazptrs_.compare_exchange_weak(
      p->next_, p, std::memory_order_release, std::memory_order_acquire)) {
    /* keep trying */;
  }
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

extern hazptr_stats hazptr_stats_;

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

/** hazptr_mb */

FOLLY_ALWAYS_INLINE void hazptr_mb::light() {
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

/**
 *  TLS structures
 */

/**
 *  hazptr_tc structures
 */

/** hazptr_tc_entry */

FOLLY_ALWAYS_INLINE void hazptr_tc_entry::fill(hazptr_rec* hprec) {
  hprec_ = hprec;
  DEBUG_PRINT(this << " " << hprec);
}

FOLLY_ALWAYS_INLINE hazptr_rec* hazptr_tc_entry::get() {
  auto hprec = hprec_;
  DEBUG_PRINT(this << " " << hprec);
  return hprec;
}

inline void hazptr_tc_entry::evict() {
  auto hprec = hprec_;
  hprec->release();
  DEBUG_PRINT(this << " " << hprec);
}

/** hazptr_tc */

FOLLY_ALWAYS_INLINE hazptr_tc_entry& hazptr_tc::operator[](size_t i) {
  DCHECK(i <= HAZPTR_TC_SIZE);
  return entry_[i];
}

FOLLY_ALWAYS_INLINE hazptr_rec* hazptr_tc::get() {
  if (LIKELY(count_ != 0)) {
    auto hprec = entry_[--count_].get();
    DEBUG_PRINT(this << " " << hprec);
    return hprec;
  }
  DEBUG_PRINT(this << " nullptr");
  return nullptr;
}

FOLLY_ALWAYS_INLINE bool hazptr_tc::put(hazptr_rec* hprec) {
  if (LIKELY(count_ < HAZPTR_TC_SIZE)) {
    entry_[count_++].fill(hprec);
    DEBUG_PRINT(this << " " << count_ - 1);
    return true;
  }
  return false;
}

FOLLY_ALWAYS_INLINE size_t hazptr_tc::count() {
  return count_;
}

/** hazptr_tc free functions */

FOLLY_ALWAYS_INLINE hazptr_tc* hazptr_tc_tls() {
  DEBUG_PRINT(tls_state_);
  if (LIKELY(tls_state_ == TLS_ALIVE)) {
    DEBUG_PRINT(tls_state_);
    return &tls_tc_data_;
  } else if (tls_state_ == TLS_UNINITIALIZED) {
    tls_life_odr_use();
    return &tls_tc_data_;
  }
  return nullptr;
}

inline void hazptr_tc_init() {
  DEBUG_PRINT("");
  auto& tc = tls_tc_data_;
  DEBUG_PRINT(&tc);
  tc.count_ = 0;
  if (kIsDebug) {
    tc.local_ = false;
  }
}

inline void hazptr_tc_shutdown() {
  auto& tc = tls_tc_data_;
  DEBUG_PRINT(&tc);
  for (size_t i = 0; i < tc.count_; ++i) {
    tc.entry_[i].evict();
  }
}

FOLLY_ALWAYS_INLINE hazptr_rec* hazptr_tc_try_get() {
  DEBUG_PRINT(TLS_UNINITIALIZED << TLS_ALIVE << TLS_DESTROYED);
  DEBUG_PRINT(tls_state_);
  if (LIKELY(tls_state_ == TLS_ALIVE)) {
    DEBUG_PRINT(tls_state_);
    return tls_tc_data_.get();
  } else if (tls_state_ == TLS_UNINITIALIZED) {
    tls_life_odr_use();
    return tls_tc_data_.get();
  }
  return nullptr;
}

FOLLY_ALWAYS_INLINE bool hazptr_tc_try_put(hazptr_rec* hprec) {
  DEBUG_PRINT(tls_state_);
  if (LIKELY(tls_state_ == TLS_ALIVE)) {
    DEBUG_PRINT(tls_state_);
    return tls_tc_data_.put(hprec);
  }
  return false;
}

/**
 *  hazptr_priv
 */

inline void hazptr_priv::push(hazptr_obj* obj) {
  auto& domain = default_hazptr_domain();
  obj->next_ = nullptr;
  if (tail_) {
    tail_->next_ = obj;
  } else {
    if (!active_) {
      domain.objRetire(obj);
      return;
    }
    head_ = obj;
  }
  tail_ = obj;
  ++rcount_;
  if (domain.reachedThreshold(rcount_)) {
    pushAllToDomain();
  }
}

inline void hazptr_priv::pushAllToDomain() {
  auto& domain = default_hazptr_domain();
  domain.pushRetired(head_, tail_, rcount_);
  head_ = nullptr;
  tail_ = nullptr;
  rcount_ = 0;
  domain.tryBulkReclaim();
}

inline void hazptr_priv_init() {
  auto& priv = tls_priv_data_;
  DEBUG_PRINT(&priv);
  priv.head_ = nullptr;
  priv.tail_ = nullptr;
  priv.rcount_ = 0;
  priv.active_ = true;
}

inline void hazptr_priv_shutdown() {
  auto& priv = tls_priv_data_;
  DEBUG_PRINT(&priv);
  DCHECK(priv.active_);
  priv.active_ = false;
  if (priv.tail_) {
    priv.pushAllToDomain();
  }
}

inline bool hazptr_priv_try_retire(hazptr_obj* obj) {
  DEBUG_PRINT(tls_state_);
  if (tls_state_ == TLS_ALIVE) {
    DEBUG_PRINT(tls_state_);
    tls_priv_data_.push(obj);
    return true;
  } else if (tls_state_ == TLS_UNINITIALIZED) {
    DEBUG_PRINT(tls_state_);
    tls_life_odr_use();
    tls_priv_data_.push(obj);
    return true;
  }
  return false;
}

/** hazptr_tls_life */

inline void tls_life_odr_use() {
  DEBUG_PRINT(tls_state_);
  CHECK(tls_state_ == TLS_UNINITIALIZED);
  auto volatile tlsOdrUse = &tls_life_;
  CHECK(tlsOdrUse != nullptr);
  DEBUG_PRINT(tlsOdrUse);
}

inline hazptr_tls_life::hazptr_tls_life() {
  DEBUG_PRINT(this);
  CHECK(tls_state_ == TLS_UNINITIALIZED);
  hazptr_tc_init();
  hazptr_priv_init();
  tls_state_ = TLS_ALIVE;
}

inline hazptr_tls_life::~hazptr_tls_life() {
  DEBUG_PRINT(this);
  CHECK(tls_state_ == TLS_ALIVE);
  hazptr_tc_shutdown();
  hazptr_priv_shutdown();
  tls_state_ = TLS_DESTROYED;
}

} // namespace hazptr
} // namespace folly
