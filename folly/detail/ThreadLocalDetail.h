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
#include <memory>
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
#include <folly/Synchronized.h>
#include <folly/concurrency/container/atomic_grow_array.h>
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

//  as a memory-usage optimization, try to make this deleter fit in-situ in
//  the deleter function storage rather than being heap-allocated separately
//
//  for libstdc++, specialization below of std::__is_location_invariant
//
//  TODO: ensure in-situ storage for other standard-library implementations
struct SharedPtrDeleter {
  mutable std::shared_ptr<void> ts_;
  explicit SharedPtrDeleter(std::shared_ptr<void> const& ts) noexcept;
  SharedPtrDeleter(SharedPtrDeleter const& that) noexcept;
  void operator=(SharedPtrDeleter const& that) = delete;
  ~SharedPtrDeleter();
  void operator()(void* ptr, folly::TLPDestructionMode) const;
};

} // namespace threadlocal_detail

} // namespace folly

#if defined(__GLIBCXX__)

namespace std {

template <>
struct __is_location_invariant<::folly::threadlocal_detail::SharedPtrDeleter>
    : std::true_type {};

} // namespace std

#endif

namespace folly {

namespace threadlocal_detail {

/**
 * POD wrapper around an element (a void*) and an associated deleter.
 * This must be POD, as we memset() it to 0 and memcpy() it around.
 */
struct ElementWrapper {
  using DeleterFunType = void(void*, TLPDestructionMode);
  using DeleterObjType = std::function<DeleterFunType>;

  static inline constexpr auto deleter_obj_mask = uintptr_t(0b01);
  static inline constexpr auto deleter_all_mask = uintptr_t(0) //
      | deleter_obj_mask //
      ;

  static_assert(alignof(DeleterObjType) > deleter_all_mask);

  //  must be noinline and must launder: https://godbolt.org/z/bo6f7f6v6
  FOLLY_NOINLINE static uintptr_t castForgetAlign(DeleterFunType*) noexcept;

  bool dispose(TLPDestructionMode mode) noexcept {
    if (ptr == nullptr) {
      return false;
    }

    DCHECK_NE(0, deleter);
    auto const deleter_masked = deleter & ~deleter_all_mask;
    if (deleter & deleter_obj_mask) {
      auto& obj = *reinterpret_cast<DeleterObjType*>(deleter_masked);
      obj(ptr, mode);
    } else {
      auto& fun = *reinterpret_cast<DeleterFunType*>(deleter_masked);
      fun(ptr, mode);
    }
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
    DCHECK_EQ(static_cast<void*>(nullptr), ptr);
    DCHECK_EQ(0, deleter);

    if (!p) {
      return;
    }
    auto const fun = +[](void* pt, TLPDestructionMode) {
      delete static_cast<Ptr>(pt);
    };
    auto const raw = castForgetAlign(fun);
    if (raw & deleter_all_mask) {
      return set(p, std::ref(*fun));
    }
    DCHECK_EQ(0, raw & deleter_all_mask);
    deleter = raw;
    ptr = p;
  }

  template <typename Ptr, typename Deleter>
  static auto makeDeleter(const Deleter& d) {
    return [d](void* pt, TLPDestructionMode mode) {
      d(static_cast<Ptr>(pt), mode);
    };
  }

  template <typename Ptr>
  static decltype(auto) makeDeleter(const SharedPtrDeleter& d) {
    return d;
  }

  template <class Ptr, class Deleter>
  void set(Ptr p, const Deleter& d) {
    DCHECK_EQ(static_cast<void*>(nullptr), ptr);
    DCHECK_EQ(0, deleter);

    if (!p) {
      return;
    }

    auto guard = makeGuard([&] { d(p, TLPDestructionMode::THIS_THREAD); });
    auto const obj = new DeleterObjType(makeDeleter<Ptr>(d));
    guard.dismiss();
    auto const raw = reinterpret_cast<uintptr_t>(obj);
    DCHECK_EQ(0, raw & deleter_all_mask);
    deleter = raw | deleter_obj_mask;
    ptr = p;
  }

  void cleanup() noexcept {
    if (deleter & deleter_obj_mask) {
      auto const deleter_masked = deleter & ~deleter_all_mask;
      auto const obj = reinterpret_cast<DeleterObjType*>(deleter_masked);
      delete obj;
    }
    ptr = nullptr;
    deleter = 0;
  }

  void* ptr;
  uintptr_t deleter;
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
  void cleanupElement(uint32_t id);

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
  using EntryIndex = std::unordered_map<ThreadEntry*, EntryVector::size_type>;
  EntryIndex entryToVectorSlot;

  bool basicSanity() const;

  void clear() {
    DCHECK(basicSanity());
    entryToVectorSlot.clear();
    threadEntries.clear();
  }

  bool contains(ThreadEntry* entry) const {
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
    if (compressible()) {
      compress();
    }
    DCHECK(basicSanity());
    return true;
  }

