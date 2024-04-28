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

#include <limits.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glog/logging.h>

#include <folly/Exception.h>
#include <folly/Function.h>
#include <folly/MapUtil.h>
#include <folly/Portability.h>
#include <folly/ScopeGuard.h>
#include <folly/SharedMutex.h>
#include <folly/container/Foreach.h>
#include <folly/detail/StaticSingletonManager.h>
#include <folly/detail/UniqueInstance.h>
#include <folly/lang/Exception.h>
#include <folly/memory/Malloc.h>
#include <folly/portability/PThread.h>
#include <folly/synchronization/MicroSpinLock.h>
#include <folly/synchronization/RelaxedAtomic.h>
#include <folly/system/AtFork.h>
#include <folly/system/ThreadId.h>

namespace folly {

enum class TLPDestructionMode { THIS_THREAD, ALL_THREADS };
struct AccessModeStrict {};

namespace threadlocal_detail {

constexpr uint32_t kEntryIDInvalid = std::numeric_limits<uint32_t>::max();

/**
 * POD wrapper around an element (a void*) and an associated deleter.
 * This must be POD, as we memset() it to 0 and memcpy() it around.
 */
struct ElementWrapper {
  using DeleterFunType = void(void*, TLPDestructionMode);

  bool dispose(TLPDestructionMode mode) {
    if (ptr == nullptr) {
      return false;
    }

    DCHECK(deleter1 != nullptr);
    ownsDeleter ? (*deleter2)(ptr, mode) : (*deleter1)(ptr, mode);
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
    DCHECK(deleter1 == nullptr);

    if (!p) {
      return;
    }
    deleter1 = [](void* pt, TLPDestructionMode) {
      delete static_cast<Ptr>(pt);
    };
    ownsDeleter = false;
    ptr = p;
  }

  template <class Ptr, class Deleter>
  void set(Ptr p, const Deleter& d) {
    DCHECK(ptr == nullptr);
    DCHECK(deleter2 == nullptr);

    if (!p) {
      return;
    }

    auto guard = makeGuard([&] { d(p, TLPDestructionMode::THIS_THREAD); });
    deleter2 = new std::function<DeleterFunType>(
        [d](void* pt, TLPDestructionMode mode) {
          d(static_cast<Ptr>(pt), mode);
        });
    guard.dismiss();
    ownsDeleter = true;
    ptr = p;
  }

  void cleanup() {
    if (ownsDeleter) {
      delete deleter2;
    }
    ptr = nullptr;
    deleter1 = nullptr;
    ownsDeleter = false;
  }

  void* ptr;
  union {
    DeleterFunType* deleter1;
    std::function<DeleterFunType>* deleter2;
  };
  bool ownsDeleter;
};

struct StaticMetaBase;
struct ThreadEntryList;

/**
 * Per-thread entry.  Each thread using a StaticMeta object has one.
 * This is written from the owning thread only (under the lock), read
 * from the owning thread (no lock necessary), and read from other threads
 * (under the lock).
 */
struct ThreadEntry {
  ElementWrapper* elements{nullptr};
  std::atomic<size_t> elementsCapacity{0};
  ThreadEntryList* list{nullptr};
  ThreadEntry* listNext{nullptr};
  StaticMetaBase* meta{nullptr};
  bool removed_{false};
  uint64_t tid_os{};
  aligned_storage_for_t<std::thread::id> tid_data{};

  size_t getElementsCapacity() const noexcept {
    return elementsCapacity.load(std::memory_order_relaxed);
  }

  void setElementsCapacity(size_t capacity) noexcept {
    elementsCapacity.store(capacity, std::memory_order_relaxed);
  }

  std::thread::id& tid() {
    return *reinterpret_cast<std::thread::id*>(&tid_data);
  }

  /*
   * Releases element from ThreadEntry::elements at index @id.
   */
  void* releaseElement(uint32_t id);

  /*
   * Clean up element from ThreadEntry::elements at index @id.
   */
  void cleanupElementAndSetThreadEntry(uint32_t id, bool validThreadEntry);

  /*
   * Templated methods to deal with reset with and without a deleter
   * for the element @id
   */
  template <class Ptr>
  void resetElement(Ptr p, uint32_t id);

