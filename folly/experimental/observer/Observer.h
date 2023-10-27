/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
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
#include <memory>

#include <folly/SharedMutex.h>
#include <folly/ThreadLocal.h>
#include <folly/experimental/observer/Observer-pre.h>
#include <folly/experimental/observer/detail/Core.h>

namespace folly {
namespace observer {

/**
 * Observer - a library which lets you create objects which track updates of
 * their dependencies and get re-computed when any of the dependencies changes.
 *
 *
 * Given an Observer, you can get a snapshot of the current version of the
 * object it holds:
 *
 *   Observer<int> myObserver = ...;
 *   Snapshot<int> mySnapshot = myObserver.getSnapshot();
 * or simply
 *   Snapshot<int> mySnapshot = *myObserver;
 *
 * Snapshot will hold a view of the object, even if object in the Observer
 * gets updated.
 *
 *
 * What makes Observer powerful is its ability to track updates to other
 * Observers. Imagine we have two separate Observers A and B which hold
 * integers.
 *
 *   Observer<int> observerA = ...;
 *   Observer<int> observerB = ...;
 *
 * To compute a sum of A and B we can create a new Observer which would track
 * updates to A and B and re-compute the sum only when necessary.
 *
 *   Observer<int> sumObserver = makeObserver(
 *       [observerA, observerB] {
 *         int a = **observerA;
 *         int b = **observerB;
 *         return a + b;
 *       });
 *
 *   int sum = **sumObserver;
 *
 * Notice that a + b will be only called when either a or b is changed. Getting
 * a snapshot from sumObserver won't trigger any re-computation.
 *
 * Getting an Observer snapshot involves acquiring a shared_ptr, which can be
 * expensive, especially if several threads do so concurrently. If the cost of
 * getSnapshot() is noticeable, alternative Observer implementations are
 * available, offering different trade-offs:
 *
 * - If T is a type for which std::atomic<T> is lock-free (all word-sized PODs
 *   for example), AtomicObserver and ReadMostlyAtomicObserver offer the best
 *   performance at no additional memory cost.
 *
 * - TLObserver stores a copy of the value for each thread that accesses it,
 *   which avoids any synchronization, but can consume significant amounts of
 *   memory depending on the size of the values.
 *
 * - HazptrObserver uses hazard pointers to protect the snapshot, which offer
 *   high read scalability and low cost, but the snapshot should be held as
 *   little as possible and should not cross coroutine suspension points.
 *
 * - ReadMostlyTLObserver returns a snapshot that can be used like a regular
 *   shared_ptr. Scalability and cost are comparable to HazptrObserver, but the
 *   snapshots can be held for arbitrary time. Memory cost is a small constant
 *   for each thread that acquires a snapshot.
 *
 * - CoreCachedObserver can be used if a std::shared_ptr<T> is strictly
 *   required. Read scalability is comparable to the previous options, but cost
 *   is moderately higher. Memory cost is a small constant for each CPU in the
 *   system.
 *
 * See ObserverCreator class if you want to wrap any existing subscription API
 * in an Observer object.
 */
template <typename T>
class Observer;

/**
 * An AtomicObserver provides read-optimized caching for an Observer using
 * `std::atomic`. Reading only requires atomic loads unless the cached value
 * is stale. If the cache needs to be refreshed, a mutex is used to
 * synchronize the update. This avoids creating a shared_ptr for every read.
 *
 * AtomicObserver models CopyConstructible and MoveConstructible. Copying or
 * moving simply invalidates the cache.
 *
 * AtomicObserver is ideal when there are lots of reads on a trivially-copyable
 * type. if `std::atomic<T>` is not possible but you still want to optimize
 * reads, consider a TLObserver.
 *
 *   Observer<int> observer = ...;
 *   AtomicObserver<int> atomicObserver(observer);
 *   auto value = *atomicObserver;
 */
template <typename T>
class AtomicObserver;

/**
 * A TLObserver provides read-optimized caching for an Observer using
 * thread-local storage. This avoids creating a shared_ptr for every read.
 *
 * The functionality is similar to that of AtomicObserver except it allows types
 * that don't support atomics. If possible, use AtomicObserver instead.
 *
 * TLObserver can consume significant amounts of memory if accessed from many
 * threads. The problem is exacerbated if you chain several TLObservers.
 * Therefore, TLObserver should be used sparingly.
 *
 *   Observer<int> observer = ...;
 *   TLObserver<int> tlObserver(observer);
 *   auto& snapshot = *tlObserver;
 */
template <typename T>
class TLObserver;

/**
 * A ReadMostlyAtomicObserver guarantees that reading is exactly one relaxed
 * atomic load and a read from a thread local bool. Like AtomicObserver, the
 * value is cached using `std::atomic`.  However, there is no version check when
 * reading which means that the cached value may be out-of-date with the
 * Observer value. The cached value will be updated asynchronously in a
 * background thread.
 *
 * When get() is called from makeObserver, the underlying observer is directly
 * snapshotted to ensure dependent observers have current values and capture
 * dependencies.
 *
 * ReadMostlyAtomicObserver is ideal for fastest possible reads on a
 * trivially-copyable type when a slightly out-of-date value will suffice. It is
 * perfect for very frequent reads coupled with very infrequent writes.
 *
 *   Observer<int> observer = ...;
 *   ReadMostlyAtomicObserver<int> atomicObserver(observer);
 *   auto value = *atomicObserver;
 */
template <typename T>
class ReadMostlyAtomicObserver;

template <typename T>
class Snapshot {
 public:
  const T& operator*() const { return *get(); }