  /// compressible
  ///
  /// If many elements have been removed, then size might be much less than
  /// capacity and it becomes possible to reduce memory usage.
  bool compressible() const {
    // We choose a sufficiently-large multiplier so that there is no risk of a
    // following insert growing the vector and then a following erase shrinking
    // the vector, since that way lies non-amortized-O(N)-complexity costs for
    // both insert and erase ops.
    constexpr size_t const mult = 4;
    auto& vec = threadEntries;
    return std::max(size_t(1), vec.size()) * mult <= vec.capacity();
  }
  /// compress
  ///
  /// Attempt to reduce the memory usage of the data structure.
  void compress();
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
      DCHECK(value.load() == kEntryIDInvalid);
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

  using SynchronizedThreadEntrySet = folly::Synchronized<ThreadEntrySet>;

  /*
   * Helper inline methods to add/remove/clear ThreadEntry* from
   * allId2ThreadEntrySets_
   */

  /*
   * Return true if given ThreadEntry is already present in the ThreadEntrySet
   * for the given id.
   */
  FOLLY_ALWAYS_INLINE bool isThreadEntryInSet(ThreadEntry* te, uint32_t id) {
    return allId2ThreadEntrySets_[id].rlock()->contains(te);
  }

  /*
   * Ensure the given ThreadEntry* is present in the tracking set for the
   * given id. Once added, we do not remove it until the thread exits or the
   * whole set is reaped when the TL id itself is destroyed.
   *
   * Note: Call may drop and reacquire the read lock.
   * If the provided entry is not already in the set, the given RLockedPtr will
   * be released, entry added under a WLockedPtr, and RLockedPtr reacquired
   * before returning.
   */
  FOLLY_NOINLINE void ensureThreadEntryIsInSet(
      ThreadEntry* te,
      SynchronizedThreadEntrySet& set,
      SynchronizedThreadEntrySet::RLockedPtr& rlock);

  /*
   * Remove a ThreadEntry* from the map of allId2ThreadEntrySets_
   * for all slot @id's in ThreadEntry::elements that are
   * used. This is essentially clearing out a ThreadEntry entirely
   * from the allId2ThreadEntrySets_.
   */
  FOLLY_ALWAYS_INLINE void removeThreadEntryFromAllInMap(ThreadEntry* te) {
    for (const auto ptr : getThreadEntrySetsPtrSpan()) {
      auto& set = *ptr;
      set.wlock()->erase(te);
    }
  }

