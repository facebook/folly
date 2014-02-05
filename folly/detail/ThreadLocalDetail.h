/*
 * Copyright 2014 Facebook, Inc.
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

#include "folly/Foreach.h"
#include "folly/Exception.h"
#include "folly/Malloc.h"

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
  void dispose(TLPDestructionMode mode) {
    if (ptr != NULL) {
      DCHECK(deleter != NULL);
      deleter->dispose(ptr, mode);
      if (ownsDeleter) {
        delete deleter;
      }
      ptr = NULL;
      deleter = NULL;
      ownsDeleter = false;
    }
  }

  template <class Ptr>
  void set(Ptr p) {
    DCHECK(ptr == NULL);
    DCHECK(deleter == NULL);

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
    DCHECK(ptr == NULL);
    DCHECK(deleter == NULL);
    if (p) {
      ptr = p;
      deleter = new CustomDeleter<Ptr,Deleter>(d);
      ownsDeleter = true;
    }
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

  int nextId_;
  std::vector<int> freeIds_;
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

#if !__APPLE__
  static __thread ThreadEntry threadEntry_;
#endif
  static StaticMeta<Tag>* inst_;

  StaticMeta() : nextId_(1) {
    head_.next = head_.prev = &head_;
    int ret = pthread_key_create(&pthreadKey_, &onThreadExit);
    checkPosixError(ret, "pthread_key_create failed");

    ret = pthread_atfork(/*prepare*/ &StaticMeta::preFork,
                         /*parent*/ &StaticMeta::onForkParent,
                         /*child*/ &StaticMeta::onForkChild);
    checkPosixError(ret, "pthread_atfork failed");
  }
  ~StaticMeta() {
    LOG(FATAL) << "StaticMeta lives forever!";
  }

  static ThreadEntry* getThreadEntry() {
#if !__APPLE__
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
    auto & meta = instance();
#if !__APPLE__
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
    FOR_EACH_RANGE(i, 0, threadEntry->elementsCapacity) {
      threadEntry->elements[i].dispose(TLPDestructionMode::THIS_THREAD);
    }
    free(threadEntry->elements);
    threadEntry->elements = NULL;
    pthread_setspecific(meta.pthreadKey_, NULL);

#if __APPLE__
    // Allocated in getThreadEntry(); free it
    delete threadEntry;
#endif
  }

  static int create() {
    int id;
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

  static void destroy(int id) {
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
  static void reserve(int id) {
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
      size_t newByteSize = newCapacity * sizeof(ElementWrapper);
      size_t realByteSize = 0;

      // Try to grow in place.
      //
      // Note that rallocm(ALLOCM_ZERO) will only zero newly allocated memory,
      // even if a previous allocation allocated more than we requested.
      // This is fine; we always use ALLOCM_ZERO with jemalloc and we
      // always expand our allocation to the real size.
      if (prevCapacity * sizeof(ElementWrapper) >=
          jemallocMinInPlaceExpandable) {
        success = (rallocm(reinterpret_cast<void**>(&threadEntry->elements),
                           &realByteSize,
                           newByteSize,
                           0,
                           ALLOCM_NO_MOVE | ALLOCM_ZERO) == ALLOCM_SUCCESS);

      }

      // In-place growth failed.
      if (!success) {
        // Note that, unlike calloc,allocm(... ALLOCM_ZERO) zeros all
        // allocated bytes (*realByteSize) and not just the requested
        // bytes (newByteSize)
        success = (allocm(reinterpret_cast<void**>(&reallocated),
                          &realByteSize,
                          newByteSize,
                          ALLOCM_ZERO) == ALLOCM_SUCCESS);
      }

      if (success) {
        // Expand to real size
        assert(realByteSize / sizeof(ElementWrapper) >= newCapacity);
        newCapacity = realByteSize / sizeof(ElementWrapper);
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

#if !__APPLE__
    if (prevCapacity == 0) {
      pthread_setspecific(meta.pthreadKey_, &meta);
    }
#endif
  }

  static ElementWrapper& get(int id) {
    ThreadEntry* threadEntry = getThreadEntry();
    if (UNLIKELY(threadEntry->elementsCapacity <= id)) {
      reserve(id);
      assert(threadEntry->elementsCapacity > id);
    }
    return threadEntry->elements[id];
  }
};

#if !__APPLE__
template <class Tag> __thread ThreadEntry StaticMeta<Tag>::threadEntry_ = {0};
#endif
template <class Tag> StaticMeta<Tag>* StaticMeta<Tag>::inst_ = nullptr;

}  // namespace threadlocal_detail
}  // namespace folly

#endif /* FOLLY_DETAIL_THREADLOCALDETAIL_H_ */

