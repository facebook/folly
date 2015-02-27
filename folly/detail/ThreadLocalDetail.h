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

#ifndef FOLLY_DETAIL_THREADLOCALDETAIL_H_
#define FOLLY_DETAIL_THREADLOCALDETAIL_H_

#include <limits.h>
#include <pthread.h>

#include <mutex>
#include <string>
#include <vector>

#include <glog/logging.h>

#include <folly/Foreach.h>
#include <folly/Exception.h>
#include <folly/Malloc.h>

// In general, emutls cleanup is not guaranteed to play nice with the way
// StaticMeta mixes direct pthread calls and the use of __thread. This has
// caused problems on multiple platforms so don't use __thread there.
//
// XXX: Ideally we would instead determine if emutls is in use at runtime as it
// is possible to configure glibc on Linux to use emutls regardless.
#if !__APPLE__ && !__ANDROID__
#define FOLLY_TLD_USE_FOLLY_TLS 1
#else
#undef FOLLY_TLD_USE_FOLLY_TLS
#endif

namespace folly {
namespace threadlocal_detail {

/**
 * Base class for deleters.
 */
class DeleterBase {
 public:
  virtual ~DeleterBase() { }
  virtual void dispose(void* ptr, TLPDestructionMode mode) const = 0;
};

/**
 * Simple deleter class that calls delete on the passed-in pointer.
 */
template <class Ptr>
class SimpleDeleter : public DeleterBase {
 public:
  virtual void dispose(void* ptr, TLPDestructionMode mode) const {
    delete static_cast<Ptr>(ptr);
  }
};

/**
 * Custom deleter that calls a given callable.
 */
template <class Ptr, class Deleter>
class CustomDeleter : public DeleterBase {
 public:
  explicit CustomDeleter(Deleter d) : deleter_(d) { }
  virtual void dispose(void* ptr, TLPDestructionMode mode) const {
    deleter_(static_cast<Ptr>(ptr), mode);
  }
 private:
  Deleter deleter_;
};


/**
 * POD wrapper around an element (a void*) and an associated deleter.
 * This must be POD, as we memset() it to 0 and memcpy() it around.
 */
struct ElementWrapper {
  bool dispose(TLPDestructionMode mode) {
    if (ptr == nullptr) {
      return false;
    }

    DCHECK(deleter != nullptr);
    deleter->dispose(ptr, mode);
    cleanup();
    return true;
  }

  void* release() {
    auto retPtr = ptr;

    if (ptr != nullptr) {
      cleanup();
    }

    return retPtr;
  }

  template <class Ptr>
  void set(Ptr p) {
    DCHECK(ptr == nullptr);
    DCHECK(deleter == nullptr);

    if (p) {
      // We leak a single object here but that is ok.  If we used an
      // object directly, there is a chance that the destructor will be
      // called on that static object before any of the ElementWrappers
      // are disposed and that isn't so nice.
      static auto d = new SimpleDeleter<Ptr>();
      ptr = p;
      deleter = d;
      ownsDeleter = false;
    }
  }

  template <class Ptr, class Deleter>
  void set(Ptr p, Deleter d) {
    DCHECK(ptr == nullptr);
    DCHECK(deleter == nullptr);
    if (p) {
      ptr = p;
      deleter = new CustomDeleter<Ptr,Deleter>(d);
      ownsDeleter = true;
    }
  }

  void cleanup() {
    if (ownsDeleter) {
      delete deleter;
    }
    ptr = nullptr;
    deleter = nullptr;
    ownsDeleter = false;
  }

  void* ptr;
  DeleterBase* deleter;
  bool ownsDeleter;
};

/**
 * Per-thread entry.  Each thread using a StaticMeta object has one.
 * This is written from the owning thread only (under the lock), read
 * from the owning thread (no lock necessary), and read from other threads
 * (under the lock).
 */
struct ThreadEntry {
  ElementWrapper* elements;
  size_t elementsCapacity;
  ThreadEntry* next;
  ThreadEntry* prev;
};

// Held in a singleton to track our global instances.
// We have one of these per "Tag", by default one for the whole system
// (Tag=void).
//
// Creating and destroying ThreadLocalPtr objects, as well as thread exit
// for threads that use ThreadLocalPtr objects collide on a lock inside
// StaticMeta; you can specify multiple Tag types to break that lock.
template <class Tag>
struct StaticMeta {
  static StaticMeta<Tag>& instance() {
    // Leak it on exit, there's only one per process and we don't have to
    // worry about synchronization with exiting threads.
    static bool constructed = (inst_ = new StaticMeta<Tag>());
    (void)constructed; // suppress unused warning
    return *inst_;
  }

  uint32_t nextId_;
  std::vector<uint32_t> freeIds_;
  std::mutex lock_;
  pthread_key_t pthreadKey_;
  ThreadEntry head_;

