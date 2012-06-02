/*
 * Copyright 2012 Facebook, Inc.
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
#include <list>
#include <string>
#include <vector>

#include <boost/thread/mutex.hpp>

#include <glog/logging.h>

#include "folly/Foreach.h"
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
    static bool constructed = (inst = new StaticMeta<Tag>());
    return *inst;
  }

  int nextId_;
  std::vector<int> freeIds_;
  boost::mutex lock_;
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

  static __thread ThreadEntry threadEntry_;
  static StaticMeta<Tag>* inst;

  StaticMeta() : nextId_(1) {
    head_.next = head_.prev = &head_;
    int ret = pthread_key_create(&pthreadKey_, &onThreadExit);
    if (ret != 0) {
      std::string msg;
      switch (ret) {
        case EAGAIN:
          char buf[100];
          snprintf(buf, sizeof(buf), "PTHREAD_KEYS_MAX (%d) is exceeded",
                   PTHREAD_KEYS_MAX);
          msg = buf;
          break;
        case ENOMEM:
          msg = "Out-of-memory";
          break;
        default:
          msg = "(unknown error)";
      }
      throw std::runtime_error("pthread_key_create failed: " + msg);
    }
  }
  ~StaticMeta() {
    LOG(FATAL) << "StaticMeta lives forever!";
  }

  static void onThreadExit(void* ptr) {
    auto & meta = instance();
    DCHECK_EQ(ptr, &meta);
    // We wouldn't call pthread_setspecific unless we actually called get()
    DCHECK_NE(threadEntry_.elementsCapacity, 0);
    {
      boost::lock_guard<boost::mutex> g(meta.lock_);
      meta.erase(&threadEntry_);
      // No need to hold the lock any longer; threadEntry_ is private to this
      // thread now that it's been removed from meta.
    }
    FOR_EACH_RANGE(i, 0, threadEntry_.elementsCapacity) {
      threadEntry_.elements[i].dispose(TLPDestructionMode::THIS_THREAD);
    }
    free(threadEntry_.elements);
    threadEntry_.elements = NULL;
    pthread_setspecific(meta.pthreadKey_, NULL);
  }

  static int create() {
    int id;
    auto & meta = instance();
    boost::lock_guard<boost::mutex> g(meta.lock_);
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
        boost::lock_guard<boost::mutex> g(meta.lock_);
        for (ThreadEntry* e = meta.head_.next; e != &meta.head_; e = e->next) {
          if (id < e->elementsCapacity && e->elements[id].ptr) {
            elements.push_back(e->elements[id]);

            // Writing another thread's ThreadEntry from here is fine;
            // the only other potential reader is the owning thread --
            // from onThreadExit (which grabs the lock, so is properly
            // synchronized with us) or from get() -- but using get() on a
            // ThreadLocalPtr object that's being destroyed is a bug, so
            // undefined behavior is fair game.
            e->elements[id].ptr = NULL;
            e->elements[id].deleter = NULL;
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

  static ElementWrapper& get(int id) {
    size_t prevSize = threadEntry_.elementsCapacity;
    if (prevSize <= id) {
      size_t newSize = static_cast<size_t>((id + 5) * 1.7);
      auto & meta = instance();
      ElementWrapper* ptr = NULL;
      // Rely on jemalloc to zero the memory if possible -- maybe it knows
      // it's already zeroed and saves us some work.
      if (!usingJEMalloc() ||
          prevSize < jemallocMinInPlaceExpandable ||
          (rallocm(
              static_cast<void**>(static_cast<void*>(&threadEntry_.elements)),
              NULL, newSize, 0, ALLOCM_NO_MOVE | ALLOCM_ZERO) !=
           ALLOCM_SUCCESS)) {
        // Sigh, must realloc, but we can't call realloc here, as elements is
        // still linked in meta, so another thread might access invalid memory
        // after realloc succeeds.  We'll copy by hand and update threadEntry_
        // under the lock.
        if ((ptr = static_cast<ElementWrapper*>(
              malloc(sizeof(ElementWrapper) * newSize))) != NULL) {
          memcpy(ptr, threadEntry_.elements,
                 sizeof(ElementWrapper) * prevSize);
          memset(ptr + prevSize, 0,
                 (newSize - prevSize) * sizeof(ElementWrapper));
        } else {
          throw std::bad_alloc();
        }
      }

      // Success, update the entry
      {
        boost::lock_guard<boost::mutex> g(meta.lock_);
        if (prevSize == 0) {
          meta.push_back(&threadEntry_);
        }
        if (ptr) {
          using std::swap;
          swap(ptr, threadEntry_.elements);
        }
        threadEntry_.elementsCapacity = newSize;
      }

      free(ptr);

      if (prevSize == 0) {
        pthread_setspecific(meta.pthreadKey_, &meta);
      }
    }
    return threadEntry_.elements[id];
  }
};

template <class Tag> __thread ThreadEntry StaticMeta<Tag>::threadEntry_ = {0};
template <class Tag> StaticMeta<Tag>* StaticMeta<Tag>::inst = nullptr;

}  // namespace threadlocal_detail
}  // namespace folly

#endif /* FOLLY_DETAIL_THREADLOCALDETAIL_H_ */

