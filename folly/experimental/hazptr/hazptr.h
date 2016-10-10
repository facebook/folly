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

/** hazptr_obj: Private class for objects protected by hazard pointers. */
class hazptr_obj;

/** hazptr_obj_base: Base template for objects protected by hazard pointers. */
template <typename T, typename Deleter>
class hazptr_obj_base;

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

  void try_reclaim();

 private:
  template <typename, typename>
  friend class hazptr_obj_base;
  template <typename> friend class hazptr_owner;

  /** Constant -- May be changed to parameter in the future */
  enum { kScanThreshold = 3 };

  memory_resource* mr_;
  std::atomic<hazptr_rec*> hazptrs_ = {nullptr};
  std::atomic<hazptr_obj*> retired_ = {nullptr};
  std::atomic<int> hcount_ = {0};
  std::atomic<int> rcount_ = {0};

  void objRetire(hazptr_obj*);
  hazptr_rec* hazptrAcquire();
  void hazptrRelease(hazptr_rec*) noexcept;
  int pushRetired(hazptr_obj* head, hazptr_obj* tail, int count);
  void tryBulkReclaim();
  void bulkReclaim();
};

/** Get the default hazptr_domain */
hazptr_domain& default_hazptr_domain();

/** Definition of hazptr_obj */
class hazptr_obj {
  friend class hazptr_domain;
  template <typename, typename>
  friend class hazptr_obj_base;

  void (*reclaim_)(hazptr_obj*);
  hazptr_obj* next_;
  const void* getObjPtr() const;
};

/** Definition of hazptr_obj_base */
template <typename T, typename Deleter = std::default_delete<T>>
class hazptr_obj_base : private hazptr_obj {
 public:
  /* Policy for storing retired objects */
  enum class storage_policy { priv, shared };

  /* Retire a removed object and pass the responsibility for
   * reclaiming it to the hazptr library */
  void retire(
      hazptr_domain& domain = default_hazptr_domain(),
      Deleter reclaim = {},
      const storage_policy policy = storage_policy::shared);

 private:
  Deleter deleter_;
};

/** hazptr_owner: Template for automatic acquisition and release of
 *  hazard pointers, and interface for hazard pointer operations. */
template <typename T> class hazptr_owner {
 public:
  /* Policy for caching hazard pointers */
  enum class cache_policy { cache, nocache };

  /* Constructor automatically acquires a hazard pointer. */
  explicit hazptr_owner(
      hazptr_domain& domain = default_hazptr_domain(),
      const cache_policy policy = cache_policy::nocache);

  /* Destructor automatically clears and releases the owned hazard pointer. */
  ~hazptr_owner();

  /* Copy and move constructors and assignment operators are
   * disallowed because:
   * - Each hazptr_owner owns exactly one hazard pointer at any time.
   * - Each hazard pointer may have up to one owner at any time. */
  hazptr_owner(const hazptr_owner&) = delete;
  hazptr_owner(hazptr_owner&&) = delete;
  hazptr_owner& operator=(const hazptr_owner&) = delete;
  hazptr_owner& operator=(hazptr_owner&&) = delete;

  /** Hazard pointer operations */
  /* Returns a protected pointer from the source */
  T* get_protected(const std::atomic<T*>& src) noexcept;
  /* Return true if successful in protecting ptr if src == ptr after
   * setting the hazard pointer.  Otherwise sets ptr to src. */
  bool try_protect(T*& ptr, const std::atomic<T*>& src) noexcept;
  /* Set the hazard pointer to ptr */
  void set(const T* ptr) noexcept;
  /* Clear the hazard pointer */
  void clear() noexcept;

  /* Swap ownership of hazard ponters between hazptr_owner-s. */
  /* Note: The owned hazard pointers remain unmodified during the swap
   * and continue to protect the respective objects that they were
   * protecting before the swap, if any. */
  void swap(hazptr_owner&) noexcept;

 private:
  hazptr_domain* domain_;
  hazptr_rec* hazptr_;
};

template <typename T>
void swap(hazptr_owner<T>&, hazptr_owner<T>&) noexcept;

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
