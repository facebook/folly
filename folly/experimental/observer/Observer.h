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

#include <folly/SharedMutex.h>
#include <folly/ThreadLocal.h>
#include <folly/experimental/ReadMostlySharedPtr.h>
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
 * See AtomicObserver and TLObserver for optimized reads.
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
  CallbackHandle(
      Observer<T> observer,
      folly::Function<void(Snapshot<T>)> callback);
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

  CallbackHandle addCallback(folly::Function<void(Snapshot<T>)> callback) const;

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

 private:
  mutable std::atomic<T> cachedValue_{};
  mutable std::atomic<size_t> cachedVersion_{};
  mutable folly::SharedMutex refreshLock_;
  Observer<T> observer_;
};

template <typename T>
class TLObserver {
 public:
  explicit TLObserver(Observer<T> observer);
  TLObserver(const TLObserver<T>& other);

  const Snapshot<T>& getSnapshotRef() const;
  const Snapshot<T>& operator*() const { return getSnapshotRef(); }

 private:
  Observer<T> observer_;
  folly::ThreadLocal<Snapshot<T>> snapshot_;
};

/**
 * A TLObserver that optimizes for getting shared_ptr to data
 */
template <typename T>
class ReadMostlyTLObserver {
 public:
  explicit ReadMostlyTLObserver(Observer<T> observer);
  ReadMostlyTLObserver(const ReadMostlyTLObserver<T>& other);

  folly::ReadMostlySharedPtr<const T> getShared() const;

 private:
  folly::ReadMostlySharedPtr<const T> refresh() const;

  struct LocalSnapshot {
    LocalSnapshot() {}
    LocalSnapshot(
        const folly::ReadMostlyMainPtr<const T>& data,
        int64_t version)
        : data_(data), version_(version) {}

    folly::ReadMostlyWeakPtr<const T> data_;
    int64_t version_;
  };

  Observer<T> observer_;

  folly::Synchronized<folly::ReadMostlyMainPtr<const T>, std::mutex>
      globalData_;
  std::atomic<int64_t> globalVersion_;

  folly::ThreadLocal<LocalSnapshot> localSnapshot_;

  // Construct callback last so that it's joined before members it may
  // be accessing are destructed
  CallbackHandle callback_;
};

/**
 * Same as makeObserver(...), but creates ReadMostlyTLObserver.
 */
template <typename T>
ReadMostlyTLObserver<T> makeReadMostlyTLObserver(Observer<T> observer) {
  return ReadMostlyTLObserver<T>(std::move(observer));
}

template <typename F>
auto makeReadMostlyTLObserver(F&& creator) {
  return makeReadMostlyTLObserver(makeObserver(std::forward<F>(creator)));
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
