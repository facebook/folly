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

#ifndef FOLLY_ATOMICHASHARRAY_H_
#error "This should only be included by AtomicHashArray.h"
#endif

#include "folly/Bits.h"
#include "folly/detail/AtomicHashUtils.h"

namespace folly {

// AtomicHashArray private constructor --
template <class KeyT, class ValueT, class HashFcn>
AtomicHashArray<KeyT, ValueT, HashFcn>::
AtomicHashArray(size_t capacity, KeyT emptyKey, KeyT lockedKey,
                KeyT erasedKey, double maxLoadFactor, size_t cacheSize)
    : capacity_(capacity), maxEntries_(size_t(maxLoadFactor * capacity_ + 0.5)),
      kEmptyKey_(emptyKey), kLockedKey_(lockedKey), kErasedKey_(erasedKey),
      kAnchorMask_(nextPowTwo(capacity_) - 1), numEntries_(0, cacheSize),
      numPendingEntries_(0, cacheSize), isFull_(0), numErases_(0) {
}

/*
 * findInternal --
 *
 *   Sets ret.second to value found and ret.index to index
 *   of key and returns true, or if key does not exist returns false and
 *   ret.index is set to capacity_.
 */
template <class KeyT, class ValueT, class HashFcn>
typename AtomicHashArray<KeyT, ValueT, HashFcn>::SimpleRetT
AtomicHashArray<KeyT, ValueT, HashFcn>::
findInternal(const KeyT key_in) {
  DCHECK_NE(key_in, kEmptyKey_);
  DCHECK_NE(key_in, kLockedKey_);
  DCHECK_NE(key_in, kErasedKey_);
  for (size_t idx = keyToAnchorIdx(key_in), numProbes = 0;
       ;
       idx = probeNext(idx, numProbes)) {
    const KeyT key = acquireLoadKey(cells_[idx]);
    if (LIKELY(key == key_in)) {
      return SimpleRetT(idx, true);
    }
    if (UNLIKELY(key == kEmptyKey_)) {
      // if we hit an empty element, this key does not exist
      return SimpleRetT(capacity_, false);
    }
    ++numProbes;
    if (UNLIKELY(numProbes >= capacity_)) {
      // probed every cell...fail
      return SimpleRetT(capacity_, false);
    }
  }
}

/*
 * insertInternal --
 *
 *   Returns false on failure due to key collision or full.
 *   Also sets ret.index to the index of the key.  If the map is full, sets
 *   ret.index = capacity_.  Also sets ret.second to cell value, thus if insert
 *   successful this will be what we just inserted, if there is a key collision
 *   this will be the previously inserted value, and if the map is full it is
 *   default.
 */
template <class KeyT, class ValueT, class HashFcn>
template <class T>
typename AtomicHashArray<KeyT, ValueT, HashFcn>::SimpleRetT
AtomicHashArray<KeyT, ValueT, HashFcn>::
insertInternal(KeyT key_in, T&& value) {
  const short NO_NEW_INSERTS = 1;
  const short NO_PENDING_INSERTS = 2;
  CHECK_NE(key_in, kEmptyKey_);
  CHECK_NE(key_in, kLockedKey_);
  CHECK_NE(key_in, kErasedKey_);

  size_t idx = keyToAnchorIdx(key_in);
  size_t numProbes = 0;
  for (;;) {
    DCHECK_LT(idx, capacity_);
    value_type* cell = &cells_[idx];
    if (relaxedLoadKey(*cell) == kEmptyKey_) {
      // NOTE: isFull_ is set based on numEntries_.readFast(), so it's
      // possible to insert more than maxEntries_ entries. However, it's not
      // possible to insert past capacity_.
      ++numPendingEntries_;
      if (isFull_.load(std::memory_order_acquire)) {
        --numPendingEntries_;

        // Before deciding whether this insert succeeded, this thread needs to
        // wait until no other thread can add a new entry.

        // Correctness assumes isFull_ is true at this point. If
        // another thread now does ++numPendingEntries_, we expect it
        // to pass the isFull_.load() test above. (It shouldn't insert
        // a new entry.)
        FOLLY_SPIN_WAIT(
          isFull_.load(std::memory_order_acquire) != NO_PENDING_INSERTS
            && numPendingEntries_.readFull() != 0
        );
        isFull_.store(NO_PENDING_INSERTS, std::memory_order_release);

        if (relaxedLoadKey(*cell) == kEmptyKey_) {
          // Don't insert past max load factor
          return SimpleRetT(capacity_, false);
        }
      } else {
        // An unallocated cell. Try once to lock it. If we succeed, insert here.
        // If we fail, fall through to comparison below; maybe the insert that
        // just beat us was for this very key....
        if (tryLockCell(cell)) {
          // Write the value - done before unlocking
          try {
            DCHECK(relaxedLoadKey(*cell) == kLockedKey_);
            /*
             * This happens using the copy constructor because we won't have
             * constructed a lhs to use an assignment operator on when
             * values are being set.
             */
            new (&cell->second) ValueT(std::forward<T>(value));
            unlockCell(cell, key_in); // Sets the new key
          } catch (...) {
            // Transition back to empty key---requires handling
            // locked->empty below.
            unlockCell(cell, kEmptyKey_);
            --numPendingEntries_;
            throw;
          }
          DCHECK(relaxedLoadKey(*cell) == key_in);
          --numPendingEntries_;
          ++numEntries_;  // This is a thread cached atomic increment :)
          if (numEntries_.readFast() >= maxEntries_) {
            isFull_.store(NO_NEW_INSERTS, std::memory_order_relaxed);
          }
          return SimpleRetT(idx, true);
        }
        --numPendingEntries_;
      }
    }
    DCHECK(relaxedLoadKey(*cell) != kEmptyKey_);
    if (kLockedKey_ == acquireLoadKey(*cell)) {
      FOLLY_SPIN_WAIT(
        kLockedKey_ == acquireLoadKey(*cell)
      );
    }

    const KeyT thisKey = acquireLoadKey(*cell);
    if (thisKey == key_in) {
      // Found an existing entry for our key, but we don't overwrite the
      // previous value.
      return SimpleRetT(idx, false);
    } else if (thisKey == kEmptyKey_ || thisKey == kLockedKey_) {
      // We need to try again (i.e., don't increment numProbes or
      // advance idx): this case can happen if the constructor for
      // ValueT threw for this very cell (the rethrow block above).
      continue;
    }

    ++numProbes;
    if (UNLIKELY(numProbes >= capacity_)) {
      // probed every cell...fail
      return SimpleRetT(capacity_, false);
    }

    idx = probeNext(idx, numProbes);
  }
}


/*
 * erase --
 *
 *   This will attempt to erase the given key key_in if the key is found. It
 *   returns 1 iff the key was located and marked as erased, and 0 otherwise.
 *
 *   Memory is not freed or reclaimed by erase, i.e. the cell containing the
 *   erased key will never be reused. If there's an associated value, we won't
 *   touch it either.
 */
template <class KeyT, class ValueT, class HashFcn>
size_t AtomicHashArray<KeyT, ValueT, HashFcn>::
erase(KeyT key_in) {
  CHECK_NE(key_in, kEmptyKey_);
  CHECK_NE(key_in, kLockedKey_);
  CHECK_NE(key_in, kErasedKey_);
  for (size_t idx = keyToAnchorIdx(key_in), numProbes = 0;
       ;
       idx = probeNext(idx, numProbes)) {
    DCHECK_LT(idx, capacity_);
    value_type* cell = &cells_[idx];
    KeyT currentKey = acquireLoadKey(*cell);
    if (currentKey == kEmptyKey_ || currentKey == kLockedKey_) {
      // If we hit an empty (or locked) element, this key does not exist. This
      // is similar to how it's handled in find().
      return 0;
    }
    if (key_in == currentKey) {
      // Found an existing entry for our key, attempt to mark it erased.
      // Some other thread may have erased our key, but this is ok.
      KeyT expect = key_in;
      if (cellKeyPtr(*cell)->compare_exchange_strong(expect, kErasedKey_)) {
        numErases_.fetch_add(1, std::memory_order_relaxed);

        // Even if there's a value in the cell, we won't delete (or even
        // default construct) it because some other thread may be accessing it.
        // Locking it meanwhile won't work either since another thread may be
        // holding a pointer to it.

        // We found the key and successfully erased it.
        return 1;
      }
      // If another thread succeeds in erasing our key, we'll stop our search.
      return 0;
    }
    ++numProbes;
    if (UNLIKELY(numProbes >= capacity_)) {
      // probed every cell...fail
      return 0;
    }
  }
}

template <class KeyT, class ValueT, class HashFcn>
const typename AtomicHashArray<KeyT, ValueT, HashFcn>::Config
AtomicHashArray<KeyT, ValueT, HashFcn>::defaultConfig;

template <class KeyT, class ValueT, class HashFcn>
typename AtomicHashArray<KeyT, ValueT, HashFcn>::SmartPtr
AtomicHashArray<KeyT, ValueT, HashFcn>::
create(size_t maxSize, const Config& c) {
  CHECK_LE(c.maxLoadFactor, 1.0);
  CHECK_GT(c.maxLoadFactor, 0.0);
  CHECK_NE(c.emptyKey, c.lockedKey);
  size_t capacity = size_t(maxSize / c.maxLoadFactor);
  size_t sz = sizeof(AtomicHashArray) + sizeof(value_type) * capacity;

  std::unique_ptr<void, void(*)(void*)> mem(malloc(sz), free);
  new(mem.get()) AtomicHashArray(capacity, c.emptyKey, c.lockedKey, c.erasedKey,
                                 c.maxLoadFactor, c.entryCountThreadCacheSize);
  SmartPtr map(static_cast<AtomicHashArray*>(mem.release()));

  /*
   * Mark all cells as empty.
   *
   * Note: we're bending the rules a little here accessing the key
   * element in our cells even though the cell object has not been
   * constructed, and casting them to atomic objects (see cellKeyPtr).
   * (Also, in fact we never actually invoke the value_type
   * constructor.)  This is in order to avoid needing to default
   * construct a bunch of value_type when we first start up: if you
   * have an expensive default constructor for the value type this can
   * noticeably speed construction time for an AHA.
   */
  FOR_EACH_RANGE(i, 0, map->capacity_) {
    cellKeyPtr(map->cells_[i])->store(map->kEmptyKey_,
      std::memory_order_relaxed);
  }
  return map;
}

template <class KeyT, class ValueT, class HashFcn>
void AtomicHashArray<KeyT, ValueT, HashFcn>::
destroy(AtomicHashArray* p) {
  assert(p);
  FOR_EACH_RANGE(i, 0, p->capacity_) {
    if (p->cells_[i].first != p->kEmptyKey_) {
      p->cells_[i].~value_type();
    }
  }
  p->~AtomicHashArray();
  free(p);
}

// clear -- clears all keys and values in the map and resets all counters
template <class KeyT, class ValueT, class HashFcn>
void AtomicHashArray<KeyT, ValueT, HashFcn>::
clear() {
  FOR_EACH_RANGE(i, 0, capacity_) {
    if (cells_[i].first != kEmptyKey_) {
      cells_[i].~value_type();
      *const_cast<KeyT*>(&cells_[i].first) = kEmptyKey_;
    }
    CHECK(cells_[i].first == kEmptyKey_);
  }
  numEntries_.set(0);
  numPendingEntries_.set(0);
  isFull_.store(0, std::memory_order_relaxed);
  numErases_.store(0, std::memory_order_relaxed);
}


// Iterator implementation

template <class KeyT, class ValueT, class HashFcn>
template <class ContT, class IterVal>
struct AtomicHashArray<KeyT, ValueT, HashFcn>::aha_iterator
    : boost::iterator_facade<aha_iterator<ContT,IterVal>,
                             IterVal,
                             boost::forward_traversal_tag>
{
  explicit aha_iterator() : aha_(0) {}

  // Conversion ctor for interoperability between const_iterator and
  // iterator.  The enable_if<> magic keeps us well-behaved for
  // is_convertible<> (v. the iterator_facade documentation).
  template<class OtherContT, class OtherVal>
  aha_iterator(const aha_iterator<OtherContT,OtherVal>& o,
               typename std::enable_if<
               std::is_convertible<OtherVal*,IterVal*>::value >::type* = 0)
      : aha_(o.aha_)
      , offset_(o.offset_)
  {}

  explicit aha_iterator(ContT* array, size_t offset)
      : aha_(array)
      , offset_(offset)
  {
    advancePastEmpty();
  }

  // Returns unique index that can be used with findAt().
  // WARNING: The following function will fail silently for hashtable
  // with capacity > 2^32
  uint32_t getIndex() const { return offset_; }

 private:
  friend class AtomicHashArray;
  friend class boost::iterator_core_access;

  void increment() {
    ++offset_;
    advancePastEmpty();
  }

  bool equal(const aha_iterator& o) const {
    return aha_ == o.aha_ && offset_ == o.offset_;
  }

  IterVal& dereference() const {
    return aha_->cells_[offset_];
  }

  void advancePastEmpty() {
    while (offset_ < aha_->capacity_ && !isValid()) {
      ++offset_;
    }
  }

  bool isValid() const {
    KeyT key = acquireLoadKey(aha_->cells_[offset_]);
    return key != aha_->kEmptyKey_  &&
      key != aha_->kLockedKey_ &&
      key != aha_->kErasedKey_;
  }

 private:
  ContT* aha_;
  size_t offset_;
}; // aha_iterator

} // namespace folly

#undef FOLLY_SPIN_WAIT