  template <class Ptr, class Deleter>
  void resetElement(Ptr p, Deleter& d, uint32_t id);
};

struct ThreadEntryList {
  ThreadEntry* head{nullptr};
  size_t count{0};
};

/**
 * We want to disable onThreadExit call at the end of shutdown, we don't care
 * about leaking memory at that point.
 *
 * Otherwise if ThreadLocal is used in a shared library, onThreadExit may be
 * called after dlclose().
 *
 * This class has one single static instance; however since it's so widely used,
 * directly or indirectly, by so many classes, we need to take care to avoid
 * problems stemming from the Static Initialization/Destruction Order Fiascos.
 * Therefore this class needs to be constexpr-constructible, so as to avoid
 * the need for this to participate in init/destruction order.
 */
class PthreadKeyUnregister {
 public:
  static constexpr size_t kMaxKeys = size_t(1) << 16;

  ~PthreadKeyUnregister() {
    // If static constructor priorities are not supported then
    // ~PthreadKeyUnregister logic is not safe.
#if !defined(__APPLE__) && !defined(_MSC_VER)
    MSLGuard lg(lock_);
    while (size_) {
      pthread_key_delete(keys_[--size_]);
    }
#endif
  }

  static void registerKey(pthread_key_t key) { instance_.registerKeyImpl(key); }

 private:
  /**
   * Only one global instance should exist, hence this is private.
   * See also the important note at the top of this class about `constexpr`
   * usage.
   */
  constexpr PthreadKeyUnregister() : lock_(), size_(0), keys_() {}

  void registerKeyImpl(pthread_key_t key) {
    MSLGuard lg(lock_);
    if (size_ == kMaxKeys) {
      throw_exception<std::logic_error>(
          "pthread_key limit has already been reached");
    }
    keys_[size_++] = key;
  }

  MicroSpinLock lock_;
  size_t size_;
  pthread_key_t keys_[kMaxKeys];

  static PthreadKeyUnregister instance_;
};

// ThreadEntrySet is used to track all ThreadEntry that have a valid
// ElementWrapper for a particular TL id. The class provides no internal locking
// and caller must ensure safety of any access.
struct ThreadEntrySet {
  // Vector of ThreadEntry for fast iteration during accessAllThreads.
  using EntryVector = std::vector<ThreadEntry*>;
  EntryVector threadEntries;
  // Map from ThreadEntry* to its slot in the threadEntries vector to be able
  // to remove an entry quickly.
  std::unordered_map<ThreadEntry*, EntryVector::size_type> entryToVectorSlot;

  bool basicSanity() {
    return threadEntries.size() == entryToVectorSlot.size();
  }

  void clear() {
    DCHECK(basicSanity());
    entryToVectorSlot.clear();
    threadEntries.clear();
  }

  bool contains(ThreadEntry* entry) {
    DCHECK(basicSanity());
    return entryToVectorSlot.find(entry) != entryToVectorSlot.end();
  }

  bool insert(ThreadEntry* entry) {
    DCHECK(basicSanity());
    auto iter = entryToVectorSlot.find(entry);
    if (iter != entryToVectorSlot.end()) {
      // Entry already present. Sanity check and exit.
      DCHECK_EQ(entry, threadEntries[iter->second]);
      return false;
    }
    threadEntries.push_back(entry);
    auto idx = threadEntries.size() - 1;
    entryToVectorSlot[entry] = idx;
    return true;
  }

  bool erase(ThreadEntry* entry) {
    DCHECK(basicSanity());
    auto iter = entryToVectorSlot.find(entry);
    if (iter == entryToVectorSlot.end()) {
      // Entry not present.
      return false;
    }
    auto idx = iter->second;
    DCHECK_LT(idx, threadEntries.size());
    entryToVectorSlot.erase(iter);
    if (idx != threadEntries.size() - 1) {
      ThreadEntry* last = threadEntries.back();
      threadEntries[idx] = last;
      entryToVectorSlot[last] = idx;
    }
    threadEntries.pop_back();
    DCHECK(basicSanity());
    return true;
  }
};

struct StaticMetaBase {
  // In general, emutls cleanup is not guaranteed to play nice with the way
  // StaticMeta mixes direct pthread calls and the use of __thread. This has
  // caused problems on multiple platforms so don't use __thread there.
  //
  // XXX: Ideally we would instead determine if emutls is in use at runtime as
  // it is possible to configure glibc on Linux to use emutls regardless.
  static constexpr bool kUseThreadLocal = !kIsMobile && !kIsApple && !kMscVer;

  // Represents an ID of a thread local object. Initially set to the maximum
  // uint. This representation allows us to avoid a branch in accessing TLS data
  // (because if you test capacity > id if id = maxint then the test will always
  // fail). It allows us to keep a constexpr constructor and avoid SIOF.
  class EntryID {
   public:
    std::atomic<uint32_t> value;

    constexpr EntryID() : value(kEntryIDInvalid) {}

