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

/**
 * This module implements a Locked abstraction useful in mutex-based
 * concurrency.
 *
 * The Locked<T, Mutex> is the primary public API exposed by this module.
 * See its class documentation for more information about how to use this
 * functionality.
 */
#pragma once

#include <mutex>
#include <type_traits>

#include <folly/LockTraits.h>
#include <folly/SharedMutex.h>
#include <glog/logging.h>

namespace folly {

namespace detail {
class LockExclusive;
class LockShared;
class LockSharedOrExclusive;
}

template <class LockedType, class Mutex, class LockPolicy>
class LockedPtrBase;
template <class LockedType, class LockPolicy>
class LockedPtr;
template <class LockedType, class LockPolicy = detail::LockExclusive>
class LockedGuardPtr;

template <class T, class Mutex>
class Locked;

/**
 * RWLocked is a Locked object using folly::SharedMutex as the
 * default lock type.
 */
template <class T, class Mutex = folly::SharedMutex>
using RWLocked = Locked<T, Mutex>;

/**
 * LockedBase is a helper parent class for Locked<T>.
 *
 * It provides wlock() and rlock() methods for shared mutex types,
 * or lock() methods for purely exclusive mutex types.
 */
template <class Subclass, bool is_shared>
class LockedBase;

/**
 * LockedBase specialization for shared mutex types.
 *
 * This class provides wlock() and rlock() methods for acquiring the lock and
 * accessing the data.
 */
template <class Subclass>
class LockedBase<Subclass, true> {
 public:
  using LockedPtr = ::folly::LockedPtr<Subclass, detail::LockExclusive>;
  using ConstWLockedPtr =
      ::folly::LockedPtr<const Subclass, detail::LockExclusive>;
  using ConstLockedPtr = ::folly::LockedPtr<const Subclass, detail::LockShared>;

  /**
   * Acquire an exclusive lock, and return a LockedPtr that can be used to
   * safely access the datum.
   *
   * LockedPtr offers operator -> and * to provide access to the datum.
   * The lock will be released when the LockedPtr is destroyed.
   */
  LockedPtr wlock() {
    return LockedPtr(static_cast<Subclass*>(this));
  }
  ConstWLockedPtr wlock() const {
    return ConstWLockedPtr(static_cast<Subclass*>(this));
  }

  /**
   * Acquire a read lock, and return a ConstLockedPtr that can be used to
   * safely access the datum.
   */
  ConstLockedPtr rlock() const {
    return ConstLockedPtr(static_cast<const Subclass*>(this));
  }

  /**
   * Attempts to acquire the lock for up to the given number of milliseconds.
   * If acquisition is unsuccessful, the returned LockedPtr will be invalid.
   *
   * (Use LockedPtr::isValid() to check for validity.)
   */
  LockedPtr wlock(std::chrono::milliseconds timeout) {
    return LockedPtr(static_cast<Subclass*>(this), timeout);
  }
  ConstWLockedPtr wlock(std::chrono::milliseconds timeout) const {
    return ConstWLockedPtr(static_cast<const Subclass*>(this), timeout);
  }

  /**
   * Attempts to acquire the lock for up to the given number of milliseconds.
   * If acquisition is unsuccessful, the returned LockedPtr will be invalid.
   *
   * (Use LockedPtr::isValid() to check for validity.)
   */
  ConstLockedPtr rlock(std::chrono::milliseconds timeout) const {
    return ConstLockedPtr(static_cast<const Subclass*>(this), timeout);
  }

  /**
   * Invoke a function while holding th lock exclusively.
   *
   * A reference to the datum will be passed into the function as its only
   * argument.
   *
   * This can be used with a lambda argument for easily defining small critical
   * sections in the code.  For example:
   *
   *   auto value = obj.withWLock([](auto& data) {
   *     data.doStuff();
   *     return data.getValue();
   *   });
   */
  template <class Function>
  auto withWLock(Function&& function) {
    LockedGuardPtr<Subclass, detail::LockExclusive> guardPtr(
        static_cast<Subclass*>(this));
    return function(*guardPtr);
  }
  template <class Function>
  auto withWLock(Function&& function) const {
    LockedGuardPtr<const Subclass, detail::LockExclusive> guardPtr(
        static_cast<const Subclass*>(this));
    return function(*guardPtr);
  }