  const T* operator->() const { return get(); }

  const T* get() const { return data_.get(); }

  std::shared_ptr<const T> getShared() const& { return data_; }

  std::shared_ptr<const T> getShared() && { return std::move(data_); }

  /**
   * Return the version of the observed object.
   */
  size_t getVersion() const { return version_; }

 private:
  friend class Observer<T>;

  Snapshot(
      const observer_detail::Core& core,
      std::shared_ptr<const T> data,
      size_t version)
      : data_(std::move(data)), version_(version), core_(&core) {
    DCHECK(data_);
  }

  std::shared_ptr<const T> data_;
  size_t version_;
  const observer_detail::Core* core_;
};

class CallbackHandle {
 public:
  CallbackHandle();
  template <typename T>
  CallbackHandle(Observer<T> observer, Function<void(Snapshot<T>)> callback);
  CallbackHandle(const CallbackHandle&) = delete;
  CallbackHandle(CallbackHandle&&) = default;
  CallbackHandle& operator=(const CallbackHandle&) = delete;
  CallbackHandle& operator=(CallbackHandle&&) noexcept;
  ~CallbackHandle();

  // If callback is currently running, waits until it completes.
  // Callback will never be called after cancel() returns.
  void cancel();

 private:
  struct Context;
  std::shared_ptr<Context> context_;
};

template <typename Observable, typename Traits>
class ObserverCreator;

template <typename T>
class Observer {
 public:
  explicit Observer(observer_detail::Core::Ptr core);

  Snapshot<T> getSnapshot() const;
  Snapshot<T> operator*() const { return getSnapshot(); }

  /**
   * Check if we have a newer version of the observed object than the snapshot.
   * Snapshot should have been originally from this Observer.
   */
  bool needRefresh(const Snapshot<T>& snapshot) const {
    DCHECK_EQ(core_.get(), snapshot.core_);
    return needRefresh(snapshot.getVersion());
  }

  bool needRefresh(size_t version) const {
    return version < core_->getVersionLastChange();
  }

  CallbackHandle addCallback(Function<void(Snapshot<T>)> callback) const;

 private:
  template <typename Observable, typename Traits>
  friend class ObserverCreator;