  void push_back(ThreadEntry* t) {
    t->next = &head_;
    t->prev = head_.prev;
    head_.prev->next = t;
    head_.prev = t;
  }

  void erase(ThreadEntry* t) {
    t->next->prev = t->prev;
    t->prev->next = t->next;
    t->next = t->prev = t;
  }

#ifdef FOLLY_TLD_USE_FOLLY_TLS
  static FOLLY_TLS ThreadEntry threadEntry_;
#endif
  static StaticMeta<Tag>* inst_;

  StaticMeta() : nextId_(1) {
    head_.next = head_.prev = &head_;
    int ret = pthread_key_create(&pthreadKey_, &onThreadExit);
    checkPosixError(ret, "pthread_key_create failed");

#if FOLLY_HAVE_PTHREAD_ATFORK
    ret = pthread_atfork(/*prepare*/ &StaticMeta::preFork,
                         /*parent*/ &StaticMeta::onForkParent,
                         /*child*/ &StaticMeta::onForkChild);
    checkPosixError(ret, "pthread_atfork failed");
#elif !__ANDROID__
    // pthread_atfork is not part of the Android NDK at least as of n9d. If
    // something is trying to call native fork() directly at all with Android's
    // process management model, this is probably the least of the problems.
    //
    // But otherwise, this is a problem.
    #warning pthread_atfork unavailable
#endif
  }
  ~StaticMeta() {
    LOG(FATAL) << "StaticMeta lives forever!";
  }

  static ThreadEntry* getThreadEntry() {
#ifdef FOLLY_TLD_USE_FOLLY_TLS
    return &threadEntry_;
#else
    ThreadEntry* threadEntry =
        static_cast<ThreadEntry*>(pthread_getspecific(inst_->pthreadKey_));
    if (!threadEntry) {
        threadEntry = new ThreadEntry();
        int ret = pthread_setspecific(inst_->pthreadKey_, threadEntry);
        checkPosixError(ret, "pthread_setspecific failed");
    }
    return threadEntry;
#endif
  }

  static void preFork(void) {
    instance().lock_.lock();  // Make sure it's created
  }

  static void onForkParent(void) {
    inst_->lock_.unlock();
  }

  static void onForkChild(void) {
    // only the current thread survives
    inst_->head_.next = inst_->head_.prev = &inst_->head_;
    ThreadEntry* threadEntry = getThreadEntry();
    // If this thread was in the list before the fork, add it back.
    if (threadEntry->elementsCapacity != 0) {
      inst_->push_back(threadEntry);
    }
    inst_->lock_.unlock();
  }

  static void onThreadExit(void* ptr) {
    auto& meta = instance();
#ifdef FOLLY_TLD_USE_FOLLY_TLS
    ThreadEntry* threadEntry = getThreadEntry();

    DCHECK_EQ(ptr, &meta);
    DCHECK_GT(threadEntry->elementsCapacity, 0);
#else
    ThreadEntry* threadEntry = static_cast<ThreadEntry*>(ptr);
#endif
    {
      std::lock_guard<std::mutex> g(meta.lock_);
      meta.erase(threadEntry);
      // No need to hold the lock any longer; the ThreadEntry is private to this
      // thread now that it's been removed from meta.
    }
    // NOTE: User-provided deleter / object dtor itself may be using ThreadLocal
    // with the same Tag, so dispose() calls below may (re)create some of the
    // elements or even increase elementsCapacity, thus multiple cleanup rounds
    // may be required.
    for (bool shouldRun = true; shouldRun; ) {
      shouldRun = false;
      FOR_EACH_RANGE(i, 0, threadEntry->elementsCapacity) {
        if (threadEntry->elements[i].dispose(TLPDestructionMode::THIS_THREAD)) {
          shouldRun = true;
        }
      }
    }
    free(threadEntry->elements);
    threadEntry->elements = nullptr;
    pthread_setspecific(meta.pthreadKey_, nullptr);

#ifndef FOLLY_TLD_USE_FOLLY_TLS
    // Allocated in getThreadEntry() when not using folly TLS; free it
    delete threadEntry;
#endif
  }

  static uint32_t create() {
    uint32_t id;
    auto & meta = instance();
    std::lock_guard<std::mutex> g(meta.lock_);
    if (!meta.freeIds_.empty()) {
      id = meta.freeIds_.back();
      meta.freeIds_.pop_back();
    } else {
      id = meta.nextId_++;
    }
    return id;
  }