  /**
   * Invoke a function while holding th lock exclusively.
   *
   * This is similar to withWLock(), but the function will be passed a
   * LockedPtr rather than a reference to the data itself.
   *
   * This allows scopedUnlock() to be called on the LockedPtr argument if
   * desired.
   */
  template <class Function>
  auto withWLockPtr(Function&& function) {
    return function(wlock());
  }
  template <class Function>
  auto withWLockPtr(Function&& function) const {
    return function(wlock());
  }

  /**
   * Invoke a function while holding an the lock in shared mode.
   *
   * A const reference to the datum will be passed into the function as its
   * only argument.
   */
  template <class Function>
  auto withRLock(Function&& function) const {
    LockedGuardPtr<const Subclass, detail::LockShared> guardPtr(
        static_cast<const Subclass*>(this));
    return function(*guardPtr);
  }

  template <class Function>
  auto withRLockPtr(Function&& function) const {
    return function(rlock());
  }
};

/**
 * LockedBase specialization for non-shared mutex types.
 *
 * This class provides lock() methods for acquiring the lock and accessing the
 * data.
 */
template <class Subclass>
class LockedBase<Subclass, false> {
 public:
  using LockedPtr = ::folly::LockedPtr<Subclass, detail::LockExclusive>;
  using ConstLockedPtr =
      ::folly::LockedPtr<const Subclass, detail::LockExclusive>;

  /**
   * Acquire a lock, and return a LockedPtr that can be used to safely access
   * the datum.
   */
  LockedPtr lock() {
    return LockedPtr(static_cast<Subclass*>(this));
  }

  /**
   * Acquire a lock, and return a ConstLockedPtr that can be used to safely
   * access the datum.
   */
  ConstLockedPtr lock() const {
    return ConstLockedPtr(static_cast<const Subclass*>(this));
  }

  /**
   * Attempts to acquire the lock for up to the given number of milliseconds.
   * If acquisition is unsuccessful, the returned LockedPtr will be invalid.
   */
  LockedPtr lock(std::chrono::milliseconds timeout) {
    return LockedPtr(static_cast<Subclass*>(this), timeout);
  }

  /**
   * Attempts to acquire the lock for up to the given number of milliseconds.
   * If acquisition is unsuccessful, the returned LockedPtr will be invalid.
   */
  ConstLockedPtr lock(std::chrono::milliseconds timeout) const {
    return ConstLockedPtr(static_cast<const Subclass*>(this), timeout);
  }

  /**
   * Invoke a function while holding the lock.
   *
   * A reference to the datum will be passed into the function as its only
   * argument.
   *
   * This can be used with a lambda argument for easily defining small critical
   * sections in the code.  For example:
   *
   *   auto value = obj.withLock([](auto& data) {
   *     data.doStuff();
   *     return data.getValue();
   *   });
   */
  template <class Function>
  auto withLock(Function&& function) {
    LockedGuardPtr<Subclass, detail::LockExclusive> guardPtr(
        static_cast<Subclass*>(this));
    return function(*guardPtr);
  }
  template <class Function>
  auto withLock(Function&& function) const {
    LockedGuardPtr<const Subclass, detail::LockExclusive> guardPtr(
        static_cast<const Subclass*>(this));
    return function(*guardPtr);
  }

