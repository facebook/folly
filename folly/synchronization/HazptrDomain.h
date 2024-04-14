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

#include <atomic>

#include <folly/Executor.h>
#include <folly/Memory.h>
#include <folly/Portability.h>
#include <folly/container/F14Set.h>
#include <folly/synchronization/AsymmetricThreadFence.h>
#include <folly/synchronization/Hazptr-fwd.h>
#include <folly/synchronization/HazptrObj.h>
#include <folly/synchronization/HazptrRec.h>
#include <folly/synchronization/HazptrThrLocal.h>

///
/// Classes related to hazard pointer domains.
///

namespace folly {

class Executor;

namespace detail {

/** Threshold for the number of retired objects to trigger
    asynchronous reclamation. */
constexpr int hazptr_domain_rcount_threshold() {
  return 1000;
}

folly::Executor::KeepAlive<> hazptr_get_default_executor();

} // namespace detail

/**
 *  hazptr_domain
 *
 *  A domain manages a set of hazard pointers and a set of retired objects.
 *
 *  Most user code need not specify any domains.
 *
 *  Notes on destruction order and tagged objects:
 *  - Tagged objects support reclamation order guarantees (i.e.,
 *    synchronous reclamation). A call to cleanup_cohort_tag(tag)
 *    guarantees that all objects with the specified tag are reclaimed
 *    before the function returns.
 *  - There are two types of reclamation operations to consider:
 *   - Asynchronous reclamation: It is triggered by meeting some
 *     threshold for the number of retired objects or the time since
 *     the last asynchronous reclamation. Reclaimed objects may have
 *     different tags or no tags. Hazard pointers are checked and only
 *     unprotected objects are reclaimed. This type is expected to be
 *     expensive but infrequent and the cost is amortized over a large
 *     number of reclaimed objects. This type is needed to guarantee
 *     an upper bound on unreclaimed reclaimable objects.
 *   - Synchronous reclamation: It is invoked by calling
 *     cleanup_cohort_tag for a specific tag. All objects with the
 *     specified tag must be reclaimed unconditionally before
 *     returning from such a function call. Hazard pointers are not
 *     checked. This type of reclamation operation is expected to be
 *     inexpensive and may be invoked more frequently than
 *     asynchronous reclamation.
 *  - Tagged retired objects are kept in a sharded list in the domain
 *    structure.
 *  - Both asynchronous and synchronous reclamation pop all the
 *    objects in the tagged list(s) and sort them into two sets of
 *    reclaimable and unreclaimable objects. The objects in the
 *    reclaimable set are reclaimed and the objects in the
 *    unreclaimable set are pushed back in the tagged list(s).
 *  - The tagged list(s) are locked between popping all objects and
 *    pushing back unreclaimable objects, in order to guarantee that
 *    synchronous reclamation operations do not miss any objects.
 *  - Asynchronous reclamation can release the lock(s) on the tagged
 *    list(s) before reclaiming reclaimable objects, because it pushes
 *    reclaimable tagged objects in their respective cohorts, which
 *    would handle concurrent synchronous reclamation of such objects
 *    properly.
 *  - Synchronous reclamation operations can release the lock on the
 *    tagged list shard before reclaiming objects because the sets of
 *    reclaimable objects by different synchronous reclamation
 *    operations are disjoint.
 */
template <template <typename> class Atom>
class hazptr_domain {
  using Obj = hazptr_obj<Atom>;
  using Rec = hazptr_rec<Atom>;
  using List = hazptr_detail::linked_list<Obj>;
  using ObjList = hazptr_obj_list<Atom>;
  using RetiredList = hazptr_detail::shared_head_only_list<Obj, Atom>;
  using Set = folly::F14FastSet<const void*>;
  using ExecFn = folly::Executor::KeepAlive<> (*)();