  static void destroy(uint32_t id) {
    try {
      auto & meta = instance();
      // Elements in other threads that use this id.
      std::vector<ElementWrapper> elements;
      {
        std::lock_guard<std::mutex> g(meta.lock_);
        for (ThreadEntry* e = meta.head_.next; e != &meta.head_; e = e->next) {
          if (id < e->elementsCapacity && e->elements[id].ptr) {
            elements.push_back(e->elements[id]);

            /*
             * Writing another thread's ThreadEntry from here is fine;
             * the only other potential reader is the owning thread --
             * from onThreadExit (which grabs the lock, so is properly
             * synchronized with us) or from get(), which also grabs
             * the lock if it needs to resize the elements vector.
             *
             * We can't conflict with reads for a get(id), because
             * it's illegal to call get on a thread local that's
             * destructing.
             */
            e->elements[id].ptr = nullptr;
            e->elements[id].deleter = nullptr;
            e->elements[id].ownsDeleter = false;
          }
        }
        meta.freeIds_.push_back(id);
      }
      // Delete elements outside the lock
      FOR_EACH(it, elements) {
        it->dispose(TLPDestructionMode::ALL_THREADS);
      }
    } catch (...) { // Just in case we get a lock error or something anyway...
      LOG(WARNING) << "Destructor discarding an exception that was thrown.";
    }
  }

  /**
   * Reserve enough space in the ThreadEntry::elements for the item
   * @id to fit in.
   */
  static void reserve(uint32_t id) {
    auto& meta = instance();
    ThreadEntry* threadEntry = getThreadEntry();
    size_t prevCapacity = threadEntry->elementsCapacity;
    // Growth factor < 2, see folly/docs/FBVector.md; + 5 to prevent
    // very slow start.
    size_t newCapacity = static_cast<size_t>((id + 5) * 1.7);
    assert(newCapacity > prevCapacity);
    ElementWrapper* reallocated = nullptr;

    // Need to grow. Note that we can't call realloc, as elements is
    // still linked in meta, so another thread might access invalid memory
    // after realloc succeeds. We'll copy by hand and update our ThreadEntry
    // under the lock.
    if (usingJEMalloc()) {
      bool success = false;
      size_t newByteSize = nallocx(newCapacity * sizeof(ElementWrapper), 0);

      // Try to grow in place.
      //
      // Note that xallocx(MALLOCX_ZERO) will only zero newly allocated memory,
      // even if a previous allocation allocated more than we requested.
      // This is fine; we always use MALLOCX_ZERO with jemalloc and we
      // always expand our allocation to the real size.
      if (prevCapacity * sizeof(ElementWrapper) >=
          jemallocMinInPlaceExpandable) {
        success = (xallocx(threadEntry->elements, newByteSize, 0, MALLOCX_ZERO)
                   == newByteSize);
      }

      // In-place growth failed.
      if (!success) {
        success = ((reallocated = static_cast<ElementWrapper*>(
                    mallocx(newByteSize, MALLOCX_ZERO))) != nullptr);
      }

      if (success) {
        // Expand to real size
        assert(newByteSize / sizeof(ElementWrapper) >= newCapacity);
        newCapacity = newByteSize / sizeof(ElementWrapper);
      } else {
        throw std::bad_alloc();
      }
    } else {  // no jemalloc
      // calloc() is simpler than malloc() followed by memset(), and
      // potentially faster when dealing with a lot of memory, as it can get
      // already-zeroed pages from the kernel.
      reallocated = static_cast<ElementWrapper*>(
          calloc(newCapacity, sizeof(ElementWrapper)));
      if (!reallocated) {
        throw std::bad_alloc();
      }
    }

    // Success, update the entry
    {
      std::lock_guard<std::mutex> g(meta.lock_);

      if (prevCapacity == 0) {
        meta.push_back(threadEntry);
      }

      if (reallocated) {
       /*
        * Note: we need to hold the meta lock when copying data out of
        * the old vector, because some other thread might be
        * destructing a ThreadLocal and writing to the elements vector
        * of this thread.
        */
        memcpy(reallocated, threadEntry->elements,
               sizeof(ElementWrapper) * prevCapacity);
        using std::swap;
        swap(reallocated, threadEntry->elements);
      }
      threadEntry->elementsCapacity = newCapacity;
    }

    free(reallocated);

#ifdef FOLLY_TLD_USE_FOLLY_TLS
    if (prevCapacity == 0) {
      pthread_setspecific(meta.pthreadKey_, &meta);
    }
#endif
  }

  static ElementWrapper& get(uint32_t id) {
    ThreadEntry* threadEntry = getThreadEntry();
    if (UNLIKELY(threadEntry->elementsCapacity <= id)) {
      reserve(id);
      assert(threadEntry->elementsCapacity > id);
    }
    return threadEntry->elements[id];
  }
};

#ifdef FOLLY_TLD_USE_FOLLY_TLS
template <class Tag>
FOLLY_TLS ThreadEntry StaticMeta<Tag>::threadEntry_{nullptr, 0,
                                                    nullptr, nullptr};
#endif
template <class Tag> StaticMeta<Tag>* StaticMeta<Tag>::inst_ = nullptr;

}  // namespace threadlocal_detail
}  // namespace folly

#endif /* FOLLY_DETAIL_THREADLOCALDETAIL_H_ */