  /**
   * Invoke a function while holding th lock exclusively.
   *
   * This is similar to withWLock(), but the function will be passed a
   * LockedPtr rather than a reference to the data itself.
   *
   * This allows scopedUnlock() and getUniqueLock() to be called on the
   * LockedPtr argument.
   */
  template <class Function>
  auto withLockPtr(Function&& function) {
    return function(lock());
  }
  template <class Function>
  auto withLockPtr(Function&& function) const {
    return function(lock());
  }
};

/**
 * Locked<T> encapsulates an object of type T (a "datum") paired
 * with a mutex. The only way to access the datum is while the mutex
 * is locked, and Locked makes it virtually impossible to do
 * otherwise. The code that would access the datum in unsafe ways
 * would look odd and convoluted, thus readily alerting the human
 * reviewer. In contrast, the code that uses Locked<T> correctly
 * looks simple and intuitive.
 *
 * The second parameter must be a mutex type.  Any mutex type supported by
 * LockTraits<Mutex> can be used.  By default any class with lock() and
 * unlock() methods will work automatically.  LockTraits can be specialized to
 * teach Locked how to use other custom mutex types.  See the documentation in
 * LockTraits.h for additional details.
 *
 * Supported mutexes that work by default include std::mutex,
 * std::recursive_mutex, std::timed_mutex, std::recursive_timed_mutex,
 * folly::SharedMutex, folly::RWSpinLock, folly::SpinLock,
 * boost::mutex, boost::recursive_mutex, boost::shared_mutex,
 * boost::timed_mutex, and boost::recursive_timed_mutex.
 */
template <class T, class Mutex = std::mutex>
class Locked
    : public LockedBase<Locked<T, Mutex>, LockTraits<Mutex>::is_shared> {
 private:
  /*
   * Helpers to indicate if the copy and move constructors are noexcept.
   */
  static constexpr bool nxCopyCtor{
      std::is_nothrow_copy_constructible<T>::value};
  static constexpr bool nxMoveCtor{
      std::is_nothrow_move_constructible<T>::value};

 public:
  using DataType = T;
  using MutexType = Mutex;

  /**
   * Default constructor leaves both members call their own default
   * constructor.
   */
  Locked() = default;

  /**
   * Constructor taking a datum as argument copies it. There is no
   * need to lock the constructing object.
   */
  explicit Locked(const T& rhs) noexcept(nxCopyCtor) : datum_(rhs) {}

  /**
   * Constructor taking a datum rvalue as argument moves it. Again,
   * there is no need to lock the constructing object.
   */
  explicit Locked(T&& rhs) noexcept(nxMoveCtor) : datum_(std::move(rhs)) {}

  /**
   * Lets you construct non-movable types in-place.
   */
  template <typename... Args>
  explicit Locked(Args&&... args) : datum_(std::forward<Args>(args)...) {}

  /**
   * Lock object, assign datum.
   */
  Locked& operator=(const T& rhs) {
    auto guard = contextualLock();
    datum_ = rhs;
    return *this;
  }

  /**
   * Lock object, move-assign datum.
   */
  Locked& operator=(T&& rhs) {
    auto guard = contextualLock();
    datum_ = std::move(rhs);
    return *this;
  }

  /*
   * Note that our parent LockedBase class provides wlock() and rlock() methods
   * if MutexType is a read-write mutex, or a lock() method if MutexType is a
   * purely exclusive lock.
   */

  /**
   * Acquire an appropriate lock based on the context.
   *
   * If the mutex is a read-write lock, and the locked object is const,
   * this acquires a shared lock.  Otherwise this acquires an exclusive lock.
   *
   * In general, prefer using the explicit rlock() and wlock() methods
   * for read-write locks, and lock() for purely exclusive locks.
   *
   * contextualLock() is primarily intended for use in other template functions
   * that do not necessarily know the lock type.
   */
  LockedPtr<Locked<T, Mutex>, detail::LockExclusive> contextualLock() {
    return LockedPtr<Locked<T, Mutex>, detail::LockExclusive>(this);
  }
  LockedPtr<const Locked<T, Mutex>, detail::LockSharedOrExclusive>
  contextualLock() const {
    return LockedPtr<const Locked<T, Mutex>, detail::LockSharedOrExclusive>(
        this);
  }
  LockedPtr<Locked<T, Mutex>, detail::LockExclusive> contextualLock(
      std::chrono::milliseconds timeout) {
    return LockedPtr<Locked<T, Mutex>, detail::LockExclusive>(this, timeout);
  }
  LockedPtr<const Locked<T, Mutex>, detail::LockSharedOrExclusive>
  contextualLock(std::chrono::milliseconds timeout) const {
    return LockedPtr<const Locked<T, Mutex>, detail::LockSharedOrExclusive>(
        this, timeout);
  }
  /**
   * contextualRLock() acquires a read lock if the mutex type is shared,
   * or a regular exclusive lock for non-shared mutex types.
   *
   * contextualRLock() when you know that you prefer a read lock (if
   * available), even if the Locked<T> object itself is non-const.
   */
  LockedPtr<const Locked<T, Mutex>, detail::LockSharedOrExclusive>
  contextualRLock() const {
    return LockedPtr<const Locked<T, Mutex>, detail::LockSharedOrExclusive>(
        this);
  }
  LockedPtr<const Locked<T, Mutex>, detail::LockSharedOrExclusive>
  contextualRLock(std::chrono::milliseconds timeout) const {
    return LockedPtr<const Locked<T, Mutex>, detail::LockSharedOrExclusive>(
        this, timeout);
  }

  /**
   * Swap data with another Locked<T> object.
   *
   * Locks are acquired in increasing address order.
   */
  void swapData(Locked& rhs) {
    if (this == &rhs) {
      return;
    }
    if (this > &rhs) {
      return rhs.swapData(*this);
    }
    auto guard = contextualLock();
    auto rhsGuard = rhs.contextualLock();

    using std::swap;
    swap(datum_, rhs.datum_);
  }

  /**
   * Swap with another datum.
   */
  void swapData(T& rhs) {
    auto guard = contextualLock();

    using std::swap;
    swap(datum_, rhs);
  }

  /**
   * Copies datum to a given target.
   */
  void copy(T* target) const {
    auto guard = contextualLock();
    *target = datum_;
  }

  /**
   * Returns a fresh copy of the datum.
   */
  T copy() const {
    auto guard = contextualLock();
    return datum_;
  }

 private:
  template <class LockedType, class MutexType, class LockPolicy>
  friend class folly::LockedPtrBase;
  template <class LockedType, class LockPolicy>
  friend class folly::LockedPtr;
  template <class LockedType, class LockPolicy>
  friend class folly::LockedGuardPtr;

  /**
   * Forbidden copy and move constructors and assignment operators.
   *
   * These are deleted since mutex objects cannot be copied or moved.
   *
   * It conceivably might be reasonable to support a copy constructor that
   * copies the data and not the mutex.  (The older Synchronized class in fact
   * did this for both copy and move construction and assignment.)  However, it
   * seems like this has a large potential for surprising behaviors.  (For
   * instance, it would allow std::vector<Locked<T>>, but this vector can't
   * actually be used sanely without holding a global lock for the duration of
   * pretty much any access, to ensure that the Locked objects aren't copied or
   * moved out from users in other threads if the vector is resized.)
   *
   * We could consider adding support for these operators in the future if we
   * find a compelling use case, but for now they seem too dangerous.
   */
  Locked(const Locked&) = delete;
  Locked(Locked&&) = delete;
  Locked& operator=(const Locked&) = delete;
  Locked& operator=(Locked&&) = delete;

  T datum_;
  mutable Mutex mutex_;
};

template <class LockedType, class LockPolicy>
class ScopedUnlocker;

/**
 * A helper base class for implementing LockedPtr.
 *
 * The main reason for having this as a separate class is so we can specialize
 * it for std::mutex, so we can expose a std::unique_lock to the caller
 * when std::mutex is being used.  This allows callers to use a
 * std::condition_variable with the mutex from a Locked<T, std::mutex>.
 *
 * Note that the LockedType template parameter may or may not be const
 * qualified.
 */
template <class LockedType, class Mutex, class LockPolicy>
class LockedPtrBase {
 public:
  using MutexType = Mutex;
  friend class folly::ScopedUnlocker<LockedType, LockPolicy>;