  static constexpr int kThreshold = detail::hazptr_domain_rcount_threshold();
  static constexpr int kMultiplier = 2;
  static constexpr int kListTooLarge = 100000;
  static constexpr uint64_t kSyncTimePeriod{2000000000}; // nanoseconds
  static constexpr uintptr_t kTagBit = hazptr_obj<Atom>::kTagBit;
  static constexpr uintptr_t kLockBit = 1;
  static constexpr int kIgnoredLowBits = 8;

  static constexpr int kNumShards = 8;
  static constexpr int kShardMask = kNumShards - 1;
  static_assert(
      (kNumShards & kShardMask) == 0, "kNumShards must be a power of 2");

  Atom<Rec*> hazptrs_{nullptr};
  Atom<uintptr_t> avail_{reinterpret_cast<uintptr_t>(nullptr)};
  Atom<uint64_t> sync_time_{0};
  /* Using signed int for rcount_ because it may transiently be negative.
     Using signed int for all integer variables that may be involved in
     calculations related to the value of rcount_. */
  Atom<int> hcount_{0};
  Atom<uint16_t> num_bulk_reclaims_{0};
  bool shutdown_{false};
  RetiredList untagged_[kNumShards];
  RetiredList tagged_[kNumShards];
  Atom<int> count_{0};
  Atom<uint64_t> due_time_{0};
  Atom<ExecFn> exec_fn_{nullptr};
  Atom<int> exec_backlog_{0};

 public:
  /** Constructor */
  hazptr_domain() = default;

  /** Destructor */
  ~hazptr_domain() {
    shutdown_ = true;
    reclaim_all_objects();
    free_hazptr_recs();
    if (kIsDebug && !tagged_empty()) {
      LOG(WARNING)
          << "Tagged objects remain. This may indicate a higher-level leak "
          << "of object(s) that use hazptr_obj_cohort.";
    }
  }

  hazptr_domain(const hazptr_domain&) = delete;
  hazptr_domain(hazptr_domain&&) = delete;
  hazptr_domain& operator=(const hazptr_domain&) = delete;
  hazptr_domain& operator=(hazptr_domain&&) = delete;

  void set_executor(ExecFn exfn) {
    exec_fn_.store(exfn, std::memory_order_release);
  }

  void clear_executor() { exec_fn_.store(nullptr, std::memory_order_release); }

  /** retire - nonintrusive - allocates memory */
  template <typename T, typename D = std::default_delete<T>>
  void retire(T* obj, D reclaim = {}) {
    struct hazptr_retire_node : hazptr_obj<Atom> {
      std::unique_ptr<T, D> obj_;
      hazptr_retire_node(T* retireObj, D toReclaim)
          : obj_{retireObj, std::move(toReclaim)} {}
    };

    auto node = new hazptr_retire_node(obj, std::move(reclaim));
    node->reclaim_ = [](hazptr_obj<Atom>* p, hazptr_obj_list<Atom>&) {
      delete static_cast<hazptr_retire_node*>(p);
    };
    hazptr_obj_list<Atom> l(node);
    push_list(l);
  }

  /** cleanup */
  void cleanup() noexcept {
    inc_num_bulk_reclaims();
    do_reclamation(0);
    wait_for_zero_bulk_reclaims(); // wait for concurrent bulk_reclaim-s
  }

  /** delete_hazard_pointers -- Used only for benchmarking */
  void delete_hazard_pointers() {
    // Call cleanup() to ensure that there is no lagging concurrent
    // asynchronous reclamation in progress.
    cleanup();
    Rec* rec = head();
    while (rec) {
      auto next = rec->next();
      rec->~Rec();
      hazptr_rec_alloc{}.deallocate(rec, 1);
      rec = next;
    }
    hazptrs_.store(nullptr);
    hcount_.store(0);
    avail_.store(reinterpret_cast<uintptr_t>(nullptr));
  }

