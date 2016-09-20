/*
 * Copyright 2016 Facebook, Inc.
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

#include <folly/experimental/hazptr/debug.h>

#include <unordered_set>

namespace folly {
namespace hazptr {

/** hazptr_domain */

constexpr hazptr_domain::hazptr_domain(memory_resource* mr) noexcept
    : mr_(mr) {}

template <typename T>
void hazptr_domain::flush(const hazptr_obj_reclaim<T>* reclaim) {
  DEBUG_PRINT(this << " " << reclaim);
  flush(reinterpret_cast<const hazptr_obj_reclaim<void>*>(reclaim));
}

template <typename T>
inline void hazptr_domain::objRetire(hazptr_obj_base<T>* p) {
  DEBUG_PRINT(this << " " << p);
  objRetire(reinterpret_cast<hazptr_obj_base<void>*>(p));
}

/** hazptr_obj_base */

template <typename T>
inline void hazptr_obj_base<T>::retire(
    hazptr_domain* domain,
    const hazptr_obj_reclaim<T>* reclaim,
    const storage_policy /* policy */) {
  DEBUG_PRINT(this << " " << reclaim << " " << &domain);
  reclaim_ = reclaim;
  domain->objRetire<T>(this);
}

/* Definition of default_hazptr_obj_reclaim */