  /**
   * Destructor releases.
   */
  ~LockedPtrBase() {
    if (parent_) {
      LockPolicy::unlock(parent_->mutex_);
    }
  }

 protected:
  LockedPtrBase() {}
  explicit LockedPtrBase(LockedType* parent) : parent_(parent) {
    LockPolicy::lock(parent_->mutex_);
  }
  LockedPtrBase(LockedType* parent, std::chrono::milliseconds timeout) {
    if (LockPolicy::try_lock_for(parent->mutex_, timeout)) {
      this->parent_ = parent;
    }
  }
  LockedPtrBase(LockedPtrBase&& rhs) noexcept : parent_(rhs.parent_) {
    rhs.parent_ = nullptr;
  }
  LockedPtrBase& operator=(LockedPtrBase&& rhs) noexcept {
    if (parent_) {
      LockPolicy::unlock(parent_->mutex_);
    }

    parent_ = rhs.parent_;
    rhs.parent_ = nullptr;
  }

  using Data = LockedType*;

  Data releaseLock() {
    auto current = parent_;
    parent_ = nullptr;
    LockPolicy::unlock(current->mutex_);
    return current;
  }
  void reacquireLock(Data&& data) {
    DCHECK(parent_ == nullptr);
    parent_ = data;
    LockPolicy::lock(parent_->mutex_);
  }

