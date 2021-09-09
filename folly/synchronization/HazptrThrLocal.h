/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <folly/synchronization/Hazptr-fwd.h>

#if FOLLY_HAZPTR_THR_LOCAL

#include <atomic>

#include <glog/logging.h>

#include <folly/SingletonThreadLocal.h>
#include <folly/synchronization/HazptrObj.h>
#include <folly/synchronization/HazptrRec.h>

/**
 *  Thread local classes and singletons
 */

namespace folly {

/**
 *  hazptr_tc_entry
 *
 *  Thread cache entry.
 */
template <template <typename> class Atom>
class hazptr_tc_entry {
  hazptr_rec<Atom>* hprec_;

  template <uint8_t, template <typename> class>
  friend class hazptr_array;
  template <uint8_t, template <typename> class>
  friend class hazptr_local;
  friend class hazptr_tc<Atom>;
  template <uint8_t M, template <typename> class A>
  friend hazptr_array<M, A> make_hazard_pointer_array();

  FOLLY_ALWAYS_INLINE void fill(hazptr_rec<Atom>* hprec) noexcept {
    hprec_ = hprec;
  }

  FOLLY_ALWAYS_INLINE hazptr_rec<Atom>* get() const noexcept { return hprec_; }

  void evict() { hprec_->release(); }
}; // hazptr_tc_entry

/**
 *  hazptr_tc:
 *
 *  Thread cache of hazptr_rec-s that belong to the default domain.
 */
template <template <typename> class Atom>
class hazptr_tc {
  static constexpr uint8_t kCapacity = 9;

  hazptr_tc_entry<Atom> entry_[kCapacity];
  uint8_t count_{0};
  bool local_{false}; // for debug mode only

 public:
  ~hazptr_tc() {
    for (uint8_t i = 0; i < count(); ++i) {
      entry_[i].evict();
    }
  }

  static constexpr uint8_t capacity() noexcept { return kCapacity; }

 private:
  template <uint8_t, template <typename> class>
  friend class hazptr_array;
  friend class hazptr_holder<Atom>;
  template <uint8_t, template <typename> class>
  friend class hazptr_local;
  friend hazptr_holder<Atom> make_hazard_pointer<Atom>(hazptr_domain<Atom>&);
  template <uint8_t M, template <typename> class A>
  friend hazptr_array<M, A> make_hazard_pointer_array();

  FOLLY_ALWAYS_INLINE
  hazptr_tc_entry<Atom>& operator[](uint8_t i) noexcept {
    DCHECK(i <= capacity());
    return entry_[i];
  }

  FOLLY_ALWAYS_INLINE hazptr_rec<Atom>* try_get() noexcept {
    if (LIKELY(count_ > 0)) {
      auto hprec = entry_[--count_].get();
      return hprec;
    }
    return nullptr;
  }

  FOLLY_ALWAYS_INLINE bool try_put(hazptr_rec<Atom>* hprec) noexcept {
    if (LIKELY(count_ < capacity())) {
      entry_[count_++].fill(hprec);
      return true;
    }
    hazptr_warning_tc_overflow();
    return false;
  }

  FOLLY_EXPORT FOLLY_NOINLINE void hazptr_warning_tc_overflow() {
    static std::atomic<uint64_t> warning_count{0};
    if ((warning_count++ % 10000) == 0) {
      LOG(WARNING) << "Hazptr thread cache overflow "
                   << std::this_thread::get_id();
      ;
    }
  }

  FOLLY_ALWAYS_INLINE uint8_t count() const noexcept { return count_; }

  FOLLY_ALWAYS_INLINE void set_count(uint8_t val) noexcept { count_ = val; }

  FOLLY_NOINLINE void fill(uint8_t num) {
    DCHECK_LE(count_ + num, capacity());
    auto& domain = default_hazptr_domain<Atom>();
    for (uint8_t i = 0; i < num; ++i) {
      auto hprec = domain.hprec_acquire();
      entry_[count_++].fill(hprec);
    }
  }

  FOLLY_NOINLINE void evict(uint8_t num) {
    DCHECK_GE(count_, num);
    for (uint8_t i = 0; i < num; ++i) {
      entry_[--count_].evict();
    }
  }

  bool local() const noexcept { // for debugging only
    return local_;
  }

  void set_local(bool b) noexcept { // for debugging only
    local_ = b;
  }
}; // hazptr_tc

struct hazptr_tc_tls_tag {};
/** hazptr_tc_tls */
template <template <typename> class Atom>
FOLLY_ALWAYS_INLINE hazptr_tc<Atom>& hazptr_tc_tls() {
  return folly::SingletonThreadLocal<hazptr_tc<Atom>, hazptr_tc_tls_tag>::get();
}

} // namespace folly

#endif // FOLLY_HAZPTR_THR_LOCAL
