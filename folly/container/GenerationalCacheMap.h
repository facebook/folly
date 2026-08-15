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

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

#include <folly/SharedMutex.h>
#include <folly/concurrency/ConcurrentHashMap.h>
#include <folly/synchronization/Hazptr.h>

namespace folly {

/**
 * A concurrent, bounded, approximately-LRU cache with lock-free lookups.
 *
 * Entries live in one of two generations. A lookup checks the recent generation
 * and then the older one; an insertion goes into the recent one. When the
 * recent generation fills, the generations rotate: the recent one becomes the
 * older one, and the previous older one is discarded. A hit in the older
 * generation is copied back into the recent one, so a key that stays in use
 * survives indefinitely, while one that falls out of use is dropped within two
 * rotations.
 *
 * Compared with Synchronized<EvictingCacheMap>:
 *  - A lookup takes no lock, and on a hit in the recent generation mutates no
 *    shared state. EvictingCacheMap must take a write lock on every hit purely
 *    to reorder its intrusive LRU list, which serializes readers against each
 *    other.
 *  - Eviction is coarse. Recency is tracked only at the granularity of "which
 *    generation", so an entry can be evicted while less recently used entries
 *    survive. That imprecision is what buys the point above.
 *
 * Compared with a fixed-capacity map that never evicts, which would also give
 * lock-free lookups, this tolerates drift: a key set that shifts over time, or
 * a burst of distinct keys early on, cannot permanently fill it and lock out
 * whatever is actually in use later.
 *
 * The operating point it is built for is a working set comfortably smaller than
 * `capacity` that drifts slowly, with keys gradually falling out of use as new
 * ones appear, rather than churning wholesale. There, almost every lookup hits
 * the recent generation and mutates nothing shared: no lock, no reference
 * count, no LRU bookkeeping. Insertions, promotions and rotations happen only
 * at the speed of the drift, so throughput scales close to linearly in the
 * number of threads.
 *
 * Away from that operating point both properties degrade together. A working
 * set larger than `capacity` means every rotation discards entries that are
 * still in use, so they are inserted again, and every cycle promotes everything
 * that survived: the hit rate falls, and exactly the write traffic that lookups
 * were meant to avoid comes back. Watch rotations() to tell the two regimes
 * apart; see below.
 *
 * It does not suit callers that need precise eviction, an exact size, or a hard
 * memory bound.
 *
 * `capacity` is the approximate number of entries retained. It is approximate
 * in both directions: inserters racing a rotation overshoot the threshold
 * slightly, and a discarded generation is freed asynchronously, so the resident
 * count lags. Overshoot is bounded, since an inserter that finds the map at
 * twice the rotation threshold waits for the rotation rather than adding to it,
 * but it is a bound, not a guarantee of staying near `capacity` under a heavy
 * enough insert rate.
 *
 * Value must be copy-constructible, since promotion copies it.
 */
template <
    typename Key,
    typename Value,
    typename HashFn = std::hash<Key>,
    typename KeyEqual = std::equal_to<Key>>
class GenerationalCacheMap {
 private:
  using Map = ConcurrentHashMap<Key, Value, HashFn, KeyEqual>;

  // The pair of live generations, published as a single immutable object so
  // that a reader gets a consistent pair from one protected load. Named for the
  // LSM-tree notion of a version: a snapshot of which tables are live.
  //
  // A rotation retires the outgoing Version rather than deleting it, so that
  // hazard pointer reclamation runs its destructor, which frees a whole
  // generation's worth of entries, on a background thread, instead of stalling
  // whichever caller happened to trigger the rotation.
  //
  // The maps are held by shared_ptr because a rotation aliases one of them: the
  // outgoing version's `recent` becomes the incoming version's `older`, and the
  // outgoing version stays alive for as long as any reader still protects it,
  // so neither version can be said to own it.
  struct Version : hazptr_obj_base<Version> {
    Version(std::shared_ptr<Map> r, std::shared_ptr<Map> o)
        : recent(std::move(r)), older(std::move(o)) {}

    const std::shared_ptr<Map> recent;
    const std::shared_ptr<Map> older;
  };

 public:
  explicit GenerationalCacheMap(size_t capacity)
      : rotateAt_(std::max<size_t>(capacity / 2, 1)),
        version_(
            new Version(std::make_shared<Map>(), std::make_shared<Map>())) {}

  GenerationalCacheMap(const GenerationalCacheMap&) = delete;
  GenerationalCacheMap(GenerationalCacheMap&&) = delete;
  GenerationalCacheMap& operator=(const GenerationalCacheMap&) = delete;
  GenerationalCacheMap& operator=(GenerationalCacheMap&&) = delete;

  ~GenerationalCacheMap() { delete version_.load(std::memory_order_acquire); }

  /**
   * A found value, kept alive for as long as the Ref is held. Converts to false
   * if the key was absent.
   *
   * Holding one pins the generation it came from, delaying reclamation, so keep
   * them short-lived.
   */
  class Ref {
   public:
    ~Ref() = default;
    // Movable so it can be returned or handed on, but not assignable: a Ref is
    // meant to be a short-lived scoped handle, not a variable to reuse.
    Ref(Ref&&) = default;
    Ref(const Ref&) = delete;
    Ref& operator=(Ref&&) = delete;
    Ref& operator=(const Ref&) = delete;

    explicit operator bool() const { return value_ != nullptr; }
    const Value& operator*() const { return *value_; }
    const Value* operator->() const { return value_; }
    const Value* get() const { return value_; }

   private:
    friend class GenerationalCacheMap;

    Ref(hazptr_holder<> holder,
        typename Map::ConstIterator found,
        const Value* value)
        : holder_(std::move(holder)), found_(std::move(found)), value_(value) {}