  /** cleanup_cohort_tag */
  void cleanup_cohort_tag(const hazptr_obj_cohort<Atom>* cohort) noexcept {
    auto ftag = reinterpret_cast<uintptr_t>(cohort) + kTagBit;
    auto shard = calc_shard(ftag);
    auto obj = tagged_[shard].pop_all(RetiredList::kAlsoLock);
    ObjList match, nomatch;
    list_match_tag(ftag, obj, match, nomatch);
    List l(nomatch.head(), nomatch.tail());
    tagged_[shard].push_unlock(l);
    add_count(-match.count());
    obj = match.head();
    reclaim_list_transitive(obj);
    int count = match.count() + nomatch.count();
    if (count > kListTooLarge) {
      hazptr_warning_list_too_large(ftag, shard, count);
    }
  }

  void list_match_tag(
      uintptr_t ftag, Obj* obj, ObjList& match, ObjList& nomatch) {
    list_match_condition(obj, match, nomatch, [ftag](Obj* o) {
      return o->cohort_tag() == ftag;
    });
  }

 private:
  using hazptr_rec_alloc = AlignedSysAllocator<Rec, FixedAlign<alignof(Rec)>>;

  friend void hazptr_domain_push_retired<Atom>(
      hazptr_obj_list<Atom>&, hazptr_domain<Atom>&) noexcept;
  friend hazptr_holder<Atom> make_hazard_pointer<Atom>(hazptr_domain<Atom>&);
  template <uint8_t M, template <typename> class A>
  friend hazptr_array<M, A> make_hazard_pointer_array();
  friend class hazptr_holder<Atom>;
  friend class hazptr_obj<Atom>;
  friend class hazptr_obj_cohort<Atom>;
#if FOLLY_HAZPTR_THR_LOCAL
  friend class hazptr_tc<Atom>;
#endif

  int load_count() { return count_.load(std::memory_order_acquire); }

  void add_count(int val) { count_.fetch_add(val, std::memory_order_release); }

  int exchange_count(int val) {
    return count_.exchange(val, std::memory_order_acq_rel);
  }

  bool cas_count(int& expected, int newval) {
    return count_.compare_exchange_weak(
        expected, newval, std::memory_order_acq_rel, std::memory_order_relaxed);
  }

  uint64_t load_due_time() { return due_time_.load(std::memory_order_acquire); }

  void set_due_time() {
    uint64_t time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count();
    due_time_.store(time + kSyncTimePeriod, std::memory_order_release);
  }

  bool cas_due_time(uint64_t& expected, uint64_t newval) {
    return due_time_.compare_exchange_strong(
        expected, newval, std::memory_order_acq_rel, std::memory_order_relaxed);
  }

  uint16_t load_num_bulk_reclaims() {
    return num_bulk_reclaims_.load(std::memory_order_acquire);
  }

  void inc_num_bulk_reclaims() {
    num_bulk_reclaims_.fetch_add(1, std::memory_order_release);
  }

  void dec_num_bulk_reclaims() {
    DCHECK_GT(load_num_bulk_reclaims(), 0);
    num_bulk_reclaims_.fetch_sub(1, std::memory_order_release);
  }

  uintptr_t load_avail() { return avail_.load(std::memory_order_acquire); }

  void store_avail(uintptr_t val) {
    avail_.store(val, std::memory_order_release);
  }

  bool cas_avail(uintptr_t& expval, uintptr_t newval) {
    return avail_.compare_exchange_weak(
        expval, newval, std::memory_order_acq_rel, std::memory_order_acquire);
  }

  /** acquire_hprecs */
  Rec* acquire_hprecs(uint8_t num) {
    DCHECK_GE(num, 1);
    // C++17: auto [n, head] = try_pop_available_hprecs(num);
    uint8_t n;
    Rec* head;
    std::tie(n, head) = try_pop_available_hprecs(num);
    for (; n < num; ++n) {
      Rec* rec = create_new_hprec();
      DCHECK(rec->next_avail() == nullptr);
      rec->set_next_avail(head);
      head = rec;
    }
    DCHECK(head);
    return head;
  }

  /** release_hprec */
  void release_hprec(Rec* hprec) noexcept {
    DCHECK(hprec);
    DCHECK(hprec->next_avail() == nullptr);
    push_available_hprecs(hprec, hprec);
  }

