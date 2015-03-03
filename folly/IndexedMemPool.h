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

#ifndef FOLLY_INDEXEDMEMPOOL_H
#define FOLLY_INDEXEDMEMPOOL_H

#include <type_traits>
#include <stdint.h>
#include <assert.h>
#include <unistd.h>
#include <sys/mman.h>
#include <boost/noncopyable.hpp>
#include <folly/AtomicStruct.h>
#include <folly/detail/CacheLocality.h>

// Ignore shadowing warnings within this file, so includers can use -Wshadow.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

namespace folly {

namespace detail {
template <typename Pool>
struct IndexedMemPoolRecycler;
}

/// Instances of IndexedMemPool dynamically allocate and then pool their
/// element type (T), returning 4-byte integer indices that can be passed
/// to the pool's operator[] method to access or obtain pointers to the
/// actual elements.  The memory backing items returned from the pool
/// will always be readable, even if items have been returned to the pool.
/// These two features are useful for lock-free algorithms.  The indexing
/// behavior makes it easy to build tagged pointer-like-things, since
/// a large number of elements can be managed using fewer bits than a
/// full pointer.  The access-after-free behavior makes it safe to read
/// from T-s even after they have been recycled, since it is guaranteed
/// that the memory won't have been returned to the OS and unmapped
/// (the algorithm must still use a mechanism to validate that the read
/// was correct, but it doesn't have to worry about page faults), and if
/// the elements use internal sequence numbers it can be guaranteed that
/// there won't be an ABA match due to the element being overwritten with
/// a different type that has the same bit pattern.
///
/// IndexedMemPool has two object lifecycle strategies.  The first
/// is to construct objects when they are allocated from the pool and
/// destroy them when they are recycled.  In this mode allocIndex and
/// allocElem have emplace-like semantics.  In the second mode, objects
/// are default-constructed the first time they are removed from the pool,
/// and deleted when the pool itself is deleted.  By default the first
/// mode is used for non-trivial T, and the second is used for trivial T.
///
/// IMPORTANT: Space for extra elements is allocated to account for those
/// that are inaccessible because they are in other local lists, so the
/// actual number of items that can be allocated ranges from capacity to
/// capacity + (NumLocalLists_-1)*LocalListLimit_.  This is important if
/// you are trying to maximize the capacity of the pool while constraining
/// the bit size of the resulting pointers, because the pointers will
/// actually range up to the boosted capacity.  See maxIndexForCapacity
/// and capacityForMaxIndex.
///
/// To avoid contention, NumLocalLists_ free lists of limited (less than
/// or equal to LocalListLimit_) size are maintained, and each thread
/// retrieves and returns entries from its associated local list.  If the
/// local list becomes too large then elements are placed in bulk in a
/// global free list.  This allows items to be efficiently recirculated
/// from consumers to producers.  AccessSpreader is used to access the
/// local lists, so there is no performance advantage to having more
/// local lists than L1 caches.
///
/// The pool mmap-s the entire necessary address space when the pool is
/// constructed, but delays element construction.  This means that only
/// elements that are actually returned to the caller get paged into the
/// process's resident set (RSS).
template <typename T,
          int NumLocalLists_ = 32,
          int LocalListLimit_ = 200,
          template<typename> class Atom = std::atomic,
          bool EagerRecycleWhenTrivial = false,
          bool EagerRecycleWhenNotTrivial = true>
struct IndexedMemPool : boost::noncopyable {
  typedef T value_type;

  typedef std::unique_ptr<T, detail::IndexedMemPoolRecycler<IndexedMemPool>>
      UniquePtr;

  static_assert(LocalListLimit_ <= 255, "LocalListLimit must fit in 8 bits");
  enum {
    NumLocalLists = NumLocalLists_,
    LocalListLimit = LocalListLimit_
  };


  static constexpr bool eagerRecycle() {
    return std::is_trivial<T>::value
        ? EagerRecycleWhenTrivial : EagerRecycleWhenNotTrivial;
  }