  /*
   * Check if ThreadEntry* is present in the map for all slots of @ids.
   */
  FOLLY_ALWAYS_INLINE bool isThreadEntryRemovedFromAllInMap(
      ThreadEntry* te, bool needForkLock) {
    std::shared_lock rlocked(forkHandlerLock_, std::defer_lock);
    if (needForkLock) {
      rlocked.lock();
    }
    for (const auto ptr : getThreadEntrySetsPtrSpan()) {
      auto& set = *ptr;
      if (set.rlock()->contains(te)) {
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

  span<SynchronizedThreadEntrySet* const> getThreadEntrySetsPtrSpan() {
    const auto sets = allId2ThreadEntrySets_.as_view().as_ptr_span();
    const size_t nextId = nextId_.load();
    return sets.subspan(0, std::min(sets.size(), nextId));
  }

  relaxed_atomic_uint32_t nextId_;
  std::vector<uint32_t> freeIds_;
  std::mutex lock_;
  mutable SharedMutex accessAllThreadsLock_;
  // As part of handling fork, we need to ensure no locks used by ThreadLocal
  // implementation are held by threads other than the one forking. The total
  // number of locks involved is large due to the per ThreadEntrySet lock. TSAN
  // builds have to track each lock acquire and release. TSAN also has its own
  // fork handler. Using a lot of locks in fork handler can end up deadlocking
  // TSAN. To avoid that behavior, we the forkHandlerLock_. All code paths that
  // acquire a lock on any ThreadEntrySet (accessAllThreads() or reset() calls)
  // must also acquire a shared lock on forkHandlerLock_.
  // Fork handler will acquire an exclusive lock on forkHandlerLock_,
  // along with exclusive locks on accessAllThreadsLock_ and lock_.
  mutable SharedMutex forkHandlerLock_;
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
  folly::atomic_grow_array<SynchronizedThreadEntrySet> allId2ThreadEntrySets_;

  // Note on locking rules. There are 4 locks involved in managing StaticMeta:
  // fork handler lock (getStaticMetaGlobalForkMutex(),
  // access all threads lock (accessAllThreadsLock_),
  // per thread entry set lock implicit in SynchronizedThreadEntrySet and
  // meta lock (lock_)
  //
  // If multiple locks need to be acquired in a call path, the above is also
  // the order in which they should be acquired. Additionally, if per
  // ThreadEntrySet locks are the only ones that are acquired in a path, it
  // must also acquire shared lock on the fork handler lock.
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
  auto& set = meta->allId2ThreadEntrySets_[id];
  auto rlock = set.rlock();
  if (p != nullptr && !removed_ && !rlock->contains(this)) {
    meta->ensureThreadEntryIsInSet(this, set, rlock);
  }
  cleanupElement(id);
  elements[id].set(p);
}

/*
 * Resets element from ThreadEntry::elements at index @id.
 * call set() on the element to reset it.
 * This is a templated method for when a deleter is not provided.
 */
template <class Ptr, class Deleter>
void ThreadEntry::resetElement(Ptr p, Deleter& d, uint32_t id) {
  auto& set = meta->allId2ThreadEntrySets_[id];
  auto rlock = set.rlock();
  if (p != nullptr && !removed_ && !rlock->contains(this)) {
    meta->ensureThreadEntryIsInSet(this, set, rlock);
  }
  cleanupElement(id);
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

  struct LocalCache {
    ThreadEntry* threadEntry;
    size_t capacity;
  };
  static_assert(std::is_standard_layout_v<LocalCache>);
  static_assert(std::is_trivial_v<LocalCache>);

  FOLLY_EXPORT FOLLY_ALWAYS_INLINE static LocalCache& getLocalCache() {
    static thread_local LocalCache instance;
    return instance;
  }

  FOLLY_ALWAYS_INLINE static ElementWrapper& get(EntryID* ent) {
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
   * StaticMetaBase::allId2ThreadEntrySets_ during ThreadLocalPtr
   * reset()/release() we need access to the ThreadEntry* directly. This allows
   * for direct interaction with StaticMetaBase::allId2ThreadEntrySets_. We keep
   * StaticMetaBase::allId2ThreadEntrySets_ updated with ThreadEntry* whenever a
   * ThreadLocal is set/released.
   */
  FOLLY_ALWAYS_INLINE static ThreadEntry* getThreadEntry(EntryID* ent) {
    if (!kUseThreadLocal) {
      return getThreadEntrySlowReserve(ent);
    }

    // Eliminate as many branches and as much extra code as possible in the
    // cached fast path, leaving only one branch here and one indirection below.
    uint32_t id = ent->getOrInvalid();
    auto& cache = getLocalCache();
    if (FOLLY_UNLIKELY(cache.capacity <= id)) {
      getSlowReserveAndCache(ent, cache);
    }
    return cache.threadEntry;
  }

  FOLLY_NOINLINE static void getSlowReserveAndCache(
      EntryID* ent, LocalCache& cache) {
    auto threadEntry = getThreadEntrySlowReserve(ent);
    cache.capacity = threadEntry->getElementsCapacity();
    cache.threadEntry = threadEntry;
  }

  FOLLY_NOINLINE static ThreadEntry* getThreadEntrySlowReserve(EntryID* ent) {
    uint32_t id = ent->getOrInvalid();

    auto& inst = instance();
    auto threadEntry = inst.threadEntry_();
    if (FOLLY_UNLIKELY(threadEntry->getElementsCapacity() <= id)) {
      inst.reserve(ent);
      id = ent->getOrInvalid();
    }
    assert(threadEntry->getElementsCapacity() > id);
    return threadEntry;
  }

  FOLLY_EXPORT FOLLY_NOINLINE static ThreadEntry* getThreadEntrySlow() {
    auto& meta = instance();
    auto key = meta.pthreadKey_;
    ThreadEntry* threadEntry =
        static_cast<ThreadEntry*>(pthread_getspecific(key));
    if (!threadEntry) {
      ThreadEntryList* threadEntryList = StaticMeta::getThreadEntryList();
      threadEntry = new ThreadEntry();

      threadEntry->list = threadEntryList;
      threadEntry->listNext = threadEntryList->head;
      threadEntryList->head = threadEntry;

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
    auto& meta = instance();
    bool gotLock = meta.forkHandlerLock_.try_lock(); // Make sure it's created
    if (!gotLock) {
      return false;
    }
    meta.accessAllThreadsLock_.lock();
    meta.lock_.lock();
    // Okay to not lock each set in meta.allId2ThreadEntrySets
    // as accessAllThreadsLock_ in held by calls to reset() and
    // accessAllThreads.
    return true;
  }

  static void onForkParent() {
    auto& meta = instance();
    meta.lock_.unlock();
    meta.accessAllThreadsLock_.unlock();
    meta.forkHandlerLock_.unlock();
  }

  static void onForkChild() {
    auto& meta = instance();
    // only the current thread survives
    meta.lock_.unlock();
    meta.accessAllThreadsLock_.unlock();
    auto threadEntry = meta.threadEntry_();
    // Loop through allId2ThreadEntrySets_; Only keep ThreadEntry* in the map
    // for ThreadEntry::elements that are still in use by the current thread.
    // Evict all of the ThreadEntry* from other threads.
    for (const auto ptr : meta.getThreadEntrySetsPtrSpan()) {
      auto& set = *ptr;
      auto wlockedSet = set.wlock();
      if (wlockedSet->contains(threadEntry)) {
        wlockedSet->clear();
        wlockedSet->insert(threadEntry);
      } else {
        wlockedSet->clear();
      }
    }
    meta.forkHandlerLock_.unlock();
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
