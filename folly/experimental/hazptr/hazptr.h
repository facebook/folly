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
#pragma once
#define HAZPTR_H

#include <atomic>
#include <functional>
#include <memory>

/* Stand-in for std::pmr::memory_resource */
#include <folly/experimental/hazptr/memory_resource.h>

namespace folly {
namespace hazptr {

/** hazptr_rec: Private class that contains hazard pointers. */
class hazptr_rec;

/** hazptr_obj_base: Base template for objects protected by hazard pointers. */
template <typename T> class hazptr_obj_base;

/** Alias for object reclamation function template */
template <typename T> using hazptr_obj_reclaim = std::function<void(T*)>;

/** hazptr_domain: Class of hazard pointer domains. Each domain manages a set
 *  of hazard pointers and a set of retired objects. */
class hazptr_domain {
 public:
  constexpr explicit hazptr_domain(
      memory_resource* = get_default_resource()) noexcept;
  ~hazptr_domain();

  hazptr_domain(const hazptr_domain&) = delete;
  hazptr_domain(hazptr_domain&&) = delete;
  hazptr_domain& operator=(const hazptr_domain&) = delete;
  hazptr_domain& operator=(hazptr_domain&&) = delete;

  /* Reclaim all retired objects with a specific reclamation
   * function currently stored by this domain */
  template <typename T> void flush(const hazptr_obj_reclaim<T>* reclaim);
  /* Reclaim all retired objects currently stored by this domain  */
  void flush();

 private:
  template <typename> friend class hazptr_obj_base;
  template <typename> friend class hazptr_owner;

  using hazptr_obj = hazptr_obj_base<void>;

  /** Constant -- May be changed to parameter in the future */
  enum { kScanThreshold = 3 };

  memory_resource* mr_;
  std::atomic<hazptr_rec*> hazptrs_ = {nullptr};
  std::atomic<hazptr_obj*> retired_ = {nullptr};
  std::atomic<int> hcount_ = {0};
  std::atomic<int> rcount_ = {0};

  template <typename T> void objRetire(hazptr_obj_base<T>*);
  hazptr_rec* hazptrAcquire();
  void hazptrRelease(hazptr_rec*) const noexcept;
  void objRetire(hazptr_obj*);
  int pushRetired(hazptr_obj* head, hazptr_obj* tail, int count);
  void bulkReclaim();
  void flush(const hazptr_obj_reclaim<void>* reclaim);
};

/** Get the default hazptr_domain */
hazptr_domain* default_hazptr_domain();

/** Declaration of default reclamation function template */
template <typename T> hazptr_obj_reclaim<T>* default_hazptr_obj_reclaim();

/** Definition of hazptr_obj_base */
template <typename T> class hazptr_obj_base {
 public:
  /* Policy for storing retired objects */
  enum class storage_policy { priv, shared };

  /* Retire a removed object and pass the responsibility for
   * reclaiming it to the hazptr library */
  void retire(
      hazptr_domain* domain = default_hazptr_domain(),
      const hazptr_obj_reclaim<T>* reclaim = default_hazptr_obj_reclaim<T>(),
      const storage_policy policy = storage_policy::shared);

 private:
  friend class hazptr_domain;
  template <typename> friend class hazptr_owner;

  const hazptr_obj_reclaim<T>* reclaim_;
  hazptr_obj_base* next_;
};

/** hazptr_owner: Template for automatic acquisition and release of
 *  hazard pointers, and interface for hazard pointer operations. */
template <typename T> class hazptr_owner;

/* Swap ownership of hazard ponters between hazptr_owner-s. */
/* Note: The owned hazard pointers remain unmodified during the swap
 * and continue to protect the respective objects that they were
 * protecting before the swap, if any. */
template <typename T>
void swap(hazptr_owner<T>&, hazptr_owner<T>&) noexcept;

template <typename T> class hazptr_owner {
 public:
  /* Policy for caching hazard pointers */
  enum class cache_policy { cache, nocache };

  /* Constructor automatically acquires a hazard pointer. */
  explicit hazptr_owner(
      hazptr_domain* domain = default_hazptr_domain(),
      const cache_policy policy = cache_policy::nocache);

  /* Destructor automatically clears and releases the owned hazard pointer. */
  ~hazptr_owner() noexcept;

  /* Copy and move constructors and assignment operators are
   * disallowed because:
   * - Each hazptr_owner owns exactly one hazard pointer at any time.
   * - Each hazard pointer may have up to one owner at any time. */
  hazptr_owner(const hazptr_owner&) = delete;
  hazptr_owner(hazptr_owner&&) = delete;
  hazptr_owner& operator=(const hazptr_owner&) = delete;
  hazptr_owner& operator=(hazptr_owner&&) = delete;

  /** Hazard pointer operations */
  /* Return true if successful in protecting the object */
  bool protect(const T* ptr, const std::atomic<T*>& src) const noexcept;
  /* Set the hazard pointer to ptr */
  void set(const T* ptr) const noexcept;
  /* Clear the hazard pointer */
  void clear() const noexcept;

 private:
  friend void swap<T>(hazptr_owner&, hazptr_owner&) noexcept;

  hazptr_domain* domain_;
  hazptr_rec* hazptr_;
};

/** hazptr_user: Thread-specific interface for users of hazard
 *  pointers (i.e., threads that own hazard pointers by using
 *  hazptr_owner. */
class hazptr_user {
 public:
  /* Release all hazptr_rec-s cached by this thread */
  static void flush();
};

/** hazptr_remover: Thread-specific interface for removers of objects
 *  protected by hazard pointersd, i.e., threads that call the retire
 *  member function of hazptr_obj_base. */
class hazptr_remover {
 public:
  /* Pass responsibility of reclaiming any retired objects stored
   * privately by this thread to the hazptr_domain to which they
   * belong. */
  static void flush();
};

} // namespace hazptr
} // namespace folly

////////////////////////////////////////////////////////////////////////////////
/// Notes
////////////////////////////////////////////////////////////////////////////////

/* The hazptr_obj_base template uses a reclamation function as a
 * parameter for the retire() member function instead of taking an
 * allocator template as an extra template parameter because objects
 * of the same type may need different reclamation functions. */

/* The hazptr interface supports reclamation by one domain at a
 * time. If an abject belongs to multiple domains simultaneously, a
 * workaround may be to design reclamation functions to form a series
 * of retirements from one domain to the next until it reaches the
 * final domain in the series that finally reclaims the object. */

////////////////////////////////////////////////////////////////////////////////

#include "hazptr-impl.h"