  /** release_hprecs */
  void release_hprecs(Rec* head, Rec* tail) noexcept {
    DCHECK(head);
    DCHECK(tail);
    DCHECK(tail->next_avail() == nullptr);
    push_available_hprecs(head, tail);
  }

  /** push_list */
  void push_list(ObjList& l) {
    if (l.empty()) {
      return;
    }
    uintptr_t btag = l.head()->cohort_tag();
    bool tagged = ((btag & kTagBit) == kTagBit);
    /*** Full fence ***/ asymmetric_thread_fence_light(
        std::memory_order_seq_cst);
    List ll(l.head(), l.tail());
    if (!tagged) {
      untagged_[calc_shard(l.head())].push(ll, RetiredList::kMayNotBeLocked);
    } else {
      tagged_[calc_shard(btag)].push(ll, RetiredList::kMayBeLocked);
    }
    add_count(l.count());
    check_threshold_and_reclaim();
  }

  /** threshold */
  int threshold() {
    auto thresh = kThreshold;
    return std::max(thresh, kMultiplier * hcount());
  }

  /** check_threshold_and_reclaim */
  void check_threshold_and_reclaim() {
    int rcount = check_count_threshold();
    if (rcount == 0) {
      rcount = check_due_time();
      if (rcount == 0)
        return;
    }
    inc_num_bulk_reclaims();
    if (!invoke_reclamation_in_executor(rcount)) {
      do_reclamation(rcount);
    }
  }

  /** calc_shard */
  size_t calc_shard(uintptr_t ftag) {
    size_t shard =
        (std::hash<uintptr_t>{}(ftag) >> kIgnoredLowBits) & kShardMask;
    DCHECK(shard < kNumShards);
    return shard;
  }

  size_t calc_shard(Obj* obj) {
    return calc_shard(reinterpret_cast<uintptr_t>(obj));
  }

  /** check_due_time */
  int check_due_time() {
    uint64_t time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count();
    auto due = load_due_time();
    if (time < due || !cas_due_time(due, time + kSyncTimePeriod))
      return 0;
    int rcount = exchange_count(0);
    if (rcount < 0) {
      add_count(rcount);
      return 0;
    }
    return rcount;
  }

  /** check_count_threshold */
  int check_count_threshold() {
    int rcount = load_count();
    while (rcount >= threshold()) {
      if (cas_count(rcount, 0)) {
        set_due_time();
        return rcount;
      }
    }
    return 0;
  }

  /** tagged_empty */
  bool tagged_empty() {
    for (int s = 0; s < kNumShards; ++s) {
      if (!tagged_[s].empty())
        return false;
    }
    return true;
  }

  /** untagged_empty */
  bool untagged_empty() {
    for (int s = 0; s < kNumShards; ++s) {
      if (!untagged_[s].empty())
        return false;
    }
    return true;
  }

  /** extract_retired_objects */
  bool extract_retired_objects(Obj* untagged[], Obj* tagged[]) {
    bool empty = true;
    for (int s = 0; s < kNumShards; ++s) {
      untagged[s] = untagged_[s].pop_all(RetiredList::kDontLock);
      if (untagged[s]) {
        empty = false;
      }
    }
    for (int s = 0; s < kNumShards; ++s) {
      /* Tagged lists need to be locked because tagging is used to
       * guarantee the identification of all objects with a specific
       * tag. Locking protects against concurrent hazptr_cleanup_tag()
       * calls missing tagged objects. */
      if (tagged_[s].check_lock()) {
        tagged[s] = nullptr;
      } else {
        tagged[s] = tagged_[s].pop_all(RetiredList::kAlsoLock);
        if (tagged[s]) {
          empty = false;
        } else {
          List l;
          tagged_[s].push_unlock(l);
        }
      }
    }
    return !empty;
  }