  LockedType* parent_ = nullptr;
};

/**
 * LockedPtrBase specialization for use with std::mutex.
 *
 * When std::mutex is used we use a std::unique_lock to hold the mutex.
 * This makes it possible to use std::condition_variable with a
 * Locked<T, std::mutex>.
 */
template <class LockedType, class LockPolicy>
class LockedPtrBase<LockedType, std::mutex, LockPolicy> {
 public:
  using MutexType = std::mutex;
  friend class folly::ScopedUnlocker<LockedType, LockPolicy>;

  /**
   * Destructor releases.
   */
  ~LockedPtrBase() {
    // The std::unique_lock will automatically release the lock when it is
    // destroyed, so we don't need to do anything extra here.
  }

  LockedPtrBase(LockedPtrBase&& rhs) noexcept
      : lock_(std::move(rhs.lock_)), parent_(rhs.parent_) {
    rhs.parent_ = nullptr;
  }
  LockedPtrBase& operator=(LockedPtrBase&& rhs) noexcept {
    lock_ = std::move(rhs.lock_);
    parent_ = rhs.parent_;
    rhs.parent_ = nullptr;
  }

  /*
   * Get a reference to the std::unique_lock.
   *
   * This is providede so that callers can use Locked<T, std::mutex>
   * with a std::condition_variable.
   *
   * While this API could be used to bypass the normal Locked APIs and manually
   * interact with the underlying unique_lock, this is strongly discouraged.
   */
  std::unique_lock<std::mutex>& getUniqueLock() {
    return lock_;
  }

 protected:
  LockedPtrBase() {}
  explicit LockedPtrBase(LockedType* parent)
      : lock_(parent->mutex_), parent_(parent) {}

  using Data = std::pair<std::unique_lock<std::mutex>, LockedType*>;

  Data releaseLock() {
    Data data(std::move(lock_), parent_);
    parent_ = nullptr;
    data.first.unlock();
    return data;
  }
  void reacquireLock(Data&& data) {
    lock_ = std::move(data.first);
    lock_.lock();
    parent_ = data.second;
  }

  // The specialization for std::mutex does have to store slightly more
  // state than the default implementation.
  std::unique_lock<std::mutex> lock_;
  LockedType* parent_ = nullptr;
};

/**
 * This class temporarily unlocks a LockedPtr in a scoped manner.
 */
template <class LockedType, class LockPolicy>
class ScopedUnlocker {
 public:
  explicit ScopedUnlocker(LockedPtr<LockedType, LockPolicy>* p)
      : ptr_(p), data_(ptr_->releaseLock()) {}
  ScopedUnlocker(const ScopedUnlocker&) = delete;
  ScopedUnlocker& operator=(const ScopedUnlocker&) = delete;
  ScopedUnlocker(ScopedUnlocker&& other) noexcept
      : ptr_(other.ptr_), data_(std::move(other.data_)) {
    other.ptr_ = nullptr;
  }
  ScopedUnlocker& operator=(ScopedUnlocker&& other) = delete;

  ~ScopedUnlocker() {
    if (ptr_) {
      ptr_->reacquireLock(std::move(data_));
    }
  }