    // Both guards are necessary. found_'s hazard pointer keeps the node
    // behind `value_` from being reclaimed, and holder_ keeps the Version
    // alive, and with it the shared_ptr to the map that node lives in.
    // ~ConcurrentHashMap deletes its nodes rather than retiring them, on the
    // grounds that callers synchronize their own destruction, so the node guard
    // alone would not survive the map itself going away.
    hazptr_holder<> holder_;
    typename Map::ConstIterator found_;
    const Value* value_;
  };

  /**
   * The value for `key`, or an empty Ref.
   *
   * A hit in the older generation copies the entry into the recent one, so this
   * can write even though it reads.
   */
  Ref find(const Key& key) {
    auto holder = make_hazard_pointer();
    auto* version = holder.protect(version_);

    auto found = version->recent->find(key);
    if (found != version->recent->cend()) {
      const auto* value = &found->second;
      return Ref(std::move(holder), std::move(found), value);
    }

    found = version->older->find(key);
    if (found == version->older->cend()) {
      return Ref(std::move(holder), std::move(found), nullptr);
    }
    // Copy back, so the entry survives the coming rotation. Only entries that
    // already survived one rotation are ever promoted, which bounds promotion
    // traffic to one insertion per entry per rotation cycle. try_emplace rather
    // than assignment, so that a value a concurrent set() has just written is
    // not clobbered by the older generation's copy.
    const auto* value = &found->second;
    if (version->recent->try_emplace(key, *value).second) {
      countInsertion(version);
    }
    return Ref(std::move(holder), std::move(found), value);
  }

  /**
   * Associates `value` with `key`, replacing whatever was there.
   *
   * Best effort: the entry may be evicted at any later point.
   */
  void set(const Key& key, Value value) {
    auto holder = make_hazard_pointer();
    auto* version = holder.protect(version_);
    auto& recent = *version->recent;
    // Whether this creates an entry has to be settled before the write:
    // ConcurrentHashMap::insert_or_assign reports success for an assignment
    // too, and counting those would rotate a map whose keys are only being
    // updated. Two setters racing can both see the key absent and both count
    // it, rotating marginally early; the counter is approximate anyway.
    const bool created = recent.find(key) == recent.cend();
    recent.insert_or_assign(key, std::move(value));
    if (created) {
      countInsertion(version);
    }
  }

  /**
   * How many rotations have happened.
   *
   * At the intended operating point this advances at the rate the working set
   * drifts, which for a slowly drifting one is rare. Rotating much faster than
   * the key set is believed to be changing means it does not fit and `capacity`
   * should be raised. Measuring that is usually easier than predicting the
   * number of distinct keys up front.
   */
  uint64_t rotations() const {
    return rotations_.load(std::memory_order_relaxed);
  }

  /** Approximate, and racy under concurrent modification. */
  size_t size() const {
    auto holder = make_hazard_pointer();
    auto* version = holder.protect(version_);
    return version->recent->size() + version->older->size();
  }

 private:
  // Accounts for an entry just added to `version`'s recent generation, rotating
  // if that filled it. Writers read `version` a moment before calling, but a
  // rotation may have landed in between, in which case the entry went into a
  // generation on its way out and is dropped; nothing depends on it surviving.
  //
  // The caller's hazard pointer must protect `version` for the whole call: it
  // is handed on to rotate(), which dereferences it after possibly blocking.
  void countInsertion(Version* version) {
    const auto size = recentSize_.fetch_add(1, std::memory_order_relaxed) + 1;
    if (size >= rotateAt_) {
      // Normally a rotation already in flight is left to finish while this
      // thread carries on. Past twice the threshold it evidently is not keeping
      // up, since the rotating thread may have been descheduled, so wait for
      // it instead of letting the generation grow without bound.
      rotate(version, /* wait */ size >= 2 * rotateAt_);
    }
  }

  // `observed` must stay protected by the caller's hazard pointer for the whole
  // call, including across the blocking lock() below. `wait` blocks until
  // whoever is already rotating is done, rather than leaving; see the caller
  // for when that is worth it.
  void rotate(Version* observed, bool wait) {
    // Gating on a mutex rather than racing a CAS matters because the loser of
    // that race would have had to build a Version, and therefore a whole map,
    // only to destroy it inline. Here a thread that finds the mutex held
    // knows a rotation is in flight and does no work at all.
    std::unique_lock<SharedMutex> lock(rotateMutex_, std::defer_lock);
    if (wait) {
      lock.lock();
    } else if (!lock.try_lock()) {
      return;
    }
    // Only rotations publish a version and they all hold this mutex, so this
    // load is stable. A thread that crossed the threshold against a version
    // that has since been rotated away has nothing left to do.
    if (version_.load(std::memory_order_acquire) != observed) {
      return;
    }

    // Deliberately not pre-sized: the whole critical section has to stay short,
    // since anything spent here is time other threads spend overshooting the
    // threshold. The map grows per-segment as it fills instead.
    auto* fresh = new Version(std::make_shared<Map>(), observed->recent);
    // Reset before publishing. An insert racing the swap then counts against
    // the fresh generation despite landing in the outgoing one, which rotates
    // marginally early; resetting afterwards would instead lose counts from
    // inserts into the fresh generation, letting it overshoot.
    recentSize_.store(0, std::memory_order_relaxed);
    version_.store(fresh, std::memory_order_release);
    rotations_.fetch_add(1, std::memory_order_relaxed);
    observed->retire();
  }

  size_t const rotateAt_;
  std::atomic<Version*> version_;
  std::atomic<size_t> recentSize_{0};
  std::atomic<uint64_t> rotations_{0};
  SharedMutex rotateMutex_;
};

} // namespace folly