  /** load_hazptr_vals */
  Set load_hazptr_vals() {
    Set hs;
    auto hprec = hazptrs_.load(std::memory_order_acquire);
    for (; hprec; hprec = hprec->next()) {
      hs.insert(hprec->hazptr());
    }
    return hs;
  }

  /** match_tagged */
  int match_tagged(Obj* tagged[], Set& hs) {
    int count = 0;
    for (int s = 0; s < kNumShards; ++s) {
      if (tagged[s]) {
        ObjList match, nomatch;
        list_match_condition(tagged[s], match, nomatch, [&](Obj* o) {
          return hs.count(o->raw_ptr()) > 0;
        });
        count += nomatch.count();
        auto obj = nomatch.head();
        while (obj) {
          auto next = obj->next();
          auto cohort = obj->cohort();
          DCHECK(cohort);
          cohort->push_safe_obj(obj);
          obj = next;
        }
        List l(match.head(), match.tail());
        tagged_[s].push_unlock(l);
      }
    }
    return count;
  }

  /** match_reclaim_untagged */
  int match_reclaim_untagged(Obj* untagged[], Set& hs, bool& done) {
    done = true;
    ObjList not_reclaimed;
    int count = 0;
    for (int s = 0; s < kNumShards; ++s) {
      ObjList match, nomatch;
      list_match_condition(untagged[s], match, nomatch, [&](Obj* o) {
        return hs.count(o->raw_ptr()) > 0;
      });
      ObjList children;
      count += nomatch.count();
      reclaim_unprotected(nomatch.head(), children);
      if (!untagged_empty() || !children.empty()) {
        done = false;
      }
      count -= children.count();
      not_reclaimed.splice(match);
      not_reclaimed.splice(children);
    }
    List l(not_reclaimed.head(), not_reclaimed.tail());
    untagged_[0].push(l, RetiredList::kMayNotBeLocked);
    return count;
  }

  /** do_reclamation */
  void do_reclamation(int rcount) {
    DCHECK_GE(rcount, 0);
    while (true) {
      Obj* untagged[kNumShards];
      Obj* tagged[kNumShards];
      bool done = true;
      if (extract_retired_objects(untagged, tagged)) {
        /*** Full fence ***/ asymmetric_thread_fence_heavy(
            std::memory_order_seq_cst);
        Set hs = load_hazptr_vals();
        rcount -= match_tagged(tagged, hs);
        rcount -= match_reclaim_untagged(untagged, hs, done);
      }
      if (rcount) {
        add_count(rcount);
      }
      rcount = check_count_threshold();
      if (rcount == 0 && done)
        break;
    }
    dec_num_bulk_reclaims();
  }

  /** list_match_condition */
  template <typename Cond>
  void list_match_condition(
      Obj* obj, ObjList& match, ObjList& nomatch, const Cond& cond) {
    while (obj) {
      auto next = obj->next();
      DCHECK_NE(obj, next);
      if (cond(obj)) {
        match.push(obj);
      } else {
        nomatch.push(obj);
      }
      obj = next;
    }
  }

  /** reclaim_unprotected */
  void reclaim_unprotected(Obj* obj, ObjList& children) {
    while (obj) {
      auto next = obj->next();
      (*(obj->reclaim()))(obj, children);
      obj = next;
    }
  }

  /** reclaim_unconditional */
  void reclaim_unconditional(Obj* head, ObjList& children) {
    while (head) {
      auto next = head->next();
      (*(head->reclaim()))(head, children);
      head = next;
    }
  }

  Rec* head() const noexcept {
    return hazptrs_.load(std::memory_order_acquire);
  }

  int hcount() const noexcept {
    return hcount_.load(std::memory_order_acquire);
  }

  void reclaim_all_objects() {
    for (int s = 0; s < kNumShards; ++s) {
      Obj* head = untagged_[s].pop_all(RetiredList::kDontLock);
      reclaim_list_transitive(head);
    }
  }

  void reclaim_list_transitive(Obj* head) {
    while (head) {
      ObjList children;
      reclaim_unconditional(head, children);
      head = children.head();
    }
  }