  observer_detail::Core::Ptr core_;
};

template <typename T>
Observer<T> unwrap(Observer<T>);

template <typename T>
Observer<T> unwrapValue(Observer<T>);

template <typename T>
Observer<T> unwrap(Observer<Observer<T>>);

template <typename T>
Observer<T> unwrapValue(Observer<Observer<T>>);

/**
 * makeObserver(...) creates a new Observer<T> object given a functor to
 * compute it. The functor can return T or std::shared_ptr<const T>.
 *
 * makeObserver(...) blocks until the initial version of Observer is computed.
 * If creator functor fails (throws or returns a nullptr) during this first
 * call, the exception is re-thrown by makeObserver(...).
 *
 * For all subsequent updates if creator functor fails (throws or returs a
 * nullptr), the Observer (and all its dependents) is not updated.
 */
template <typename F>
Observer<observer_detail::ResultOf<F>> makeObserver(F&& creator);

template <typename F>
Observer<observer_detail::ResultOfUnwrapSharedPtr<F>> makeObserver(F&& creator);

template <typename F>
Observer<observer_detail::ResultOfUnwrapObserver<F>> makeObserver(F&& creator);

/**
 * The returned Observer will proxy updates from the input observer, but will
 * skip updates that contain the same (according to operator==) value even if
 * the actual object in the update is different.
 */
template <typename T>
Observer<T> makeValueObserver(Observer<T> observer);

/**
 * A more efficient short-cut for makeValueObserver(makeObserver(...)).
 */
template <typename F>
Observer<observer_detail::ResultOf<F>> makeValueObserver(F&& creator);

template <typename F>
Observer<observer_detail::ResultOfUnwrapSharedPtr<F>> makeValueObserver(
    F&& creator);

/**
 * The returned Observer will never update and always return the passed value.
 */
template <typename T>
Observer<T> makeStaticObserver(T value);

template <typename T>
Observer<T> makeStaticObserver(std::shared_ptr<T> value);

template <typename T>
class AtomicObserver {
 public:
  explicit AtomicObserver(Observer<T> observer);
  AtomicObserver(const AtomicObserver<T>& other);
  AtomicObserver(AtomicObserver<T>&& other) noexcept;
  AtomicObserver<T>& operator=(const AtomicObserver<T>& other);
  AtomicObserver<T>& operator=(AtomicObserver<T>&& other) noexcept;
  AtomicObserver<T>& operator=(Observer<T> observer);

  T get() const;
  T operator*() const { return get(); }

  Observer<T> getUnderlyingObserver() const { return observer_; }

 private:
  mutable std::atomic<T> cachedValue_{};
  mutable std::atomic<size_t> cachedVersion_{};
  mutable SharedMutex refreshLock_;
  Observer<T> observer_;
};

template <typename T>
class TLObserver {
 public:
  explicit TLObserver(Observer<T> observer);
  TLObserver(const TLObserver<T>& other);

  const Snapshot<T>& getSnapshotRef() const;
  const Snapshot<T>& operator*() const { return getSnapshotRef(); }

  Observer<T> getUnderlyingObserver() const { return observer_; }

 private:
  Observer<T> observer_;
  ThreadLocal<Snapshot<T>> snapshot_;
};

template <typename T>
class ReadMostlyAtomicObserver {
 public:
  explicit ReadMostlyAtomicObserver(Observer<T> observer);
  ReadMostlyAtomicObserver(const ReadMostlyAtomicObserver<T>&) = delete;
  ReadMostlyAtomicObserver<T>& operator=(const ReadMostlyAtomicObserver<T>&) =
      delete;

  T get() const;
  T operator*() const { return get(); }

  Observer<T> getUnderlyingObserver() const { return observer_; }

 private:
  Observer<T> observer_;
  std::atomic<T> cachedValue_{};
  CallbackHandle callback_;
};

/**
 * Same as makeObserver(...), but creates AtomicObserver.
 */
template <typename T>
AtomicObserver<T> makeAtomicObserver(Observer<T> observer) {
  return AtomicObserver<T>(std::move(observer));
}

template <typename F>
auto makeAtomicObserver(F&& creator) {
  return makeAtomicObserver(makeObserver(std::forward<F>(creator)));
}

/**
 * Same as makeObserver(...), but creates TLObserver.
 */
template <typename T>
TLObserver<T> makeTLObserver(Observer<T> observer) {
  return TLObserver<T>(std::move(observer));
}

template <typename F>
auto makeTLObserver(F&& creator) {
  return makeTLObserver(makeObserver(std::forward<F>(creator)));
}

/**
 * Same as makeObserver(...), but creates ReadMostlyAtomicObserver.
 */
template <typename T>
ReadMostlyAtomicObserver<T> makeReadMostlyAtomicObserver(Observer<T> observer) {
  return ReadMostlyAtomicObserver<T>(std::move(observer));
}

template <typename F>
auto makeReadMostlyAtomicObserver(F&& creator) {
  return makeReadMostlyAtomicObserver(makeObserver(std::forward<F>(creator)));
}

template <typename T, bool CacheInThreadLocal>
struct ObserverTraits {};

template <typename T>
struct ObserverTraits<T, false> {
  using type = Observer<T>;
};

template <typename T>
struct ObserverTraits<T, true> {
  using type = TLObserver<T>;
};

template <typename T, bool CacheInThreadLocal>
using ObserverT = typename ObserverTraits<T, CacheInThreadLocal>::type;
} // namespace observer
} // namespace folly

#include <folly/experimental/observer/Observer-inl.h>