    EntryID(EntryID&& other) noexcept : value(other.value.load()) {
      other.value = kEntryIDInvalid;
    }

    EntryID& operator=(EntryID&& other) noexcept {
      assert(this != &other);
      value = other.value.load();
      other.value = kEntryIDInvalid;
      return *this;
    }

    EntryID(const EntryID& other) = delete;
    EntryID& operator=(const EntryID& other) = delete;

    uint32_t getOrInvalid() { return value.load(std::memory_order_acquire); }

    uint32_t getOrAllocate(StaticMetaBase& meta) {
      uint32_t id = getOrInvalid();
      if (id != kEntryIDInvalid) {
        return id;
      }
      // The lock inside allocate ensures that a single value is allocated
      return meta.allocate(this);
    }
  };

  StaticMetaBase(ThreadEntry* (*threadEntry)(), bool strict);

  FOLLY_EXPORT static ThreadEntryList* getThreadEntryList();

  static bool dying();

  static void onThreadExit(void* ptr);

  // Helper to do final free and delete of ThreadEntry and ThreadEntryList
  // structures.
  static void cleanupThreadEntriesAndList(ThreadEntryList* list);

  // returns the elementsCapacity for the
  // current thread ThreadEntry struct
  uint32_t elementsCapacity() const;

  uint32_t allocate(EntryID* ent);

  void destroy(EntryID* ent);

  /**
   * Reserve enough space in the ThreadEntry::elements for the item
   * @id to fit in.
   */
  void reserve(EntryID* id);

  ElementWrapper& getElement(EntryID* ent);

  /*
   * Helper inline methods to add/remove/clear ThreadEntry* from
   * allThreadEntryMap_
   */

  /*
   * Add a ThreadEntry* to the map of allThreadEntryMap_
   * for a given slot @id in ThreadEntry::elements that is
   * used. This should be called under a lock by the owning thread.
   */
  FOLLY_ALWAYS_INLINE void addThreadEntryToMapLocked(
      ThreadEntry* te, uint32_t id) {
    DCHECK_NE(te->removed_, true);
    allThreadEntryMap_[id].insert(te);
  }

  /*
   * Add a ThreadEntry* to the map of allThreadEntryMap_
   * for a given slot @id in ThreadEntry::elements that is
   * used. This the locked version.
   */
  FOLLY_ALWAYS_INLINE void addThreadEntryToMap(ThreadEntry* te, uint32_t id) {
    {
      std::lock_guard<std::mutex> g(lock_);
      addThreadEntryToMapLocked(te, id);
    }
  }

  /*
   * Remove a ThreadEntry* from the map of allThreadEntryMap_
   * for a all slot @id's in ThreadEntry::elements that are
   * used. This is essentially clearing out a ThreadEntry entirely
   * from the allThreadEntryMap_.
   * This should be called under a lock by the owning thread.
   * The unlocked version is used in place where the meta
   * lock_ is already held.
   */
  FOLLY_ALWAYS_INLINE void removeThreadEntryFromAllInMapLocked(
      ThreadEntry* te) {
    for (auto& [e, teSet] : allThreadEntryMap_) {
      teSet.erase(te);
    }
  }

  /*
   * Remove a ThreadEntry* from the map of allThreadEntryMap_
   * for a all slot @id's in ThreadEntry::elements that are
   * used. This is essentially clearing out a ThreadEntry entirely
   * from the allThreadEntryMap_. This is the locked version.
   */
  FOLLY_ALWAYS_INLINE void removeThreadEntryFromAllInMap(ThreadEntry* te) {
    std::lock_guard<std::mutex> g(lock_);
    removeThreadEntryFromAllInMapLocked(te);
  }

  /*
   * Remove a ThreadEntry* from the map of allThreadEntryMap_
   * for a given slot @id in ThreadEntry::elements that is
   * being freed. We only need the locked version.
   */
  FOLLY_ALWAYS_INLINE void removeThreadEntryFromIdInMapLocked(
      ThreadEntry* te, uint32_t id) {
    std::lock_guard<std::mutex> g(lock_);
    if (auto ptr = get_ptr(allThreadEntryMap_, id)) {
      ptr->erase(te);
    }
  }

  /*
   * Clear the Set containing all ThreadEntry*'s for a given slot @id.
   * This is always called in a locked context. Do not use this directly
   * without holding the meta lock_.
   */
  FOLLY_ALWAYS_INLINE void clearSetforIdInMapLocked(uint32_t id) {
    allThreadEntryMap_[id].clear();
  }