  void free_hazptr_recs() {
    /* Leak the hazard pointers for the default domain to avoid
       destruction order issues with thread caches. */
    if (this == &default_hazptr_domain<Atom>()) {
      return;
    }
    auto rec = head();
    while (rec) {
      auto next = rec->next();
      rec->~Rec();
      hazptr_rec_alloc{}.deallocate(rec, 1);
      rec = next;
    }
  }

  void wait_for_zero_bulk_reclaims() {
    while (load_num_bulk_reclaims() > 0) {
      std::this_thread::yield();
    }
  }

  std::pair<uint8_t, Rec*> try_pop_available_hprecs(uint8_t num) {
    DCHECK_GE(num, 1);
    while (true) {
      uintptr_t avail = load_avail();
      if (avail == reinterpret_cast<uintptr_t>(nullptr)) {
        return {0, nullptr};
      }
      if ((avail & kLockBit) == 0) {
        // Try to lock avail list
        if (cas_avail(avail, avail | kLockBit)) {
          // Lock acquired
          Rec* head = reinterpret_cast<Rec*>(avail);
          uint8_t nn = pop_available_hprecs_release_lock(num, head);
          // Lock released
          DCHECK_GE(nn, 1);
          DCHECK_LE(nn, num);
          return {nn, head};
        }
      } else {
        std::this_thread::yield();
      }
    }
  }

  uint8_t pop_available_hprecs_release_lock(uint8_t num, Rec* head) {
    // Lock already acquired
    DCHECK_GE(num, 1);
    DCHECK(head);
    Rec* tail = head;
    uint8_t nn = 1;
    Rec* next = tail->next_avail();
    while ((next != nullptr) && (nn < num)) {
      DCHECK_EQ(reinterpret_cast<uintptr_t>(next) & kLockBit, 0);
      tail = next;
      next = tail->next_avail();
      ++nn;
    }
    uintptr_t newval = reinterpret_cast<uintptr_t>(next);
    DCHECK_EQ(newval & kLockBit, 0);
    // Release lock
    store_avail(newval);
    tail->set_next_avail(nullptr);
    return nn;
  }

  void push_available_hprecs(Rec* head, Rec* tail) {
    DCHECK(head);
    DCHECK(tail);
    DCHECK(tail->next_avail() == nullptr);
    if (kIsDebug) {
      dcheck_connected(head, tail);
    }
    uintptr_t newval = reinterpret_cast<uintptr_t>(head);
    DCHECK_EQ(newval & kLockBit, 0);
    while (true) {
      uintptr_t avail = load_avail();
      if ((avail & kLockBit) == 0) {
        // Try to push if unlocked
        auto next = reinterpret_cast<Rec*>(avail);
        tail->set_next_avail(next);
        if (cas_avail(avail, newval)) {
          break;
        }
      } else {
        std::this_thread::yield();
      }
    }
  }

  void dcheck_connected(Rec* head, Rec* tail) {
    Rec* rec = head;
    bool connected = false;
    while (rec) {
      Rec* next = rec->next_avail();
      if (rec == tail) {
        connected = true;
        DCHECK(next == nullptr);
      }
      rec = next;
    }
    DCHECK(connected);
  }

  Rec* create_new_hprec() {
    auto rec = hazptr_rec_alloc{}.allocate(1);
    new (rec) Rec();
    rec->set_domain(this);
    while (true) {
      auto h = head();
      rec->set_next(h);
      if (hazptrs_.compare_exchange_weak(
              h, rec, std::memory_order_release, std::memory_order_acquire)) {
        break;
      }
    }
    hcount_.fetch_add(1);
    return rec;
  }

