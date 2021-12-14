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

#include <atomic>

#include <folly/concurrency/CacheLocality.h>
#include <folly/synchronization/Hazptr-fwd.h>

namespace folly {

/**
 *  hazptr_rec:
 *
 *  Contains the actual hazard pointer.
 */
template <template <typename> class Atom>
class alignas(hardware_destructive_interference_size) hazptr_rec {
  Atom<const void*> hazptr_{nullptr}; // the hazard pointer
  hazptr_domain<Atom>* domain_;
  hazptr_rec* next_; // Next in the main hazard pointer list. Immutable.
  hazptr_rec* nextAvail_{nullptr}; // Next available hazard pointer.

  friend class hazptr_domain<Atom>;
  friend class hazptr_holder<Atom>;
#if FOLLY_HAZPTR_THR_LOCAL
  friend class hazptr_tc<Atom>;
#endif
  friend hazptr_holder<Atom> make_hazard_pointer<Atom>(hazptr_domain<Atom>&);
  template <uint8_t M, template <typename> class A>
  friend hazptr_array<M, A> make_hazard_pointer_array();

  const void* hazptr() const noexcept {
    return hazptr_.load(std::memory_order_acquire);
  }

  FOLLY_ALWAYS_INLINE void reset_hazptr(const void* p = nullptr) noexcept {
    hazptr_.store(p, std::memory_order_release);
  }

  hazptr_rec<Atom>* next() { return next_; }

  hazptr_rec<Atom>* next_avail() { return nextAvail_; }

  void set_next(hazptr_rec<Atom>* rec) { next_ = rec; }

  void set_next_avail(hazptr_rec<Atom>* rec) { nextAvail_ = rec; }

  FOLLY_ALWAYS_INLINE hazptr_domain<Atom>* domain() { return domain_; }

  void set_domain(hazptr_domain<Atom>* dom) { domain_ = dom; }
}; // hazptr_rec

} // namespace folly