  /*
   * Check if ThreadEntry* is present in the map for all slots of @ids.
   * This is always called in a locked context, if not results in data races
   */
  FOLLY_ALWAYS_INLINE bool isThreadEntryRemovedFromAllInMap(ThreadEntry* te) {
    std::lock_guard<std::mutex> g(lock_);
    for (auto& [e, teSet] : allThreadEntryMap_) {
      if (teSet.contains(te)) {
        return false;
      }
    }
    return true;
  }

  // static helper method to reallocate the ThreadEntry::elements
  // returns != nullptr if the ThreadEntry::elements was reallocated
  // nullptr if the ThreadEntry::elements was just extended
  // and throws stdd:bad_alloc if memory cannot be allocated
  static ElementWrapper* reallocate(
      ThreadEntry* threadEntry, uint32_t idval, size_t& newCapacity);

  relaxed_atomic_uint32_t nextId_;
  std::vector<uint32_t> freeIds_;
  std::mutex lock_;
  mutable SharedMutex accessAllThreadsLock_;
  pthread_key_t pthreadKey_;
  ThreadEntry* (*threadEntry_)();
  bool strict_;
  // Total size of ElementWrapper arrays across all threads. This is meant
  // to surface the overhead of thread local tracking machinery since the array
  // can be sparse when there are lots of thread local variables under the same
  // tag.
  relaxed_atomic_int64_t totalElementWrappers_{0};
  // This is a map of all thread entries mapped to index i with active
  // elements[i];
  std::unordered_map<uint32_t, ThreadEntrySet> allThreadEntryMap_;
};

struct FakeUniqueInstance {
  template <template <typename...> class Z, typename... Key, typename... Mapped>
  FOLLY_ERASE constexpr explicit FakeUniqueInstance(
      tag_t<Z<Key..., Mapped...>>, tag_t<Key...>, tag_t<Mapped...>) noexcept {}
};

/*
 * Resets element from ThreadEntry::elements at index @id.
 * call set() on the element to reset it.
 * This is a templated method for when a deleter is not provided.
 */
template <class Ptr>
void ThreadEntry::resetElement(Ptr p, uint32_t id) {
  auto validThreadEntry = (p != nullptr && !removed_);
  cleanupElementAndSetThreadEntry(id, validThreadEntry);
  elements[id].set(p);
}

/*
 * Resets element from ThreadEntry::elements at index @id.
 * call set() on the element to reset it.
 * This is a templated method for when a deleter is not provided.
 */
template <class Ptr, class Deleter>
void ThreadEntry::resetElement(Ptr p, Deleter& d, uint32_t id) {
  auto validThreadEntry = (p != nullptr && !removed_);
  cleanupElementAndSetThreadEntry(id, validThreadEntry);
  elements[id].set(p, d);
}

// Held in a singleton to track our global instances.
// We have one of these per "Tag", by default one for the whole system
// (Tag=void).
//
// Creating and destroying ThreadLocalPtr objects, as well as thread exit
// for threads that use ThreadLocalPtr objects collide on a lock inside
// StaticMeta; you can specify multiple Tag types to break that lock.
template <class Tag, class AccessMode>
struct FOLLY_EXPORT StaticMeta final : StaticMetaBase {
 private:
  static constexpr bool IsTagVoid = std::is_void_v<Tag>;
  static constexpr bool IsAccessModeStrict =
      std::is_same_v<AccessMode, AccessModeStrict>;
  static_assert(!IsTagVoid || !IsAccessModeStrict);

  using UniqueInstance =
      conditional_t<IsTagVoid, FakeUniqueInstance, detail::UniqueInstance>;
  static UniqueInstance unique;

 public:
  StaticMeta()
      : StaticMetaBase(&StaticMeta::getThreadEntrySlow, IsAccessModeStrict) {
    AtFork::registerHandler(
        this,
        /*prepare*/ &StaticMeta::preFork,
        /*parent*/ &StaticMeta::onForkParent,
        /*child*/ &StaticMeta::onForkChild);
  }

  static StaticMeta<Tag, AccessMode>& instance() {
    (void)unique; // force the object not to be thrown out as unused
    // Leak it on exit, there's only one per process and we don't have to
    // worry about synchronization with exiting threads.
    return detail::createGlobal<StaticMeta<Tag, AccessMode>, void>();
  }

  FOLLY_EXPORT FOLLY_ALWAYS_INLINE static ElementWrapper& get(EntryID* ent) {
    // Eliminate as many branches and as much extra code as possible in the
    // cached fast path, leaving only one branch here and one indirection
    // below.

    ThreadEntry* te = getThreadEntry(ent);
    uint32_t id = ent->getOrInvalid();
    // Only valid index into the the elements array
    DCHECK_NE(id, kEntryIDInvalid);
    return te->elements[id];
  }