  bool invoke_reclamation_in_executor(int rcount) {
    if (!std::is_same<Atom<int>, std::atomic<int>>{} ||
        this != &default_hazptr_domain<Atom>() || !hazptr_use_executor()) {
      return false;
    }
    auto fn = exec_fn_.load(std::memory_order_acquire);
    folly::Executor::KeepAlive<> ex =
        fn ? fn() : detail::hazptr_get_default_executor();
    if (!ex) {
      return false;
    }
    auto backlog = exec_backlog_.fetch_add(1, std::memory_order_relaxed);
    auto recl_fn = [this, rcount, ka = ex] {
      exec_backlog_.store(0, std::memory_order_relaxed);
      do_reclamation(rcount);
    };
    if (ex.get() == detail::hazptr_get_default_executor().get()) {
      invoke_reclamation_may_deadlock(ex, recl_fn);
    } else {
      ex->add(recl_fn);
    }
    if (backlog >= 10) {
      hazptr_warning_executor_backlog(backlog);
    }
    return true;
  }

  template <typename Func>
  void invoke_reclamation_may_deadlock(
      folly::Executor::KeepAlive<> ex, Func recl_fn) {
    ex->add(recl_fn);
    // This program is using the default executor, which is an
    // inline executor. This is not necessarily a problem. But if this
    // program encounters deadlock, then this may be the cause. Most
    // likely this program did not call
    // folly::enable_hazptr_thread_pool_executor (which is normally
    // called by folly::init). If this is a problem check if your
    // program is missing a call to folly::init or an alternative.
  }

  FOLLY_EXPORT FOLLY_NOINLINE void hazptr_warning_list_too_large(
      uintptr_t ftag, size_t shard, int count) {
    static std::atomic<uint64_t> warning_count{0};
    if ((warning_count++ % 10000) == 0) {
      LOG(WARNING) << "Hazptr retired list too large:" << " ftag=" << ftag
                   << " shard=" << shard << " count=" << count;
    }
  }

  FOLLY_EXPORT FOLLY_NOINLINE void hazptr_warning_executor_backlog(
      int backlog) {
    static std::atomic<uint64_t> warning_count{0};
    if ((warning_count++ % 10000) == 0) {
      LOG(WARNING) << backlog
                   << " request backlog for hazptr asynchronous "
                      "reclamation executor";
    }
  }
}; // hazptr_domain

/**
 *  Free functions related to hazptr domains
 */

/** default_hazptr_domain: Returns reference to the default domain */

template <template <typename> class Atom>
struct hazptr_default_domain_helper {
  static FOLLY_ALWAYS_INLINE hazptr_domain<Atom>& get() {
    static hazptr_domain<Atom> domain;
    return domain;
  }
};

template <>
struct hazptr_default_domain_helper<std::atomic> {
  static FOLLY_ALWAYS_INLINE hazptr_domain<std::atomic>& get() {
    return default_domain;
  }
};

template <template <typename> class Atom>
FOLLY_ALWAYS_INLINE hazptr_domain<Atom>& default_hazptr_domain() {
  return hazptr_default_domain_helper<Atom>::get();
}

template <template <typename> class Atom>
FOLLY_ALWAYS_INLINE hazard_pointer_domain<Atom>&
hazard_pointer_default_domain() {
  return default_hazptr_domain<Atom>();
}

/** hazptr_domain_push_retired: push a list of retired objects into a domain */
template <template <typename> class Atom>
void hazptr_domain_push_retired(
    hazptr_obj_list<Atom>& l, hazptr_domain<Atom>& domain) noexcept {
  domain.push_list(l);
}

/** hazptr_retire */
template <template <typename> class Atom, typename T, typename D>
FOLLY_ALWAYS_INLINE void hazptr_retire(T* obj, D reclaim) {
  default_hazptr_domain<Atom>().retire(obj, std::move(reclaim));
}

/** hazptr_cleanup: Reclaims all reclaimable objects retired to the domain */
template <template <typename> class Atom>
void hazptr_cleanup(hazptr_domain<Atom>& domain) noexcept {
  domain.cleanup();
}

template <template <typename> class Atom>
void hazard_pointer_clean_up(hazard_pointer_domain<Atom>& domain) noexcept {
  hazptr_cleanup(domain);
}

} // namespace folly