  // these are public because clients may need to reason about the number
  // of bits required to hold indices from a pool, given its capacity

  static constexpr uint32_t maxIndexForCapacity(uint32_t capacity) {
    // index of uint32_t(-1) == UINT32_MAX is reserved for isAllocated tracking
    return std::min(uint64_t(capacity) + (NumLocalLists - 1) * LocalListLimit,
                    uint64_t(uint32_t(-1) - 1));
  }

  static constexpr uint32_t capacityForMaxIndex(uint32_t maxIndex) {
    return maxIndex - (NumLocalLists - 1) * LocalListLimit;
  }


  /// Constructs a pool that can allocate at least _capacity_ elements,
  /// even if all the local lists are full
  explicit IndexedMemPool(uint32_t capacity)
    : actualCapacity_(maxIndexForCapacity(capacity))
    , size_(0)
    , globalHead_(TaggedPtr{})
  {
    const size_t needed = sizeof(Slot) * (actualCapacity_ + 1);
    long pagesize = sysconf(_SC_PAGESIZE);
    mmapLength_ = ((needed - 1) & ~(pagesize - 1)) + pagesize;
    assert(needed <= mmapLength_ && mmapLength_ < needed + pagesize);
    assert((mmapLength_ % pagesize) == 0);

    slots_ = static_cast<Slot*>(mmap(nullptr, mmapLength_,
                                     PROT_READ | PROT_WRITE,
                                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (slots_ == MAP_FAILED) {
      assert(errno == ENOMEM);
      throw std::bad_alloc();
    }
  }

  /// Destroys all of the contained elements
  ~IndexedMemPool() {
    if (!eagerRecycle()) {
      for (size_t i = size_; i > 0; --i) {
        slots_[i].~Slot();
      }
    }
    munmap(slots_, mmapLength_);
  }

  /// Returns a lower bound on the number of elements that may be
  /// simultaneously allocated and not yet recycled.  Because of the
  /// local lists it is possible that more elements than this are returned
  /// successfully
  size_t capacity() {
    return capacityForMaxIndex(actualCapacity_);
  }

  /// Finds a slot with a non-zero index, emplaces a T there if we're
  /// using the eager recycle lifecycle mode, and returns the index,
  /// or returns 0 if no elements are available.
  template <typename ...Args>
  uint32_t allocIndex(Args&&... args) {
    static_assert(sizeof...(Args) == 0 || eagerRecycle(),
        "emplace-style allocation requires eager recycle, "
        "which is defaulted only for non-trivial types");
    auto idx = localPop(localHead());
    if (idx != 0 && eagerRecycle()) {
      T* ptr = &slot(idx).elem;
      new (ptr) T(std::forward<Args>(args)...);
    }
    return idx;
  }

  /// If an element is available, returns a std::unique_ptr to it that will
  /// recycle the element to the pool when it is reclaimed, otherwise returns
  /// a null (falsy) std::unique_ptr
  template <typename ...Args>
  UniquePtr allocElem(Args&&... args) {
    auto idx = allocIndex(std::forward<Args>(args)...);
    T* ptr = idx == 0 ? nullptr : &slot(idx).elem;
    return UniquePtr(ptr, typename UniquePtr::deleter_type(this));
  }

  /// Gives up ownership previously granted by alloc()
  void recycleIndex(uint32_t idx) {
    assert(isAllocated(idx));
    if (eagerRecycle()) {
      slot(idx).elem.~T();
    }
    localPush(localHead(), idx);
  }

  /// Provides access to the pooled element referenced by idx
  T& operator[](uint32_t idx) {
    return slot(idx).elem;
  }

  /// Provides access to the pooled element referenced by idx
  const T& operator[](uint32_t idx) const {
    return slot(idx).elem;
  }

  /// If elem == &pool[idx], then pool.locateElem(elem) == idx.  Also,
  /// pool.locateElem(nullptr) == 0
  uint32_t locateElem(const T* elem) const {
    if (!elem) {
      return 0;
    }

    static_assert(std::is_standard_layout<Slot>::value, "offsetof needs POD");

    auto slot = reinterpret_cast<const Slot*>(
        reinterpret_cast<const char*>(elem) - offsetof(Slot, elem));
    auto rv = slot - slots_;

    // this assert also tests that rv is in range
    assert(elem == &(*this)[rv]);
    return rv;
  }

  /// Returns true iff idx has been alloc()ed and not recycleIndex()ed
  bool isAllocated(uint32_t idx) const {
    return slot(idx).localNext == uint32_t(-1);
  }


 private:
  ///////////// types

  struct Slot {
    T elem;
    uint32_t localNext;
    uint32_t globalNext;

    Slot() : localNext{}, globalNext{} {}
  };

  struct TaggedPtr {
    uint32_t idx;

    // size is bottom 8 bits, tag in top 24.  g++'s code generation for
    // bitfields seems to depend on the phase of the moon, plus we can
    // do better because we can rely on other checks to avoid masking
    uint32_t tagAndSize;

    enum : uint32_t {
        SizeBits = 8,
        SizeMask = (1U << SizeBits) - 1,
        TagIncr = 1U << SizeBits,
    };

    uint32_t size() const {
      return tagAndSize & SizeMask;
    }

    TaggedPtr withSize(uint32_t repl) const {
      assert(repl <= LocalListLimit);
      return TaggedPtr{ idx, (tagAndSize & ~SizeMask) | repl };
    }

    TaggedPtr withSizeIncr() const {
      assert(size() < LocalListLimit);
      return TaggedPtr{ idx, tagAndSize + 1 };
    }

    TaggedPtr withSizeDecr() const {
      assert(size() > 0);
      return TaggedPtr{ idx, tagAndSize - 1 };
    }

    TaggedPtr withIdx(uint32_t repl) const {
      return TaggedPtr{ repl, tagAndSize + TagIncr };
    }

    TaggedPtr withEmpty() const {
      return withIdx(0).withSize(0);
    }
  };

  struct FOLLY_ALIGN_TO_AVOID_FALSE_SHARING LocalList {
    AtomicStruct<TaggedPtr,Atom> head;

    LocalList() : head(TaggedPtr{}) {}
  };

  ////////// fields

  /// the actual number of slots that we will allocate, to guarantee
  /// that we will satisfy the capacity requested at construction time.
  /// They will be numbered 1..actualCapacity_ (note the 1-based counting),
  /// and occupy slots_[1..actualCapacity_].
  size_t actualCapacity_;

  /// the number of bytes allocated from mmap, which is a multiple of
  /// the page size of the machine
  size_t mmapLength_;

  /// this records the number of slots that have actually been constructed.
  /// To allow use of atomic ++ instead of CAS, we let this overflow.
  /// The actual number of constructed elements is min(actualCapacity_,
  /// size_)
  std::atomic<uint32_t> size_;

  /// raw storage, only 1..min(size_,actualCapacity_) (inclusive) are
  /// actually constructed.  Note that slots_[0] is not constructed or used
  Slot* FOLLY_ALIGN_TO_AVOID_FALSE_SHARING slots_;

  /// use AccessSpreader to find your list.  We use stripes instead of
  /// thread-local to avoid the need to grow or shrink on thread start
  /// or join.   These are heads of lists chained with localNext
  LocalList local_[NumLocalLists];

  /// this is the head of a list of node chained by globalNext, that are
  /// themselves each the head of a list chained by localNext
  AtomicStruct<TaggedPtr,Atom> FOLLY_ALIGN_TO_AVOID_FALSE_SHARING globalHead_;

  ///////////// private methods

  size_t slotIndex(uint32_t idx) const {
    assert(0 < idx &&
           idx <= actualCapacity_ &&
           idx <= size_.load(std::memory_order_acquire));
    return idx;
  }

  Slot& slot(uint32_t idx) {
    return slots_[slotIndex(idx)];
  }

  const Slot& slot(uint32_t idx) const {
    return slots_[slotIndex(idx)];
  }

  // localHead references a full list chained by localNext.  s should
  // reference slot(localHead), it is passed as a micro-optimization
  void globalPush(Slot& s, uint32_t localHead) {
    while (true) {
      TaggedPtr gh = globalHead_.load(std::memory_order_acquire);
      s.globalNext = gh.idx;
      if (globalHead_.compare_exchange_strong(gh, gh.withIdx(localHead))) {
        // success
        return;
      }
    }
  }

  // idx references a single node
  void localPush(AtomicStruct<TaggedPtr,Atom>& head, uint32_t idx) {
    Slot& s = slot(idx);
    TaggedPtr h = head.load(std::memory_order_acquire);
    while (true) {
      s.localNext = h.idx;

      if (h.size() == LocalListLimit) {
        // push will overflow local list, steal it instead
        if (head.compare_exchange_strong(h, h.withEmpty())) {
          // steal was successful, put everything in the global list
          globalPush(s, idx);
          return;
        }
      } else {
        // local list has space
        if (head.compare_exchange_strong(h, h.withIdx(idx).withSizeIncr())) {
          // success
          return;
        }
      }
      // h was updated by failing CAS
    }
  }

  // returns 0 if empty
  uint32_t globalPop() {
    while (true) {
      TaggedPtr gh = globalHead_.load(std::memory_order_acquire);
      if (gh.idx == 0 || globalHead_.compare_exchange_strong(
                  gh, gh.withIdx(slot(gh.idx).globalNext))) {
        // global list is empty, or pop was successful
        return gh.idx;
      }
    }
  }

  // returns 0 if allocation failed
  uint32_t localPop(AtomicStruct<TaggedPtr,Atom>& head) {
    while (true) {
      TaggedPtr h = head.load(std::memory_order_acquire);
      if (h.idx != 0) {
        // local list is non-empty, try to pop
        Slot& s = slot(h.idx);
        if (head.compare_exchange_strong(
                    h, h.withIdx(s.localNext).withSizeDecr())) {
          // success
          s.localNext = uint32_t(-1);
          return h.idx;
        }
        continue;
      }

      uint32_t idx = globalPop();
      if (idx == 0) {
        // global list is empty, allocate and construct new slot
        if (size_.load(std::memory_order_relaxed) >= actualCapacity_ ||
            (idx = ++size_) > actualCapacity_) {
          // allocation failed
          return 0;
        }
        // default-construct it now if we aren't going to construct and
        // destroy on each allocation
        if (!eagerRecycle()) {
          T* ptr = &slot(idx).elem;
          new (ptr) T();
        }
        slot(idx).localNext = uint32_t(-1);
        return idx;
      }

      Slot& s = slot(idx);
      if (head.compare_exchange_strong(
                  h, h.withIdx(s.localNext).withSize(LocalListLimit))) {
        // global list moved to local list, keep head for us
        s.localNext = uint32_t(-1);
        return idx;
      }
      // local bulk push failed, return idx to the global list and try again
      globalPush(s, idx);
    }
  }

  AtomicStruct<TaggedPtr,Atom>& localHead() {
    auto stripe = detail::AccessSpreader<Atom>::current(NumLocalLists);
    return local_[stripe].head;
  }
};

namespace detail {

/// This is a stateful Deleter functor, which allows std::unique_ptr
/// to track elements allocated from an IndexedMemPool by tracking the
/// associated pool.  See IndexedMemPool::allocElem.
template <typename Pool>
struct IndexedMemPoolRecycler {
  Pool* pool;

  explicit IndexedMemPoolRecycler(Pool* pool) : pool(pool) {}

  IndexedMemPoolRecycler(const IndexedMemPoolRecycler<Pool>& rhs)
      = default;
  IndexedMemPoolRecycler& operator= (const IndexedMemPoolRecycler<Pool>& rhs)
      = default;

  void operator()(typename Pool::value_type* elem) const {
    pool->recycleIndex(pool->locateElem(elem));
  }
};

}

} // namespace folly

# pragma GCC diagnostic pop
#endif