  /*
   * In order to facilitate adding/clearing ThreadEntry* to
   * StaticMetaBase::allThreadEntryMap_ during ThreadLocalPtr reset()/release()
   * we need access to the ThreadEntry* directly. This allows for direct
   * interaction with StaticMetaBase::allThreadEntryMap_. We keep
   * StaticMetaBase::allThreadEntryMap_ updated with ThreadEntry* whenever a
   * ThreadLocal is set/released.
   */
  FOLLY_EXPORT FOLLY_ALWAYS_INLINE static ThreadEntry* getThreadEntry(
      EntryID* ent) {
    // Eliminate as many branches and as much extra code as possible in the
    // cached fast path, leaving only one branch here and one indirection below.
    uint32_t id = ent->getOrInvalid();
    static thread_local ThreadEntry* threadEntryTL{};
    ThreadEntry* threadEntryNonTL{};
    auto& threadEntry = kUseThreadLocal ? threadEntryTL : threadEntryNonTL;

    static thread_local size_t capacityTL{};
    size_t capacityNonTL{};
    auto& capacity = kUseThreadLocal ? capacityTL : capacityNonTL;

    if (FOLLY_UNLIKELY(capacity <= id)) {
      getSlowReserveAndCache(ent, id, threadEntry, capacity);
    }
    return threadEntry;
  }

  FOLLY_NOINLINE static void getSlowReserveAndCache(
      EntryID* ent, uint32_t& id, ThreadEntry*& threadEntry, size_t& capacity) {
    auto& inst = instance();
    threadEntry = inst.threadEntry_();
    if (FOLLY_UNLIKELY(threadEntry->getElementsCapacity() <= id)) {
      inst.reserve(ent);
      id = ent->getOrInvalid();
    }
    capacity = threadEntry->getElementsCapacity();
    assert(capacity > id);
  }

  FOLLY_EXPORT FOLLY_NOINLINE static ThreadEntry* getThreadEntrySlow() {
    auto& meta = instance();
    auto key = meta.pthreadKey_;
    ThreadEntry* threadEntry =
        static_cast<ThreadEntry*>(pthread_getspecific(key));
    if (!threadEntry) {
      ThreadEntryList* threadEntryList = StaticMeta::getThreadEntryList();
      threadEntry = new ThreadEntry();
      // if the ThreadEntry already exists
      // but pthread_getspecific returns NULL
      // do not add the same entry twice to the list
      // since this would create a loop in the list
      if (!threadEntry->list) {
        threadEntry->list = threadEntryList;
        threadEntry->listNext = threadEntryList->head;
        threadEntryList->head = threadEntry;
      }

      threadEntry->tid() = std::this_thread::get_id();
      threadEntry->tid_os = folly::getOSThreadID();

      // if we're adding a thread entry
      // we need to increment the list count
      // even if the entry is reused
      threadEntryList->count++;

      threadEntry->meta = &meta;
      int ret = pthread_setspecific(key, threadEntry);
      checkPosixError(ret, "pthread_setspecific failed");
    }
    return threadEntry;
  }

  static bool preFork() {
    return instance().lock_.try_lock(); // Make sure it's created
  }

  static void onForkParent() { instance().lock_.unlock(); }

  static void onForkChild() {
    // only the current thread survives
    auto& meta = instance();
    auto threadEntry = meta.threadEntry_();
    // Loop through allThreadEntryMap_; Only keep ThreadEntry* in the map
    // for ThreadEntry::elements that are still in use by the current thread.
    // Evict all of the ThreadEntry* from other threads.
    for (auto& [e, te] : meta.allThreadEntryMap_) {
      if (te.contains(threadEntry)) {
        meta.clearSetforIdInMapLocked(e);
        meta.addThreadEntryToMapLocked(threadEntry, e);
      } else {
        meta.clearSetforIdInMapLocked(e);
      }
    }
    instance().lock_.unlock();
  }
};

FOLLY_PUSH_WARNING
FOLLY_CLANG_DISABLE_WARNING("-Wglobal-constructors")
template <typename Tag, typename AccessMode>
typename StaticMeta<Tag, AccessMode>::UniqueInstance
    StaticMeta<Tag, AccessMode>::unique{
        tag<StaticMeta>, tag<Tag>, tag<AccessMode>};
FOLLY_POP_WARNING

} // namespace threadlocal_detail
} // namespace folly
