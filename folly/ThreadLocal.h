/*
 * Copyright 2013 Facebook, Inc.
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
 * Improved thread local storage for non-trivial types (similar speed as
 * pthread_getspecific but only consumes a single pthread_key_t, and 4x faster
 * than boost::thread_specific_ptr).
 *
 * Also includes an accessor interface to walk all the thread local child
 * objects of a parent.  accessAllThreads() initializes an accessor which holds
 * a global lock *that blocks all creation and destruction of ThreadLocal
 * objects with the same Tag* and can be used as an iterable container.
 *
 * Intended use is for frequent write, infrequent read data access patterns such
 * as counters.
 *
 * There are two classes here - ThreadLocal and ThreadLocalPtr.  ThreadLocalPtr
 * has semantics similar to boost::thread_specific_ptr. ThreadLocal is a thin
 * wrapper around ThreadLocalPtr that manages allocation automatically.
 *
 * @author Spencer Ahrens (sahrens)
 */

#ifndef FOLLY_THREADLOCAL_H_
#define FOLLY_THREADLOCAL_H_

#include "folly/Portability.h"
#include <boost/iterator/iterator_facade.hpp>
#include "folly/Likely.h"
#include <type_traits>


namespace folly {
enum class TLPDestructionMode {
  THIS_THREAD,
  ALL_THREADS
};
}  // namespace

#include "folly/detail/ThreadLocalDetail.h"

namespace folly {

template<class T, class Tag> class ThreadLocalPtr;

template<class T, class Tag=void>
class ThreadLocal {
 public:
  ThreadLocal() { }

  T* get() const {
    T* ptr = tlp_.get();
    if (LIKELY(ptr != nullptr)) {
      return ptr;
    }

    // separated new item creation out to speed up the fast path.
    return makeTlp();
  }

  T* operator->() const {
    return get();
  }

  T& operator*() const {
    return *get();
  }

  void reset(T* newPtr = nullptr) {
    tlp_.reset(newPtr);
  }

  typedef typename ThreadLocalPtr<T,Tag>::Accessor Accessor;
  Accessor accessAllThreads() const {
    return tlp_.accessAllThreads();
  }

  // movable
  ThreadLocal(ThreadLocal&&) = default;
  ThreadLocal& operator=(ThreadLocal&&) = default;

 private:
  // non-copyable
  ThreadLocal(const ThreadLocal&) = delete;
  ThreadLocal& operator=(const ThreadLocal&) = delete;

  T* makeTlp() const {
    T* ptr = new T();
    tlp_.reset(ptr);
    return ptr;
  }

  mutable ThreadLocalPtr<T,Tag> tlp_;
};

/*
 * The idea here is that __thread is faster than pthread_getspecific, so we
 * keep a __thread array of pointers to objects (ThreadEntry::elements) where
 * each array has an index for each unique instance of the ThreadLocalPtr
 * object.  Each ThreadLocalPtr object has a unique id that is an index into
 * these arrays so we can fetch the correct object from thread local storage
 * very efficiently.
 *
 * In order to prevent unbounded growth of the id space and thus huge
 * ThreadEntry::elements, arrays, for example due to continuous creation and
 * destruction of ThreadLocalPtr objects, we keep a set of all active
 * instances.  When an instance is destroyed we remove it from the active
 * set and insert the id into freeIds_ for reuse.  These operations require a
 * global mutex, but only happen at construction and destruction time.
 *
 * We use a single global pthread_key_t per Tag to manage object destruction and
 * memory cleanup upon thread exit because there is a finite number of
 * pthread_key_t's available per machine.
 */

template<class T, class Tag=void>
class ThreadLocalPtr {
 public:
  ThreadLocalPtr() : id_(threadlocal_detail::StaticMeta<Tag>::create()) { }

  ThreadLocalPtr(ThreadLocalPtr&& other) : id_(other.id_) {
    other.id_ = 0;
  }

  ThreadLocalPtr& operator=(ThreadLocalPtr&& other) {
    assert(this != &other);
    destroy();
    id_ = other.id_;
    other.id_ = 0;
    return *this;
  }

  ~ThreadLocalPtr() {
    destroy();
  }

  T* get() const {
    return static_cast<T*>(threadlocal_detail::StaticMeta<Tag>::get(id_).ptr);
  }

  T* operator->() const {
    return get();
  }

  T& operator*() const {
    return *get();
  }

  void reset(T* newPtr = nullptr) {
    threadlocal_detail::ElementWrapper& w =
      threadlocal_detail::StaticMeta<Tag>::get(id_);
    if (w.ptr != newPtr) {
      w.dispose(TLPDestructionMode::THIS_THREAD);
      w.set(newPtr);
    }
  }

  explicit operator bool() const {
    return get() != nullptr;
  }