 private:
  using Data = typename LockedPtr<LockedType, LockPolicy>::LockData;
  LockedPtr<LockedType, LockPolicy>* ptr_{nullptr};
  Data data_;
};

/*
 * Helper class to get a "DataType" from "Locked<DataType>", and
 * "const DataType" from "const Locked<DataType>",
 */
namespace detail {
template <class Locked>
struct LockedDataType {
  using DataType = typename Locked::DataType;
};
template <class T, class Mutex>
struct LockedDataType<const Locked<T, Mutex>> {
  using DataType = typename Locked<T, Mutex>::DataType const;
};
}

/**
 * A LockedPtr keeps a Locked<T> object locked for the duration of
 * LockedPtr's existence.
 *
 * It provides access the datum's members directly by using operator->() and
 * operator*().
 *
 * If the LockedType parameter is a non-const Locked<T> an exclusive lock is
 * acquired.  If the LockedType parameter is a const Locked<T>, then a shared
 * lock is acquired if the mutex supports read/write semantics; if the lock is
 * not a read/write lock then an exclusive lock is acquired.
 */
template <class LockedType, class LockPolicy>
class LockedPtr : public LockedPtrBase<
                      LockedType,
                      typename LockedType::MutexType,
                      LockPolicy> {
 private:
  using Base =
      LockedPtrBase<LockedType, typename LockedType::MutexType, LockPolicy>;
  using LockData = typename Base::Data;
  // CDataType is the DataType with the appropriate const-qualification
  using CDataType = typename detail::LockedDataType<LockedType>::DataType;

 public:
  using DataType = typename LockedType::DataType;
  using MutexType = typename LockedType::MutexType;
  using Locked = typename std::remove_const<LockedType>::type;
  friend class ScopedUnlocker<LockedType, LockPolicy>;

  /**
   * Creates an uninitialized LockedPtr.
   *
   * Dereferencing an uninitialized LockedPtr is not allowed.
   */
  LockedPtr() {}

  /**
   * Takes a Locked<T> and locks it.
   */
  explicit LockedPtr(LockedType* parent) : Base(parent) {}

  /**
   * Takes a Locked<T> and attempts to lock it, within the specified timeout.
   *
   * Blocks until the lock is acquired or until the specified timeout expires.
   * If the timeout expired without acquiring the lock, the LockedPtr will be
   * invalid, and LockedPtr::isValid() will return false.
   */
  LockedPtr(LockedType* parent, std::chrono::milliseconds timeout)
      : Base(parent, timeout) {}

  /**
   * Move constructor.
   */
  LockedPtr(LockedPtr&& rhs) noexcept = default;

  /**
   * Move assignment operator.
   */
  LockedPtr& operator=(LockedPtr&& rhs) noexcept = default;

  /*
   * Copy constructor and assignment operator are deleted.
   */
  LockedPtr(const LockedPtr& rhs) = delete;
  LockedPtr& operator=(const LockedPtr& rhs) = delete;

  /**
   * Destructor releases.
   */
  ~LockedPtr() {}

  /**
   * Check if this LockedPtr is valid or not.
   *
   * In particular, this method must be used to check if a timed-acquire
   * operation succeeded.  If an acquire operation times out it will result in
   * a null LockedPtr.
   */
  bool isValid() const {
    return this->parent_ != nullptr;
  }

  /**
   * Support using the ! operator to check if the LockedPtr is valid.
   *
   * This allows slightly more succinct checks to detect if we failed to
   * acquire a lock within the given timeout, while avoiding the pitfalls of
   * an implicit bool conversion operator.
   */
  bool operator!() const {
    return this->parent_ == nullptr;
  }

  /**
   * Access the locked data.
   *
   * This method should only be used if the LockedPtr is valid.
   */
  CDataType* operator->() const {
    return &this->parent_->datum_;
  }

  /**
   * Access the locked data.
   *
   * This method should only be used if the LockedPtr is valid.
   */
  CDataType& operator*() const {
    return this->parent_->datum_;
  }

  /**
   * Temporarily unlock the LockedPtr.
   *
   * Returns an helper object that will re-lock the LockedPtr when the helper
   * is destroyed.
   */
  ScopedUnlocker<LockedType, LockPolicy> scopedUnlock() {
    return ScopedUnlocker<LockedType, LockPolicy>(this);
  }
};

/**
 * LockedGuardPtr is a simplified version of LockedPtr.
 *
 * It is non-movable, and supports fewer features than LockedPtr.  However, it
 * is ever-so-slightly more performant than LockedPtr.  (The destructor can
 * unconditionally release the lock, without requiring a conditional branch.)
 *
 * The relationship between LockedGuardPtr and LockedPtr is similar to that
 * between std::lock_guard and std::unique_lock.
 */
template <class LockedType, class LockPolicy>
class LockedGuardPtr {
 private:
  // CDataType is the DataType with the appropriate const-qualification
  using CDataType = typename detail::LockedDataType<LockedType>::DataType;

 public:
  using DataType = typename LockedType::DataType;
  using MutexType = typename LockedType::MutexType;
  using Locked = typename std::remove_const<LockedType>::type;

  LockedGuardPtr() = delete;

  /**
   * Takes a Locked<T> and locks it.
   */
  explicit LockedGuardPtr(LockedType* parent) : parent_(parent) {
    LockPolicy::lock(parent_->mutex_);
  }