template <typename T>
inline hazptr_obj_reclaim<T>* default_hazptr_obj_reclaim() {
  static hazptr_obj_reclaim<T> fn = [](T* p) {
    DEBUG_PRINT("default_hazptr_obj_reclaim " << p << " " << sizeof(T));
    delete p;
  };
  DEBUG_PRINT(&fn);
  return &fn;
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
inline hazptr_owner<T>::hazptr_owner(
    hazptr_domain* domain,
    const cache_policy /* policy */) {
  domain_ = domain;
  hazptr_ = domain_->hazptrAcquire();
  DEBUG_PRINT(this << " " << domain_ << " " << hazptr_);
  if (hazptr_ == nullptr) { std::bad_alloc e; throw e; }
}

template <typename T>
hazptr_owner<T>::~hazptr_owner() noexcept {
  DEBUG_PRINT(this);
  domain_->hazptrRelease(hazptr_);
}

template <typename T>
inline bool hazptr_owner<T>::protect(const T* ptr, const std::atomic<T*>& src)
    const noexcept {
  DEBUG_PRINT(this << " " << ptr << " " << &src);
  hazptr_->set(ptr);
  // ORDER: store-load
  return (src.load() == ptr);
}

template <typename T>
inline void hazptr_owner<T>::set(const T* ptr) const noexcept {
  DEBUG_PRINT(this << " " << ptr);
  hazptr_->set(ptr);
}

template <typename T>
inline void hazptr_owner<T>::clear() const noexcept {
  DEBUG_PRINT(this);
  hazptr_->clear();
}

template <typename T>
inline void swap(hazptr_owner<T>& lhs, hazptr_owner<T>& rhs) noexcept {
  DEBUG_PRINT(
    &lhs << " " <<  lhs.hazptr_ << " " << lhs.domain_ << " -- "
    << &rhs << " " << rhs.hazptr_ << " " << rhs.domain_);
  std::swap(lhs.domain_, rhs.domain_);
  std::swap(lhs.hazptr_, rhs.hazptr_);
}

////////////////////////////////////////////////////////////////////////////////
// Non-template part of implementation
////////////////////////////////////////////////////////////////////////////////
// [TODO]:
// - Thread caching of hazptr_rec-s
// - Private storage of retired objects
// - Optimized memory order

/** Definition of default_hazptr_domain() */
inline hazptr_domain* default_hazptr_domain() {
  static hazptr_domain d;
  return &d;
}

/** hazptr_rec */

inline void hazptr_rec::set(const void* p) noexcept {
  DEBUG_PRINT(this << " " << p);
  hazptr_.store(p);
}

inline const void* hazptr_rec::get() const noexcept {
  DEBUG_PRINT(this << " " << hazptr_.load());
  return hazptr_.load();
}

inline void hazptr_rec::clear() noexcept {
  DEBUG_PRINT(this);
  // ORDER: release
  hazptr_.store(nullptr);
}

inline void hazptr_rec::release() noexcept {
  DEBUG_PRINT(this);
  clear();
  // ORDER: release
  active_.store(false);
}

/** hazptr_domain */

inline hazptr_domain::~hazptr_domain() {
  DEBUG_PRINT(this);
  { /* free all hazptr_rec-s */
    hazptr_rec* next;
    for (auto p = hazptrs_.load(); p; p = next) {
      next = p->next_;
      mr_->deallocate(static_cast<void*>(p), sizeof(hazptr_rec));
    }
  }
  { /* reclaim all remaining retired objects */
    hazptr_obj* next;
    for (auto p = retired_.load(); p; p = next) {
      next = p->next_;
      (*(p->reclaim_))(p);
    }
  }
}

inline void hazptr_domain::flush() {
  DEBUG_PRINT(this);
  auto rcount = rcount_.exchange(0);
  auto p = retired_.exchange(nullptr);
  hazptr_obj* next;
  for (; p; p = next) {
    next = p->next_;
    (*(p->reclaim_))(p);
    --rcount;
  }
  rcount_.fetch_add(rcount);
}

inline hazptr_rec* hazptr_domain::hazptrAcquire() {
  hazptr_rec* p;
  hazptr_rec* next;
  for (p = hazptrs_.load(); p; p = next) {
    next = p->next_;
    bool active = p->active_.load();
    if (!active) {
      if (p->active_.compare_exchange_weak(active, true)) {
        DEBUG_PRINT(this << " " << p);
        return p;
      }
    }
  }
  p = static_cast<hazptr_rec*>(mr_->allocate(sizeof(hazptr_rec)));
  if (p == nullptr) {
    return nullptr;
  }
  p->active_.store(true);
  do {
    p->next_ = hazptrs_.load();
    if (hazptrs_.compare_exchange_weak(p->next_, p)) {
      break;
    }
  } while (true);
  auto hcount = hcount_.fetch_add(1);
  DEBUG_PRINT(this << " " << p << " " << sizeof(hazptr_rec) << " " << hcount);
  return p;
}

inline void hazptr_domain::hazptrRelease(hazptr_rec* p) const noexcept {
  DEBUG_PRINT(this << " " << p);
  p->release();
}

inline int
hazptr_domain::pushRetired(hazptr_obj* head, hazptr_obj* tail, int count) {
  tail->next_ = retired_.load();
  // ORDER: store-store order
  while (!retired_.compare_exchange_weak(tail->next_, head)) {}
  return rcount_.fetch_add(count);
}

inline void hazptr_domain::objRetire(hazptr_obj* p) {
  auto rcount = pushRetired(p, p, 1) + 1;
  if (rcount >= kScanThreshold * hcount_.load()) {
    bulkReclaim();
  }
}

inline void hazptr_domain::bulkReclaim() {
  DEBUG_PRINT(this);
  auto h = hazptrs_.load();
  auto hcount = hcount_.load();
  auto rcount = rcount_.load();
  do {
    if (rcount < kScanThreshold * hcount) {
      return;
    }
    if (rcount_.compare_exchange_weak(rcount, 0)) {
      break;
    }
  } while (true);
  /* ORDER: store-load order between removing each object and scanning
   * the hazard pointers -- can be combined in one fence */
  std::unordered_set<const void*> hs;
  for (; h; h = h->next_) {
    hs.insert(h->hazptr_.load());
  }
  rcount = 0;
  hazptr_obj* retired = nullptr;
  hazptr_obj* tail = nullptr;
  auto p = retired_.exchange(nullptr);
  hazptr_obj* next;
  for (; p; p = next) {
    next = p->next_;
    if (hs.count(p) == 0) {
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

inline void hazptr_domain::flush(const hazptr_obj_reclaim<void>* reclaim) {
  DEBUG_PRINT(this << " " << reclaim);
  auto rcount = rcount_.exchange(0);
  auto p = retired_.exchange(nullptr);
  hazptr_obj* retired = nullptr;
  hazptr_obj* tail = nullptr;
  hazptr_obj* next;
  for (; p; p = next) {
    next = p->next_;
    if (p->reclaim_ == reclaim) {
      (*reclaim)(p);
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

/** hazptr_user */

inline void hazptr_user::flush() {
  DEBUG_PRINT("");
}

/** hazptr_remover */

inline void hazptr_remover::flush() {
  DEBUG_PRINT("");
}

} // namespace folly
} // namespace hazptr
