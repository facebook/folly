/*
 * Copyright 2015 Facebook, Inc.
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
/* -*- Mode: C++; tab-width: 2; c-basic-offset: 2; indent-tabs-mode: nil -*- */
#pragma once

#include <atomic>
#include <memory>
#include <folly/Optional.h>
#include <folly/ThreadLocal.h>
#include <folly/SpinLock.h>

namespace folly {

/**
 * @file ReadMostlySharedPtr is a smart pointer that allows for high
 * performance shared ownership of an object. In order to provide
 * this, ReadMostlySharedPtr may potentially delay the destruction of
 * a shared object for longer than a std::shared_ptr would, and
 * depending on the implementation, may have slower updates.
 *
 * The load() method allows a reader to acquire a ReadPtr that
 * maintains a reference to a single version of the object. Even if a
 * writer calls store(), the ReadPtr will point to the version of the
 * object that was in use at the time of the read. The old version of
 * the object will only be destroyed after all outstanding ReadPtrs to
 * that version have been destroyed.
 */

template<typename T,
         typename Tag = void>
class ReadMostlySharedPtr {
 public:
  constexpr explicit ReadMostlySharedPtr(std::unique_ptr<T>&& ptr = nullptr)
      : masterPtr_(std::move(ptr)) {}

  /**
   * Replaces the managed object.
   */
  void store(std::unique_ptr<T>&& uptr) {
    {
      std::shared_ptr<T> ptr(std::move(uptr));
      std::lock_guard<std::mutex> lock(mutex_);
      // Swap to avoid calling ~T() under the lock
      std::swap(masterPtr_, ptr);
    }

    {
      // This also holds a lock that prevents destruction of thread cache
      // entries, but not creation. If creating a thread cache entry for a new
      // thread happens duting iteration, the entry is not guaranteed to
      // be seen. It's fine for us: if load() created a new cache entry after
      // we got accessor, it will see the updated pointer, so we don't need to
      // clear the cache.
      auto accessor = threadLocalCache_.accessAllThreads();

      for (CachedPointer& local: accessor) {
        std::lock_guard<folly::SpinLock> local_lock(local.lock);
        // We could instead just assign masterPtr_ to local.ptr, but it's better
        // if the thread allocates the Ptr for itself - the allocator is more
        // likely to place its reference counter in a region optimal for access
        // from that thread.
        local.ptr.clear();
      }
    }
  }

  class ReadPtr {
    friend class ReadMostlySharedPtr;
   public:
    ReadPtr() {}
    void reset() {
      ref_ = nullptr;
      ptr_.reset();
    }
    explicit operator bool() const {
      return (ref_ != nullptr);
    }
    bool operator ==(T* ptr) const {
      return ref_ == ptr;
    }
    bool operator ==(std::nullptr_t) const {
      return ref_ == nullptr;
    }
    T* operator->() const { return ref_; }
    T& operator*() const { return *ref_; }
    T* get() const { return ref_; }
   private:
    explicit ReadPtr(std::shared_ptr<T>& ptr)
        : ptr_(ptr)
        , ref_(ptr.get()) {}
    std::shared_ptr<T> ptr_;
    T* ref_{nullptr};
  };

  /**
   * Returns a shared_ptr to the managed object.
   */
  ReadPtr load() const {
    auto& local = *threadLocalCache_;

    std::lock_guard<folly::SpinLock> local_lock(local.lock);

    if (!local.ptr.hasValue()) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!masterPtr_) {
        local.ptr.emplace(nullptr);
      } else {
        // The following expression is tricky.
        //
        // It creates a shared_ptr<shared_ptr<T>> that points to a copy of
        // masterPtr_. The reference counter of this shared_ptr<shared_ptr<T>>
        // will normally only be modified from this thread, which avoids
        // cache line bouncing. (Though the caller is free to pass the pointer
        // to other threads and bump reference counter from there)
        //
        // Then this shared_ptr<shared_ptr<T>> is turned into shared_ptr<T>.
        // This means that the returned shared_ptr<T> will internally point to
        // control block of the shared_ptr<shared_ptr<T>>, but will dereference
        // to T, not shared_ptr<T>.
        local.ptr = makeCachedCopy(masterPtr_);
      }
    }

    // The return statement makes the copy before destroying local variables,
    // so local.ptr is only accessed under local.lock here.
    return ReadPtr(local.ptr.value());
  }

 private:

  // non copyable
  ReadMostlySharedPtr(const ReadMostlySharedPtr&) = delete;
  ReadMostlySharedPtr& operator=(const ReadMostlySharedPtr&) = delete;

  struct CachedPointer {
    folly::Optional<std::shared_ptr<T>> ptr;
    folly::SpinLock lock;
  };

  std::shared_ptr<T> masterPtr_;

  // Instead of using Tag as tag for ThreadLocal, effectively use pair (T, Tag),
  // which is more granular.
  struct ThreadLocalTag {};

  mutable folly::ThreadLocal<CachedPointer, ThreadLocalTag> threadLocalCache_;

  // Ensures safety between concurrent store() and load() calls
  mutable std::mutex mutex_;

  std::shared_ptr<T>
  makeCachedCopy(const std::shared_ptr<T> &ptr) const {
    // For std::shared_ptr wrap a copy in another std::shared_ptr to
    // avoid cache line bouncing.
    //
    // The following expression is tricky.
    //
    // It creates a shared_ptr<shared_ptr<T>> that points to a copy of
    // masterPtr_. The reference counter of this shared_ptr<shared_ptr<T>>
    // will normally only be modified from this thread, which avoids
    // cache line bouncing. (Though the caller is free to pass the pointer
    // to other threads and bump reference counter from there)
    //
    // Then this shared_ptr<shared_ptr<T>> is turned into shared_ptr<T>.
    // This means that the returned shared_ptr<T> will internally point to
    // control block of the shared_ptr<shared_ptr<T>>, but will dereference
    // to T, not shared_ptr<T>.
    return std::shared_ptr<T>(
      std::make_shared<std::shared_ptr<T>>(ptr), ptr.get());
  }

};

}