  /**
   * Destructor releases.
   */
  ~LockedGuardPtr() {
    LockPolicy::unlock(parent_->mutex_);
  }

  /**
   * Access the locked data.
   *
   * This method should only be used if the LockedPtr is valid.
   */
  CDataType* operator->() const {
    return &parent_->datum_;
  }

  /**
   * Access the locked data.
   *
   * This method should only be used if the LockedPtr is valid.
   */
  CDataType& operator*() const {
    return parent_->datum_;
  }

 private:
  // This is the entire state of LockedGuardPtr.
  LockedType* const parent_{nullptr};
};

namespace detail {
template <class Locked>
struct LockedPtrType {
  using type = LockedPtr<Locked, LockExclusive>;
};
template <class T, class Mutex>
struct LockedPtrType<const Locked<T, Mutex>> {
  using type = LockedPtr<const Locked<T, Mutex>, LockSharedOrExclusive>;
};
}

/**
 * Acquire locks for multiple Locked<T> objects, in a deadlock-safe manner.
 *
 * The locks are acquired in order from lowest address to highest address.
 * (Note that this is not necessarily the same algorithm used by std::lock().)
 *
 * For parameters that are const and support shared locks, a read lock is
 * acquired.  Otherwise an exclusive lock is acquired.
 *
 * TODO: Extend acquireLocked() with variadic template versions that
 * allow for more than 2 Locked arguments.  (I haven't given too much thought
 * about how to implement this.  It seems like it would be rather complicated,
 * but I think it should be possible.)
 */
template <class Locked1, class Locked2>
std::tuple<
    typename detail::LockedPtrType<Locked1>::type,
    typename detail::LockedPtrType<Locked2>::type>
acquireLocked(Locked1& l1, Locked2& l2) {
  if (static_cast<const void*>(&l1) < static_cast<const void*>(&l2)) {
    auto p1 = l1.contextualLock();
    auto p2 = l2.contextualLock();
    return std::make_tuple(std::move(p1), std::move(p2));
  } else {
    auto p2 = l2.contextualLock();
    auto p1 = l1.contextualLock();
    return std::make_tuple(std::move(p1), std::move(p2));
  }
}

namespace detail {

/**
 * A lock policy for LockedPtr which performs exclusive lock operations.
 */
class LockExclusive {
 public:
  template <class Mutex>
  static void lock(Mutex& mutex) {
    LockTraits<Mutex>::lock(mutex);
  }
  template <class Mutex, class Rep, class Period>
  static bool try_lock_for(
      Mutex& mutex,
      const std::chrono::duration<Rep, Period>& timeout) {
    return LockTraits<Mutex>::try_lock_for(mutex, timeout);
  }
  template <class Mutex>
  static void unlock(Mutex& mutex) {
    LockTraits<Mutex>::unlock(mutex);
  }
};

/**
 * A lock policy for LockedPtr which performs shared lock operations.
 * This policy only works with shared mutex types.
 */
class LockShared {
 public:
  template <class Mutex>
  static void lock(Mutex& mutex) {
    LockTraits<Mutex>::lock_shared(mutex);
  }
  template <class Mutex, class Rep, class Period>
  static bool try_lock_for(
      Mutex& mutex,
      const std::chrono::duration<Rep, Period>& timeout) {
    return LockTraits<Mutex>::try_lock_shared_for(mutex, timeout);
  }
  template <class Mutex>
  static void unlock(Mutex& mutex) {
    LockTraits<Mutex>::unlock_shared(mutex);
  }
};

/**
 * A lock policy for LockedPtr which performs a shared lock operation if a
 * shared mutex type is given, or a normal exclusive lock operation on
 * non-shared mutex types.
 */
class LockSharedOrExclusive {
 public:
  template <class Mutex>
  static void lock(Mutex& mutex) {
    lock_shared_or_unique(mutex);
  }
  template <class Mutex, class Rep, class Period>
  static bool try_lock_for(
      Mutex& mutex,
      const std::chrono::duration<Rep, Period>& timeout) {
    return try_lock_shared_or_unique_for(mutex, timeout);
  }
  template <class Mutex>
  static void unlock(Mutex& mutex) {
    unlock_shared_or_unique(mutex);
  }
};

} // detail

} // folly
