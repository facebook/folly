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

#include <memory>
#include <mutex>

#include <folly/Function.h>
#include <folly/synchronization/Baton.h>

#include <glog/logging.h>

namespace folly {

template <typename T>
class MasterPtr;

template <typename T>
class MasterPtrRef;

/**
 * EnableMasterFromThis provides an object with appropriate access to the
 * functionality of the MasterPtr holding this.
 */
template <typename T>
class EnableMasterFromThis {
  template <class O>
  static void set(
      EnableMasterFromThis<O>* that,
      const std::shared_ptr<std::shared_ptr<O>>& outerPtrShared) {
    that->outerPtrWeak_ = outerPtrShared;
    that->lockedPtrWeak_ = *outerPtrShared;
  }
  template <class O>
  static auto set(O*, const std::shared_ptr<std::shared_ptr<T>>&) ->
      typename std::enable_if<
          !std::is_base_of<EnableMasterFromThis<T>, O>::value>::type {}

 public:
  // Gets a non-owning reference to the pointer. MasterPtr::join() does *NOT*
  // wait for outstanding MasterPtrRef objects to be released.
  MasterPtrRef<T> masterRefFromThis() {
    return MasterPtrRef<T>(outerPtrWeak_);
  }

  // Gets a non-owning reference to the pointer. MasterPtr::join() does *NOT*
  // wait for outstanding MasterPtrRef objects to be released.
  MasterPtrRef<const T> masterRefFromThis() const {
    return MasterPtrRef<const T>(outerPtrWeak_);
  }

  // Attempts to lock a pointer to this. Returns null if pointer is not set or
  // if join() was called (even if the call to join() hasn't returned yet).
  std::shared_ptr<T> masterLockFromThis() {
    if (auto outerPtr = outerPtrWeak_.lock()) {
      return *outerPtr;
    }
    return nullptr;
  }
  // Attempts to lock a pointer to this. Returns null if pointer is not set or
  // if join() was called (even if the call to join() hasn't returned yet).
  std::shared_ptr<T const> masterLockFromThis() const {
    if (auto outerPtr = outerPtrWeak_.lock()) {
      return *outerPtr;
    }
    return nullptr;
  }

  // returns the cached weak_ptr<T>
  std::weak_ptr<T> masterWeakFromThis() noexcept {
    return lockedPtrWeak_;
  }
  // returns the cached weak_ptr<T>
  std::weak_ptr<T const> masterWeakFromThis() const noexcept {
    return lockedPtrWeak_;
  }

 private:
  template <class>
  friend class MasterPtr;

  std::weak_ptr<std::shared_ptr<T>> outerPtrWeak_;
  std::weak_ptr<T> lockedPtrWeak_;
};

/**
 * MasterPtr should be used to achieve deterministic destruction of objects with
 * shared ownership.
 * Once an object is managed by a MasterPtr, shared_ptrs can be obtained
 * pointing to that object. However destroying those shared_ptrs will never call
 * the object destructor inline. To destroy the object, join() method should be
 * called on MasterPtr which will wait for all shared_ptrs to be released and
 * then call the object destructor inline.
 */
template <typename T>
class MasterPtr {
 public:
  MasterPtr() = delete;
  template <class T2, class Deleter>
  MasterPtr(std::unique_ptr<T2, Deleter> ptr) {
    set(std::move(ptr));
  }
  ~MasterPtr() {
    if (*this) {
      LOG(FATAL) << "MasterPtr has to be joined explicitly.";
    }
  }

  explicit operator bool() const {
    return !!innerPtr_;
  }

  // Attempts to lock a pointer. Returns null if pointer is not set or if join()
  // was called (even if the call to join() hasn't returned yet).
  std::shared_ptr<T> lock() const {
    if (!*this) {
      return nullptr;
    }
    if (auto outerPtr = outerPtrWeak_.lock()) {
      return *outerPtr;
    }
    return nullptr;
  }

  // Waits until all the refereces obtained via lock() are released. Then
  // destroys the object in the current thread.
  // Can not be called concurrently with set().
  void join() {
    if (!*this) {
      return;
    }

    outerPtrShared_.reset();
    joinBaton_.wait();
    innerPtr_.reset();
  }

  // Sets the pointer. Can not be called concurrently with lock() or join() or
  // ref().
  template <class T2, class Deleter>
  void set(std::unique_ptr<T2, Deleter> ptr) {
    if (*this) {
      LOG(FATAL) << "MasterPtr has to be joined before being set.";
    }

    if (!ptr) {
      return;
    }

    auto rawPtr = ptr.get();
    innerPtr_ = std::unique_ptr<T, folly::Function<void(T*)>>{
        ptr.release(),
        [d = ptr.get_deleter(), rawPtr](T*) mutable { d(rawPtr); }};
    joinBaton_.reset();
    auto innerPtrShared =
        std::shared_ptr<T>(rawPtr, [&](T*) { joinBaton_.post(); });
    outerPtrShared_ =
        std::make_shared<std::shared_ptr<T>>(std::move(innerPtrShared));
    outerPtrWeak_ = outerPtrShared_;
    EnableMasterFromThis<T>::set(rawPtr, outerPtrShared_);
  }

  // Gets a non-owning reference to the pointer. join() does *NOT* wait for
  // outstanding MasterPtrRef objects to be released.
  MasterPtrRef<T> ref() const {
    return MasterPtrRef<T>(outerPtrWeak_);
  }

 private:
  friend class MasterPtrRef<T>;
  folly::Baton<> joinBaton_;
  std::shared_ptr<std::shared_ptr<T>> outerPtrShared_;
  std::weak_ptr<std::shared_ptr<T>> outerPtrWeak_;
  std::unique_ptr<T, folly::Function<void(T*)>> innerPtr_;
};

/**
 * MasterPtrRef is a non-owning reference to the pointer. MasterPtr::join()
 * does *NOT* wait for outstanding MasterPtrRef objects to be released.
 */
template <typename T>
class MasterPtrRef {
 public:
  // Attempts to lock a pointer. Returns null if pointer is not set or if
  // join() was called (even if the call to join() hasn't returned yet).
  std::shared_ptr<T> lock() const {
    if (auto outerPtr = outerPtrWeak_.lock()) {
      return *outerPtr;
    }
    return nullptr;
  }

 private:
  template <class>
  friend class EnableMasterFromThis;
  template <class>
  friend class MasterPtr;
  /* implicit */ MasterPtrRef(std::weak_ptr<std::shared_ptr<T>> outerPtrWeak)
      : outerPtrWeak_(std::move(outerPtrWeak)) {}

  std::weak_ptr<std::shared_ptr<T>> outerPtrWeak_;
};

} // namespace folly