  /**
   * reset() with a custom deleter:
   * deleter(T* ptr, TLPDestructionMode mode)
   * "mode" is ALL_THREADS if we're destructing this ThreadLocalPtr (and thus
   * deleting pointers for all threads), and THIS_THREAD if we're only deleting
   * the member for one thread (because of thread exit or reset())
   */
  template <class Deleter>
  void reset(T* newPtr, Deleter deleter) {
    threadlocal_detail::ElementWrapper& w =
      threadlocal_detail::StaticMeta<Tag>::get(id_);
    if (w.ptr != newPtr) {
      w.dispose(TLPDestructionMode::THIS_THREAD);
      w.set(newPtr, deleter);
    }
  }

  // Holds a global lock for iteration through all thread local child objects.
  // Can be used as an iterable container.
  // Use accessAllThreads() to obtain one.
  class Accessor {
    friend class ThreadLocalPtr<T,Tag>;

    threadlocal_detail::StaticMeta<Tag>& meta_;
    boost::mutex* lock_;
    int id_;

   public:
    class Iterator;
    friend class Iterator;

    // The iterators obtained from Accessor are bidirectional iterators.
    class Iterator : public boost::iterator_facade<
          Iterator,                               // Derived
          T,                                      // value_type
          boost::bidirectional_traversal_tag> {   // traversal
      friend class Accessor;
      friend class boost::iterator_core_access;
      const Accessor* const accessor_;
      threadlocal_detail::ThreadEntry* e_;

      void increment() {
        e_ = e_->next;
        incrementToValid();
      }

      void decrement() {
        e_ = e_->prev;
        decrementToValid();
      }

      T& dereference() const {
        return *static_cast<T*>(e_->elements[accessor_->id_].ptr);
      }

      bool equal(const Iterator& other) const {
        return (accessor_->id_ == other.accessor_->id_ &&
                e_ == other.e_);
      }

      explicit Iterator(const Accessor* accessor)
        : accessor_(accessor),
          e_(&accessor_->meta_.head_) {
      }

      bool valid() const {
        return (e_->elements &&
                accessor_->id_ < e_->elementsCapacity &&
                e_->elements[accessor_->id_].ptr);
      }

      void incrementToValid() {
        for (; e_ != &accessor_->meta_.head_ && !valid(); e_ = e_->next) { }
      }

      void decrementToValid() {
        for (; e_ != &accessor_->meta_.head_ && !valid(); e_ = e_->prev) { }
      }
    };

    ~Accessor() {
      release();
    }

    Iterator begin() const {
      return ++Iterator(this);
    }

    Iterator end() const {
      return Iterator(this);
    }

    Accessor(const Accessor&) = delete;
    Accessor& operator=(const Accessor&) = delete;

    Accessor(Accessor&& other) noexcept
      : meta_(other.meta_),
        lock_(other.lock_),
        id_(other.id_) {
      other.id_ = 0;
      other.lock_ = nullptr;
    }

    Accessor& operator=(Accessor&& other) noexcept {
      // Each Tag has its own unique meta, and accessors with different Tags
      // have different types.  So either *this is empty, or this and other
      // have the same tag.  But if they have the same tag, they have the same
      // meta (and lock), so they'd both hold the lock at the same time,
      // which is impossible, which leaves only one possible scenario --
      // *this is empty.  Assert it.
      assert(&meta_ == &other.meta_);
      assert(lock_ == nullptr);
      using std::swap;
      swap(lock_, other.lock_);
      swap(id_, other.id_);
    }

    Accessor()
      : meta_(threadlocal_detail::StaticMeta<Tag>::instance()),
        lock_(nullptr),
        id_(0) {
    }

   private:
    explicit Accessor(int id)
      : meta_(threadlocal_detail::StaticMeta<Tag>::instance()),
        lock_(&meta_.lock_) {
      lock_->lock();
      id_ = id;
    }

    void release() {
      if (lock_) {
        lock_->unlock();
        id_ = 0;
        lock_ = nullptr;
      }
    }
  };

  // accessor allows a client to iterate through all thread local child
  // elements of this ThreadLocal instance.  Holds a global lock for each <Tag>
  Accessor accessAllThreads() const {
    static_assert(!std::is_same<Tag, void>::value,
                  "Must use a unique Tag to use the accessAllThreads feature");
    return Accessor(id_);
  }

 private:
  void destroy() {
    if (id_) {
      threadlocal_detail::StaticMeta<Tag>::destroy(id_);
    }
  }

  // non-copyable
  ThreadLocalPtr(const ThreadLocalPtr&) = delete;
  ThreadLocalPtr& operator=(const ThreadLocalPtr&) = delete;

  int id_;  // every instantiation has a unique id
};

}  // namespace folly

#endif /* FOLLY_THREADLOCAL_H_ */
