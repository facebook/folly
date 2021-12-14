/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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
#include <mutex>

#include <folly/container/HeterogeneousAccess.h>
#include <folly/container/detail/F14Mask.h>
#include <folly/lang/Exception.h>
#include <folly/lang/Launder.h>
#include <folly/synchronization/Hazptr.h>

#if FOLLY_SSE_PREREQ(4, 2) && !FOLLY_MOBILE
#include <nmmintrin.h>
#endif

namespace folly {

namespace detail {

namespace concurrenthashmap {

enum class InsertType {
  DOES_NOT_EXIST, // insert/emplace operations.  If key exists, return false.
  MUST_EXIST, // assign operations.  If key does not exist, return false.
  ANY, // insert_or_assign.
  MATCH, // assign_if_equal (not in std).  For concurrent maps, a
         // way to atomically change a value if equal to some other
         // value.
};

template <
    typename KeyType,
    typename ValueType,
    typename Allocator,
    template <typename>
    class Atom,
    typename Enabled = void>
class ValueHolder {
 public:
  typedef std::pair<const KeyType, ValueType> value_type;

  explicit ValueHolder(const ValueHolder& other) : item_(other.item_) {}

  template <typename Arg, typename... Args>
  ValueHolder(std::piecewise_construct_t, Arg&& k, Args&&... args)
      : item_(
            std::piecewise_construct,
            std::forward_as_tuple(std::forward<Arg>(k)),
            std::forward_as_tuple(std::forward<Args>(args)...)) {}
  value_type& getItem() { return item_; }

 private:
  value_type item_;
};

// If the ValueType is not copy constructible, we can instead add
// an extra indirection.  Adds more allocations / deallocations and
// pulls in an extra cacheline.
template <
    typename KeyType,
    typename ValueType,
    typename Allocator,
    template <typename>
    class Atom>
class ValueHolder<
    KeyType,
    ValueType,
    Allocator,
    Atom,
    std::enable_if_t<
        !std::is_nothrow_copy_constructible<ValueType>::value ||
        !std::is_nothrow_copy_constructible<KeyType>::value>> {
  typedef std::pair<const KeyType, ValueType> value_type;

  struct CountedItem {
    value_type kv_;
    Atom<uint32_t> numlinks_{1}; // Number of incoming links

    template <typename Arg, typename... Args>
    CountedItem(std::piecewise_construct_t, Arg&& k, Args&&... args)
        : kv_(std::piecewise_construct,
              std::forward_as_tuple(std::forward<Arg>(k)),
              std::forward_as_tuple(std::forward<Args>(args)...)) {}

    value_type& getItem() { return kv_; }

    void acquireLink() {
      uint32_t count = numlinks_.fetch_add(1, std::memory_order_release);
      DCHECK_GE(count, 1);
    }

    bool releaseLink() {
      uint32_t count = numlinks_.load(std::memory_order_acquire);
      DCHECK_GE(count, 1);
      if (count > 1) {
        count = numlinks_.fetch_sub(1, std::memory_order_acq_rel);
      }
      return count == 1;
    }
  }; // CountedItem
  // Back to ValueHolder specialization

  CountedItem* item_; // Link to unique key-value item.

 public:
  explicit ValueHolder(const ValueHolder& other) {
    DCHECK(other.item_);
    item_ = other.item_;
    item_->acquireLink();
  }

  ValueHolder& operator=(const ValueHolder&) = delete;

  template <typename Arg, typename... Args>
  ValueHolder(std::piecewise_construct_t, Arg&& k, Args&&... args) {
    item_ = (CountedItem*)Allocator().allocate(sizeof(CountedItem));
    new (item_) CountedItem(
        std::piecewise_construct,
        std::forward<Arg>(k),
        std::forward<Args>(args)...);
  }

  ~ValueHolder() {
    DCHECK(item_);
    if (item_->releaseLink()) {
      item_->~CountedItem();
      Allocator().deallocate((uint8_t*)item_, sizeof(CountedItem));
    }
  }

  value_type& getItem() {
    DCHECK(item_);
    return item_->getItem();
  }
}; // ValueHolder specialization

// hazptr deleter that can use an allocator.
template <typename Allocator>
class HazptrDeleter {
 public:
  template <typename Node>
  void operator()(Node* node) {
    node->~Node();
    Allocator().deallocate((uint8_t*)node, sizeof(Node));
  }
};

class HazptrTableDeleter {
  size_t count_;

 public:
  HazptrTableDeleter(size_t count) : count_(count) {}
  HazptrTableDeleter() = default;
  template <typename Table>
  void operator()(Table* table) {
    table->destroy(count_);
  }
};

namespace bucket {

template <
    typename KeyType,
    typename ValueType,
    typename Allocator,
    template <typename> class Atom = std::atomic>
class NodeT : public hazptr_obj_base_linked<
                  NodeT<KeyType, ValueType, Allocator, Atom>,
                  Atom,
                  concurrenthashmap::HazptrDeleter<Allocator>> {
 public:
  typedef std::pair<const KeyType, ValueType> value_type;

  explicit NodeT(hazptr_obj_cohort<Atom>* cohort, NodeT* other)
      : item_(other->item_) {
    init(cohort);
  }

  template <typename Arg, typename... Args>
  NodeT(hazptr_obj_cohort<Atom>* cohort, Arg&& k, Args&&... args)
      : item_(
            std::piecewise_construct,
            std::forward<Arg>(k),
            std::forward<Args>(args)...) {
    init(cohort);
  }

  void release() { this->unlink(); }

  value_type& getItem() { return item_.getItem(); }

  template <typename S>
  void push_links(bool m, S& s) {
    if (m) {
      auto p = next_.load(std::memory_order_acquire);
      if (p) {
        s.push(p);
      }
    }
  }

  Atom<NodeT*> next_{nullptr};

 private:
  void init(hazptr_obj_cohort<Atom>* cohort) {
    DCHECK(cohort);
    this->set_deleter( // defined in hazptr_obj
        concurrenthashmap::HazptrDeleter<Allocator>());
    this->set_cohort_tag(cohort); // defined in hazptr_obj
    this->acquire_link_safe(); // defined in hazptr_obj_base_linked
  }

  ValueHolder<KeyType, ValueType, Allocator, Atom> item_;
};

template <
    typename KeyType,
    typename ValueType,
    uint8_t ShardBits = 0,
    typename HashFn = std::hash<KeyType>,
    typename KeyEqual = std::equal_to<KeyType>,
    typename Allocator = std::allocator<uint8_t>,
    template <typename> class Atom = std::atomic,
    class Mutex = std::mutex>
class alignas(64) BucketTable {
 public:
  // Slightly higher than 1.0, in case hashing to shards isn't
  // perfectly balanced, reserve(size) will still work without
  // rehashing.
  static constexpr float kDefaultLoadFactor = 1.05;
  typedef std::pair<const KeyType, ValueType> value_type;

  using Node =
      concurrenthashmap::bucket::NodeT<KeyType, ValueType, Allocator, Atom>;
  using InsertType = concurrenthashmap::InsertType;
  class Iterator;

  BucketTable(
      size_t initial_buckets,
      float load_factor,
      size_t max_size,
      hazptr_obj_cohort<Atom>* cohort)
      : load_factor_(load_factor), max_size_(max_size) {
    DCHECK(cohort);
    initial_buckets = folly::nextPowTwo(initial_buckets);
    DCHECK(
        max_size_ == 0 ||
        (isPowTwo(max_size_) &&
         (folly::popcount(max_size_ - 1) + ShardBits <= 32)));
    auto buckets = Buckets::create(initial_buckets, cohort);
    buckets_.store(buckets, std::memory_order_release);
    load_factor_nodes_ = initial_buckets * load_factor_;
    bucket_count_.store(initial_buckets, std::memory_order_relaxed);
  }

  ~BucketTable() {
    auto buckets = buckets_.load(std::memory_order_relaxed);
    // To catch use-after-destruction bugs in user code.
    buckets_.store(nullptr, std::memory_order_release);
    // We can delete and not retire() here, since users must have
    // their own synchronization around destruction.
    auto count = bucket_count_.load(std::memory_order_relaxed);
    buckets->unlink_and_reclaim_nodes(count);
    buckets->destroy(count);
  }

  size_t size() { return size_.load(std::memory_order_acquire); }

  void clearSize() { size_.store(0, std::memory_order_release); }

  void incSize() {
    auto sz = size_.load(std::memory_order_relaxed);
    size_.store(sz + 1, std::memory_order_release);
  }

  void decSize() {
    auto sz = size_.load(std::memory_order_relaxed);
    DCHECK_GT(sz, 0);
    size_.store(sz - 1, std::memory_order_release);
  }

  bool empty() { return size() == 0; }

  template <typename MatchFunc, typename K, typename... Args>
  bool insert(
      Iterator& it,
      const K& k,
      InsertType type,
      MatchFunc match,
      hazptr_obj_cohort<Atom>* cohort,
      Args&&... args) {
    return doInsert(
        it, k, type, match, nullptr, cohort, std::forward<Args>(args)...);
  }

  template <typename MatchFunc, typename K, typename... Args>
  bool insert(
      Iterator& it,
      const K& k,
      InsertType type,
      MatchFunc match,
      Node* cur,
      hazptr_obj_cohort<Atom>* cohort) {
    return doInsert(it, k, type, match, cur, cohort, cur);
  }

  // Must hold lock.
  void rehash(size_t bucket_count, hazptr_obj_cohort<Atom>* cohort) {
    auto oldcount = bucket_count_.load(std::memory_order_relaxed);
    // bucket_count must be a power of 2
    DCHECK_EQ(bucket_count & (bucket_count - 1), 0);
    if (bucket_count <= oldcount) {
      return; // Rehash only if expanding.
    }
    auto buckets = buckets_.load(std::memory_order_relaxed);
    DCHECK(buckets); // Use-after-destruction by user.
    auto newbuckets = Buckets::create(bucket_count, cohort);
    load_factor_nodes_ = bucket_count * load_factor_;
    for (size_t i = 0; i < oldcount; i++) {
      auto bucket = &buckets->buckets_[i]();
      auto node = bucket->load(std::memory_order_relaxed);
      if (!node) {
        continue;
      }
      auto h = HashFn()(node->getItem().first);
      auto idx = getIdx(bucket_count, h);
      // Reuse as long a chain as possible from the end.  Since the
      // nodes don't have previous pointers, the longest last chain
      // will be the same for both the previous hashmap and the new one,
      // assuming all the nodes hash to the same bucket.
      auto lastrun = node;
      auto lastidx = idx;
      auto count = 0;
      auto last = node->next_.load(std::memory_order_relaxed);
      for (; last != nullptr;
           last = last->next_.load(std::memory_order_relaxed)) {
        auto k = getIdx(bucket_count, HashFn()(last->getItem().first));
        if (k != lastidx) {
          lastidx = k;
          lastrun = last;
          count = 0;
        }
        count++;
      }
      // Set longest last run in new bucket, incrementing the refcount.
      lastrun->acquire_link(); // defined in hazptr_obj_base_linked
      newbuckets->buckets_[lastidx]().store(lastrun, std::memory_order_relaxed);
      // Clone remaining nodes
      for (; node != lastrun;
           node = node->next_.load(std::memory_order_relaxed)) {
        auto newnode = (Node*)Allocator().allocate(sizeof(Node));
        new (newnode) Node(cohort, node);
        auto k = getIdx(bucket_count, HashFn()(node->getItem().first));
        auto prevhead = &newbuckets->buckets_[k]();
        newnode->next_.store(prevhead->load(std::memory_order_relaxed));
        prevhead->store(newnode, std::memory_order_relaxed);
      }
    }

    auto oldbuckets = buckets_.load(std::memory_order_relaxed);
    DCHECK(oldbuckets); // Use-after-destruction by user.
    seqlock_.fetch_add(1, std::memory_order_release);
    bucket_count_.store(bucket_count, std::memory_order_release);
    buckets_.store(newbuckets, std::memory_order_release);
    seqlock_.fetch_add(1, std::memory_order_release);
    oldbuckets->retire(concurrenthashmap::HazptrTableDeleter(oldcount));
  }

  template <typename K>
  bool find(Iterator& res, const K& k) {
    auto& hazcurr = res.hazptrs_[1];
    auto& haznext = res.hazptrs_[2];
    auto h = HashFn()(k);
    size_t bcount;
    Buckets* buckets;
    getBucketsAndCount(bcount, buckets, res.hazptrs_[0]);

    auto idx = getIdx(bcount, h);
    auto prev = &buckets->buckets_[idx]();
    auto node = hazcurr.protect(*prev);
    while (node) {
      if (KeyEqual()(k, node->getItem().first)) {
        res.setNode(node, buckets, bcount, idx);
        return true;
      }
      node = haznext.protect(node->next_);
      hazcurr.swap(haznext);
    }
    return false;
  }

  template <typename K, typename MatchFunc>
  std::size_t erase(const K& key, Iterator* iter, MatchFunc match) {
    Node* node{nullptr};
    auto h = HashFn()(key);
    {
      std::lock_guard<Mutex> g(m_);

      size_t bcount = bucket_count_.load(std::memory_order_relaxed);
      auto buckets = buckets_.load(std::memory_order_relaxed);
      DCHECK(buckets); // Use-after-destruction by user.
      auto idx = getIdx(bcount, h);
      auto head = &buckets->buckets_[idx]();
      node = head->load(std::memory_order_relaxed);
      Node* prev = nullptr;
      while (node) {
        if (KeyEqual()(key, node->getItem().first)) {
          if (!match(node->getItem().second)) {
            return 0;
          }
          auto next = node->next_.load(std::memory_order_relaxed);
          if (next) {
            next->acquire_link(); // defined in hazptr_obj_base_linked
          }
          if (prev) {
            prev->next_.store(next, std::memory_order_release);
          } else {
            // Must be head of list.
            head->store(next, std::memory_order_release);
          }

          if (iter) {
            iter->hazptrs_[0].reset_protection(buckets);
            iter->setNode(
                node->next_.load(std::memory_order_acquire),
                buckets,
                bcount,
                idx);
            iter->next();
          }
          decSize();
          break;
        }
        prev = node;
        node = node->next_.load(std::memory_order_relaxed);
      }
    }
    // Delete the node while not under the lock.
    if (node) {
      node->release();
      return 1;
    }

    return 0;
  }

  void clear(hazptr_obj_cohort<Atom>* cohort) {
    size_t bcount = bucket_count_.load(std::memory_order_relaxed);
    Buckets* buckets;
    auto newbuckets = Buckets::create(bcount, cohort);
    {
      std::lock_guard<Mutex> g(m_);
      buckets = buckets_.load(std::memory_order_relaxed);
      buckets_.store(newbuckets, std::memory_order_release);
      clearSize();
    }
    DCHECK(buckets); // Use-after-destruction by user.
    buckets->retire(concurrenthashmap::HazptrTableDeleter(bcount));
  }

  void max_load_factor(float factor) {
    std::lock_guard<Mutex> g(m_);
    load_factor_ = factor;
    load_factor_nodes_ =
        bucket_count_.load(std::memory_order_relaxed) * load_factor_;
  }

  Iterator cbegin() {
    Iterator res;
    size_t bcount;
    Buckets* buckets;
    getBucketsAndCount(bcount, buckets, res.hazptrs_[0]);
    res.setNode(nullptr, buckets, bcount, 0);
    res.next();
    return res;
  }

  Iterator cend() { return Iterator(nullptr); }

 private:
  // Could be optimized to avoid an extra pointer dereference by
  // allocating buckets_ at the same time.
  class Buckets : public hazptr_obj_base<
                      Buckets,
                      Atom,
                      concurrenthashmap::HazptrTableDeleter> {
    using BucketRoot = hazptr_root<Node, Atom>;

    Buckets() {}
    ~Buckets() {}

   public:
    static Buckets* create(size_t count, hazptr_obj_cohort<Atom>* cohort) {
      auto buf =
          Allocator().allocate(sizeof(Buckets) + sizeof(BucketRoot) * count);
      auto buckets = new (buf) Buckets();
      DCHECK(cohort);
      buckets->set_cohort_tag(cohort); // defined in hazptr_obj
      for (size_t i = 0; i < count; i++) {
        new (&buckets->buckets_[i]) BucketRoot;
      }
      return buckets;
    }

    void destroy(size_t count) {
      for (size_t i = 0; i < count; i++) {
        buckets_[i].~BucketRoot();
      }
      this->~Buckets();
      Allocator().deallocate(
          (uint8_t*)this, sizeof(BucketRoot) * count + sizeof(*this));
    }

    void unlink_and_reclaim_nodes(size_t count) {
      for (size_t i = 0; i < count; i++) {
        auto node = buckets_[i]().load(std::memory_order_relaxed);
        if (node) {
          buckets_[i]().store(nullptr, std::memory_order_relaxed);
          while (node) {
            auto next = node->next_.load(std::memory_order_relaxed);
            if (next) {
              node->next_.store(nullptr, std::memory_order_relaxed);
            }
            node->unlink_and_reclaim_unchecked();
            node = next;
          }
        }
      }
    }

    BucketRoot buckets_[0];
  };

 public:
  class Iterator {
   public:
    FOLLY_ALWAYS_INLINE Iterator()
        : hazptrs_(make_hazard_pointer_array<3, Atom>()) {}
    FOLLY_ALWAYS_INLINE explicit Iterator(std::nullptr_t) : hazptrs_() {}
    FOLLY_ALWAYS_INLINE ~Iterator() {}

    void setNode(
        Node* node, Buckets* buckets, size_t bucket_count, uint64_t idx) {
      node_ = node;
      buckets_ = buckets;
      idx_ = idx;
      bucket_count_ = bucket_count;
    }

    const value_type& operator*() const {
      DCHECK(node_);
      return node_->getItem();
    }

    const value_type* operator->() const {
      DCHECK(node_);
      return &(node_->getItem());
    }

    const Iterator& operator++() {
      DCHECK(node_);
      node_ = hazptrs_[2].protect(node_->next_);
      hazptrs_[1].swap(hazptrs_[2]);
      if (!node_) {
        ++idx_;
        next();
      }
      return *this;
    }

    void next() {
      while (!node_) {
        if (idx_ >= bucket_count_) {
          break;
        }
        DCHECK(buckets_);
        node_ = hazptrs_[1].protect(buckets_->buckets_[idx_]());
        if (node_) {
          break;
        }
        ++idx_;
      }
    }

    bool operator==(const Iterator& o) const { return node_ == o.node_; }

    bool operator!=(const Iterator& o) const { return !(*this == o); }

    Iterator& operator=(const Iterator& o) = delete;

    Iterator& operator=(Iterator&& o) noexcept {
      if (this != &o) {
        hazptrs_ = std::move(o.hazptrs_);
        node_ = std::exchange(o.node_, nullptr);
        buckets_ = std::exchange(o.buckets_, nullptr);
        bucket_count_ = std::exchange(o.bucket_count_, 0);
        idx_ = std::exchange(o.idx_, 0);
      }
      return *this;
    }

    Iterator(const Iterator& o) = delete;

    Iterator(Iterator&& o) noexcept
        : hazptrs_(std::move(o.hazptrs_)),
          node_(std::exchange(o.node_, nullptr)),
          buckets_(std::exchange(o.buckets_, nullptr)),
          bucket_count_(std::exchange(o.bucket_count_, 0)),
          idx_(std::exchange(o.idx_, 0)) {}

    // These are accessed directly from the functions above
    hazptr_array<3, Atom> hazptrs_;

   private:
    Node* node_{nullptr};
    Buckets* buckets_{nullptr};
    size_t bucket_count_{0};
    uint64_t idx_{0};
  };

 private:
  // Shards have already used low ShardBits of the hash.
  // Shift it over to use fresh bits.
  uint64_t getIdx(size_t bucket_count, size_t hash) {
    return (hash >> ShardBits) & (bucket_count - 1);
  }
  void getBucketsAndCount(
      size_t& bcount, Buckets*& buckets, hazptr_holder<Atom>& hazptr) {
    while (true) {
      auto seqlock = seqlock_.load(std::memory_order_acquire);
      bcount = bucket_count_.load(std::memory_order_acquire);
      buckets = hazptr.protect(buckets_);
      auto seqlock2 = seqlock_.load(std::memory_order_acquire);
      if (!(seqlock & 1) && (seqlock == seqlock2)) {
        break;
      }
    }
    DCHECK(buckets) << "Use-after-destruction by user.";
  }

  template <typename MatchFunc, typename K, typename... Args>
  bool doInsert(
      Iterator& it,
      const K& k,
      InsertType type,
      MatchFunc match,
      Node* cur,
      hazptr_obj_cohort<Atom>* cohort,
      Args&&... args) {
    auto h = HashFn()(k);
    std::unique_lock<Mutex> g(m_);

    size_t bcount = bucket_count_.load(std::memory_order_relaxed);
    auto buckets = buckets_.load(std::memory_order_relaxed);
    // Check for rehash needed for DOES_NOT_EXIST
    if (size() >= load_factor_nodes_ && type == InsertType::DOES_NOT_EXIST) {
      if (max_size_ && size() << 1 > max_size_) {
        // Would exceed max size.
        throw_exception<std::bad_alloc>();
      }
      rehash(bcount << 1, cohort);
      buckets = buckets_.load(std::memory_order_relaxed);
      bcount = bucket_count_.load(std::memory_order_relaxed);
    }

    DCHECK(buckets) << "Use-after-destruction by user.";
    auto idx = getIdx(bcount, h);
    auto head = &buckets->buckets_[idx]();
    auto node = head->load(std::memory_order_relaxed);
    auto headnode = node;
    auto prev = head;
    auto& hazbuckets = it.hazptrs_[0];
    auto& haznode = it.hazptrs_[1];
    hazbuckets.reset_protection(buckets);
    while (node) {
      // Is the key found?
      if (KeyEqual()(k, node->getItem().first)) {
        it.setNode(node, buckets, bcount, idx);
        haznode.reset_protection(node);
        if (type == InsertType::MATCH) {
          if (!match(node->getItem().second)) {
            return false;
          }
        }
        if (type == InsertType::DOES_NOT_EXIST) {
          return false;
        } else {
          if (!cur) {
            cur = (Node*)Allocator().allocate(sizeof(Node));
            new (cur) Node(cohort, std::forward<Args>(args)...);
          }
          auto next = node->next_.load(std::memory_order_relaxed);
          cur->next_.store(next, std::memory_order_relaxed);
          if (next) {
            next->acquire_link(); // defined in hazptr_obj_base_linked
          }
          prev->store(cur, std::memory_order_release);
          it.setNode(cur, buckets, bcount, idx);
          haznode.reset_protection(cur);
          g.unlock();
          // Release not under lock.
          node->release();
          return true;
        }
      }

      prev = &node->next_;
      node = node->next_.load(std::memory_order_relaxed);
    }
    if (type != InsertType::DOES_NOT_EXIST && type != InsertType::ANY) {
      haznode.reset_protection();
      hazbuckets.reset_protection();
      return false;
    }
    // Node not found, check for rehash on ANY
    if (size() >= load_factor_nodes_ && type == InsertType::ANY) {
      if (max_size_ && size() << 1 > max_size_) {
        // Would exceed max size.
        throw_exception<std::bad_alloc>();
      }
      rehash(bcount << 1, cohort);

      // Reload correct bucket.
      buckets = buckets_.load(std::memory_order_relaxed);
      DCHECK(buckets); // Use-after-destruction by user.
      bcount <<= 1;
      hazbuckets.reset_protection(buckets);
      idx = getIdx(bcount, h);
      head = &buckets->buckets_[idx]();
      headnode = head->load(std::memory_order_relaxed);
    }

    // We found a slot to put the node.
    incSize();
    if (!cur) {
      // InsertType::ANY
      // OR DOES_NOT_EXIST, but only in the try_emplace case
      DCHECK(type == InsertType::ANY || type == InsertType::DOES_NOT_EXIST);
      cur = (Node*)Allocator().allocate(sizeof(Node));
      new (cur) Node(cohort, std::forward<Args>(args)...);
    }
    cur->next_.store(headnode, std::memory_order_relaxed);
    head->store(cur, std::memory_order_release);
    it.setNode(cur, buckets, bcount, idx);
    haznode.reset_protection(cur);
    return true;
  }

  Mutex m_;
  float load_factor_;
  size_t load_factor_nodes_;
  Atom<size_t> size_{0};
  size_t const max_size_;

  // Fields needed for read-only access, on separate cacheline.
  alignas(64) Atom<Buckets*> buckets_{nullptr};
  std::atomic<uint64_t> seqlock_{0};
  Atom<size_t> bucket_count_;
};

} // namespace bucket

#if FOLLY_SSE_PREREQ(4, 2) && !FOLLY_MOBILE

namespace simd {

using folly::f14::detail::DenseMaskIter;
using folly::f14::detail::FirstEmptyInMask;
using folly::f14::detail::FullMask;
using folly::f14::detail::MaskType;
using folly::f14::detail::SparseMaskIter;

using folly::hazptr_obj_base;
using folly::hazptr_obj_cohort;

template <
    typename KeyType,
    typename ValueType,
    typename Allocator,
    template <typename> class Atom = std::atomic>
class NodeT : public hazptr_obj_base<
                  NodeT<KeyType, ValueType, Allocator, Atom>,
                  Atom,
                  HazptrDeleter<Allocator>> {
 public:
  typedef std::pair<const KeyType, ValueType> value_type;

  template <typename Arg, typename... Args>
  NodeT(hazptr_obj_cohort<Atom>* cohort, Arg&& k, Args&&... args)
      : item_(
            std::piecewise_construct,
            std::forward_as_tuple(std::forward<Arg>(k)),
            std::forward_as_tuple(std::forward<Args>(args)...)) {
    init(cohort);
  }

  value_type& getItem() { return item_; }

 private:
  void init(hazptr_obj_cohort<Atom>* cohort) {
    DCHECK(cohort);
    this->set_deleter( // defined in hazptr_obj
        HazptrDeleter<Allocator>());
    this->set_cohort_tag(cohort); // defined in hazptr_obj
  }

  value_type item_;
};

constexpr std::size_t kRequiredVectorAlignment =
    constexpr_max(std::size_t{16}, alignof(max_align_t));

template <
    typename KeyType,
    typename ValueType,
    uint8_t ShardBits = 0,
    typename HashFn = std::hash<KeyType>,
    typename KeyEqual = std::equal_to<KeyType>,
    typename Allocator = std::allocator<uint8_t>,
    template <typename> class Atom = std::atomic,
    class Mutex = std::mutex>
class alignas(64) SIMDTable {
 public:
  using Node =
      concurrenthashmap::simd::NodeT<KeyType, ValueType, Allocator, Atom>;

 private:
  using HashPair = std::pair<std::size_t, std::size_t>;
  struct alignas(kRequiredVectorAlignment) Chunk {
    static constexpr unsigned kCapacity = 14;
    static constexpr unsigned kDesiredCapacity = 12;

    static constexpr MaskType kFullMask = FullMask<kCapacity>::value;

   private:
    // Non-empty tags have their top bit set.

    // tags [0,8)
    Atom<uint64_t> tags_low_;

    // tags_hi_ holds tags [8,14), hostedOverflowCount and outboundOverflowCount

    // hostedOverflowCount: the number of values in this chunk that were placed
    // because they overflowed their desired chunk.

    // outboundOverflowCount: num values that would have been placed into this
    // chunk if there had been space, including values that also overflowed
    // previous full chunks.  This value saturates; once it becomes 255 it no
    // longer increases nor decreases.

    // Note: more bits can be used for outboundOverflowCount if this
    // optimization becomes useful
    Atom<uint64_t> tags_hi_;

    std::array<aligned_storage_for_t<Atom<Node*>>, kCapacity> rawItems_;

   public:
    void clear() {
      for (size_t i = 0; i < kCapacity; i++) {
        item(i).store(nullptr, std::memory_order_relaxed);
      }
      tags_low_.store(0, std::memory_order_relaxed);
      tags_hi_.store(0, std::memory_order_relaxed);
    }

    std::size_t tag(std::size_t index) const {
      std::size_t off = index % 8;
      const Atom<uint64_t>& tag_src = off == index ? tags_low_ : tags_hi_;
      uint64_t tags = tag_src.load(std::memory_order_relaxed);
      tags >>= (off * 8);
      return tags & 0xff;
    }

    void setTag(std::size_t index, std::size_t tag) {
      std::size_t off = index % 8;
      Atom<uint64_t>& old_tags = off == index ? tags_low_ : tags_hi_;
      uint64_t new_tags = old_tags.load(std::memory_order_relaxed);
      uint64_t mask = 0xffUL << (off * 8);
      new_tags = (new_tags & ~mask) | (tag << (off * 8));
      old_tags.store(new_tags, std::memory_order_release);
    }

    void setNodeAndTag(std::size_t index, Node* node, std::size_t tag) {
      FOLLY_SAFE_DCHECK(
          index < kCapacity && (tag == 0x0 || (tag >= 0x80 && tag <= 0xff)),
          "");
      item(index).store(node, std::memory_order_release);
      setTag(index, tag);
    }

    void clearNodeAndTag(std::size_t index) {
      setNodeAndTag(index, nullptr, 0);
    }

    ////////
    // Tag filtering using SSE2 intrinsics

    SparseMaskIter tagMatchIter(std::size_t needle) const {
      FOLLY_SAFE_DCHECK(needle >= 0x80 && needle < 0x100, "");
      uint64_t low = tags_low_.load(std::memory_order_acquire);
      uint64_t hi = tags_hi_.load(std::memory_order_acquire);
      auto tagV = _mm_set_epi64x(hi, low);
      auto needleV = _mm_set1_epi8(static_cast<uint8_t>(needle));
      auto eqV = _mm_cmpeq_epi8(tagV, needleV);
      auto mask = _mm_movemask_epi8(eqV) & kFullMask;
      return SparseMaskIter{mask};
    }

    MaskType occupiedMask() const {
      uint64_t low = tags_low_.load(std::memory_order_relaxed);
      uint64_t hi = tags_hi_.load(std::memory_order_relaxed);
      auto tagV = _mm_set_epi64x(hi, low);
      return _mm_movemask_epi8(tagV) & kFullMask;
    }

    DenseMaskIter occupiedIter() const {
      // Currently only invoked when relaxed semantics are sufficient.
      return DenseMaskIter{nullptr /*unused*/, occupiedMask()};
    }

    FirstEmptyInMask firstEmpty() const {
      return FirstEmptyInMask{occupiedMask() ^ kFullMask};
    }

    Atom<Node*>* itemAddr(std::size_t i) const {
      return static_cast<Atom<Node*>*>(
          const_cast<void*>(static_cast<void const*>(&rawItems_[i])));
    }

    Atom<Node*>& item(size_t i) { return *launder(itemAddr(i)); }

    static constexpr uint64_t kOutboundOverflowIndex = 7 * 8;
    static constexpr uint64_t kSaturatedOutboundOverflowCount = 0xffUL
        << kOutboundOverflowIndex;
    static constexpr uint64_t kOutboundOverflowOperand = 0x1UL
        << kOutboundOverflowIndex;

    unsigned outboundOverflowCount() const {
      uint64_t count = tags_hi_.load(std::memory_order_relaxed);
      return count >> kOutboundOverflowIndex;
    }

    void incrOutboundOverflowCount() {
      uint64_t count = tags_hi_.load(std::memory_order_relaxed);
      if (count < kSaturatedOutboundOverflowCount) {
        tags_hi_.store(
            count + kOutboundOverflowOperand, std::memory_order_relaxed);
      }
    }

    void decrOutboundOverflowCount() {
      uint64_t count = tags_hi_.load(std::memory_order_relaxed);
      if (count < kSaturatedOutboundOverflowCount) {
        tags_hi_.store(
            count - kOutboundOverflowOperand, std::memory_order_relaxed);
      }
    }

    static constexpr uint64_t kHostedOverflowIndex = 6 * 8;
    static constexpr uint64_t kHostedOverflowOperand = 0x10UL
        << kHostedOverflowIndex;

    unsigned hostedOverflowCount() const {
      uint64_t control = tags_hi_.load(std::memory_order_relaxed);
      return (control >> 52) & 0xf;
    }

    void incrHostedOverflowCount() {
      tags_hi_.fetch_add(kHostedOverflowOperand, std::memory_order_relaxed);
    }

    void decrHostedOverflowCount() {
      tags_hi_.fetch_sub(kHostedOverflowOperand, std::memory_order_relaxed);
    }
  };

  class Chunks : public hazptr_obj_base<Chunks, Atom, HazptrTableDeleter> {
    Chunks() {}
    ~Chunks() {}

   public:
    static Chunks* create(size_t count, hazptr_obj_cohort<Atom>* cohort) {
      auto buf = Allocator().allocate(sizeof(Chunks) + sizeof(Chunk) * count);
      auto chunks = new (buf) Chunks();
      DCHECK(cohort);
      chunks->set_cohort_tag(cohort); // defined in hazptr_obj
      for (size_t i = 0; i < count; i++) {
        new (&chunks->chunks_[i]) Chunk;
        chunks->chunks_[i].clear();
      }
      return chunks;
    }

    void destroy(size_t count) {
      for (size_t i = 0; i < count; i++) {
        chunks_[i].~Chunk();
      }
      this->~Chunks();
      Allocator().deallocate(
          (uint8_t*)this, sizeof(Chunk) * count + sizeof(*this));
    }

    void reclaim_nodes(size_t count) {
      for (size_t i = 0; i < count; i++) {
        Chunk& chunk = chunks_[i];
        auto occupied = chunk.occupiedIter();
        while (occupied.hasNext()) {
          auto idx = occupied.next();
          chunk.setTag(idx, 0);
          Node* node =
              chunk.item(idx).exchange(nullptr, std::memory_order_relaxed);
          // Tags and node ptrs should be in sync at this point.
          DCHECK(node);
          node->retire();
        }
      }
    }

    Chunk* getChunk(size_t index, size_t ccount) {
      DCHECK(isPowTwo(ccount));
      return &chunks_[index & (ccount - 1)];
    }

   private:
    Chunk chunks_[0];
  };

 public:
  static constexpr float kDefaultLoadFactor =
      Chunk::kDesiredCapacity / (float)Chunk::kCapacity;

  typedef std::pair<const KeyType, ValueType> value_type;

  using InsertType = concurrenthashmap::InsertType;

  class Iterator {
   public:
    FOLLY_ALWAYS_INLINE Iterator()
        : hazptrs_(make_hazard_pointer_array<2, Atom>()) {}
    FOLLY_ALWAYS_INLINE explicit Iterator(std::nullptr_t) : hazptrs_() {}
    FOLLY_ALWAYS_INLINE ~Iterator() {}

    void setNode(
        Node* node,
        Chunks* chunks,
        size_t chunk_count,
        uint64_t chunk_idx,
        uint64_t tag_idx) {
      DCHECK(chunk_idx < chunk_count || chunk_idx == 0);
      DCHECK(isPowTwo(chunk_count));
      node_ = node;
      chunks_ = chunks;
      chunk_count_ = chunk_count;
      chunk_idx_ = chunk_idx;
      tag_idx_ = tag_idx;
    }

    const value_type& operator*() const {
      DCHECK(node_);
      return node_->getItem();
    }

    const value_type* operator->() const {
      DCHECK(node_);
      return &(node_->getItem());
    }

    const Iterator& operator++() {
      DCHECK(node_);
      ++tag_idx_;
      findNextNode();
      return *this;
    }

    void next() {
      if (node_) {
        return;
      }
      findNextNode();
    }

    bool operator==(const Iterator& o) const { return node_ == o.node_; }

    bool operator!=(const Iterator& o) const { return !(*this == o); }

    Iterator& operator=(const Iterator& o) = delete;

    Iterator& operator=(Iterator&& o) noexcept {
      if (this != &o) {
        hazptrs_ = std::move(o.hazptrs_);
        node_ = std::exchange(o.node_, nullptr);
        chunks_ = std::exchange(o.chunks_, nullptr);
        chunk_count_ = std::exchange(o.chunk_count_, 0);
        chunk_idx_ = std::exchange(o.chunk_idx_, 0);
        tag_idx_ = std::exchange(o.tag_idx_, 0);
      }
      return *this;
    }

    Iterator(const Iterator& o) = delete;

    Iterator(Iterator&& o) noexcept
        : hazptrs_(std::move(o.hazptrs_)),
          node_(std::exchange(o.node_, nullptr)),
          chunks_(std::exchange(o.chunks_, nullptr)),
          chunk_count_(std::exchange(o.chunk_count_, 0)),
          chunk_idx_(std::exchange(o.chunk_idx_, 0)),
          tag_idx_(std::exchange(o.tag_idx_, 0)) {}

    // These are accessed directly from the functions above
    hazptr_array<2, Atom> hazptrs_;

   private:
    void findNextNode() {
      do {
        if (tag_idx_ >= Chunk::kCapacity) {
          tag_idx_ = 0;
          ++chunk_idx_;
        }
        if (chunk_idx_ >= chunk_count_) {
          node_ = nullptr;
          break;
        }
        DCHECK(chunks_);
        // Note that iteration could also be implemented with tag filtering
        node_ = hazptrs_[1].protect(
            chunks_->getChunk(chunk_idx_, chunk_count_)->item(tag_idx_));
        if (node_) {
          break;
        }
        ++tag_idx_;
      } while (true);
    }

    Node* node_{nullptr};
    Chunks* chunks_{nullptr};
    size_t chunk_count_{0};
    uint64_t chunk_idx_{0};
    uint64_t tag_idx_{0};
  };

  SIMDTable(
      size_t initial_size,
      float load_factor,
      size_t max_size,
      hazptr_obj_cohort<Atom>* cohort)
      : load_factor_(load_factor),
        max_size_(max_size),
        chunks_(nullptr),
        chunk_count_(0) {
    DCHECK(cohort);
    DCHECK(
        max_size_ == 0 ||
        (isPowTwo(max_size_) &&
         (folly::popcount(max_size_ - 1) + ShardBits <= 32)));
    DCHECK(load_factor_ > 0.0);
    load_factor_ = std::min<float>(load_factor_, 1.0);
    rehash(initial_size, cohort);
  }

  ~SIMDTable() {
    auto chunks = chunks_.load(std::memory_order_relaxed);
    // To catch use-after-destruction bugs in user code.
    chunks_.store(nullptr, std::memory_order_release);
    // We can delete and not retire() here, since users must have
    // their own synchronization around destruction.
    auto count = chunk_count_.load(std::memory_order_relaxed);
    chunks->reclaim_nodes(count);
    chunks->destroy(count);
  }

  size_t size() { return size_.load(std::memory_order_acquire); }

  void clearSize() { size_.store(0, std::memory_order_release); }

  void incSize() {
    auto sz = size_.load(std::memory_order_relaxed);
    size_.store(sz + 1, std::memory_order_release);
  }

  void decSize() {
    auto sz = size_.load(std::memory_order_relaxed);
    DCHECK_GT(sz, 0);
    size_.store(sz - 1, std::memory_order_release);
  }

  bool empty() { return size() == 0; }

  template <typename MatchFunc, typename K, typename... Args>
  bool insert(
      Iterator& it,
      const K& k,
      InsertType type,
      MatchFunc match,
      hazptr_obj_cohort<Atom>* cohort,
      Args&&... args) {
    Node* node;
    Chunks* chunks;
    size_t ccount, chunk_idx, tag_idx;

    auto h = HashFn()(k);
    auto hp = splitHash(h);

    std::unique_lock<Mutex> g(m_);

    if (!prepare_insert(
            it,
            k,
            type,
            match,
            cohort,
            chunk_idx,
            tag_idx,
            node,
            chunks,
            ccount,
            hp)) {
      return false;
    }

    auto cur = (Node*)Allocator().allocate(sizeof(Node));
    new (cur) Node(cohort, std::forward<Args>(args)...);

    if (!node) {
      std::tie(chunk_idx, tag_idx) =
          findEmptyInsertLocation(chunks, ccount, hp);
      incSize();
    }

    Chunk* chunk = chunks->getChunk(chunk_idx, ccount);
    chunk->setNodeAndTag(tag_idx, cur, hp.second);
    it.setNode(cur, chunks, ccount, chunk_idx, tag_idx);
    it.hazptrs_[1].reset_protection(cur);

    g.unlock();
    // Retire not under lock
    if (node) {
      node->retire();
    }
    return true;
  }

  template <typename MatchFunc, typename K, typename... Args>
  bool insert(
      Iterator& it,
      const K& k,
      InsertType type,
      MatchFunc match,
      Node* cur,
      hazptr_obj_cohort<Atom>* cohort) {
    DCHECK(cur != nullptr);
    Node* node;
    Chunks* chunks;
    size_t ccount, chunk_idx, tag_idx;

    auto h = HashFn()(k);
    auto hp = splitHash(h);

    std::unique_lock<Mutex> g(m_);

    if (!prepare_insert(
            it,
            k,
            type,
            match,
            cohort,
            chunk_idx,
            tag_idx,
            node,
            chunks,
            ccount,
            hp)) {
      return false;
    }

    if (!node) {
      std::tie(chunk_idx, tag_idx) =
          findEmptyInsertLocation(chunks, ccount, hp);
      incSize();
    }

    Chunk* chunk = chunks->getChunk(chunk_idx, ccount);
    chunk->setNodeAndTag(tag_idx, cur, hp.second);
    it.setNode(cur, chunks, ccount, chunk_idx, tag_idx);
    it.hazptrs_[1].reset_protection(cur);

    g.unlock();
    // Retire not under lock
    if (node) {
      node->retire();
    }
    return true;
  }

  void rehash(size_t size, hazptr_obj_cohort<Atom>* cohort) {
    size_t new_chunk_count = size == 0 ? 0 : (size - 1) / Chunk::kCapacity + 1;
    rehash_internal(folly::nextPowTwo(new_chunk_count), cohort);
  }

  template <typename K>
  bool find(Iterator& res, const K& k) {
    auto& hazz = res.hazptrs_[1];
    auto h = HashFn()(k);
    auto hp = splitHash(h);
    size_t ccount;
    Chunks* chunks;
    getChunksAndCount(ccount, chunks, res.hazptrs_[0]);

    size_t step = probeDelta(hp);
    auto& chunk_idx = hp.first;
    for (size_t tries = 0; tries < ccount; ++tries) {
      Chunk* chunk = chunks->getChunk(chunk_idx, ccount);
      auto hits = chunk->tagMatchIter(hp.second);
      while (hits.hasNext()) {
        size_t tag_idx = hits.next();
        Node* node = hazz.protect(chunk->item(tag_idx));
        if (LIKELY(node && KeyEqual()(k, node->getItem().first))) {
          chunk_idx = chunk_idx & (ccount - 1);
          res.setNode(node, chunks, ccount, chunk_idx, tag_idx);
          return true;
        }
        hazz.reset_protection();
      }

      if (LIKELY(chunk->outboundOverflowCount() == 0)) {
        break;
      }
      chunk_idx += step;
    }
    return false;
  }

  template <typename K, typename MatchFunc>
  std::size_t erase(const K& key, Iterator* iter, MatchFunc match) {
    auto h = HashFn()(key);
    const HashPair hp = splitHash(h);

    std::unique_lock<Mutex> g(m_);

    size_t ccount = chunk_count_.load(std::memory_order_relaxed);
    auto chunks = chunks_.load(std::memory_order_relaxed);
    DCHECK(chunks); // Use-after-destruction by user.
    size_t chunk_idx, tag_idx;

    Node* node = find_internal(key, hp, chunks, ccount, chunk_idx, tag_idx);

    if (!node) {
      return 0;
    }

    if (!match(node->getItem().second)) {
      return 0;
    }

    Chunk* chunk = chunks->getChunk(chunk_idx, ccount);

    // Decrement any overflow counters
    if (chunk->hostedOverflowCount() != 0) {
      size_t index = hp.first;
      size_t delta = probeDelta(hp);
      bool preferredChunk = true;
      while (true) {
        Chunk* overflowChunk = chunks->getChunk(index, ccount);
        if (chunk == overflowChunk) {
          if (!preferredChunk) {
            overflowChunk->decrHostedOverflowCount();
          }
          break;
        }
        overflowChunk->decrOutboundOverflowCount();
        preferredChunk = false;
        index += delta;
      }
    }

    chunk->clearNodeAndTag(tag_idx);

    decSize();
    if (iter) {
      iter->hazptrs_[0].reset_protection(chunks);
      iter->setNode(nullptr, chunks, ccount, chunk_idx, tag_idx + 1);
      iter->next();
    }
    // Retire the node while not under the lock.
    g.unlock();
    node->retire();
    return 1;
  }

  void clear(hazptr_obj_cohort<Atom>* cohort) {
    size_t ccount = chunk_count_.load(std::memory_order_relaxed);
    Chunks* chunks;
    auto newchunks = Chunks::create(ccount, cohort);
    {
      std::lock_guard<Mutex> g(m_);
      chunks = chunks_.load(std::memory_order_relaxed);
      chunks_.store(newchunks, std::memory_order_release);
      clearSize();
    }
    DCHECK(chunks); // Use-after-destruction by user.
    chunks->reclaim_nodes(ccount);
    chunks->retire(HazptrTableDeleter(ccount));
  }

  void max_load_factor(float factor) {
    DCHECK(factor > 0.0);
    if (factor > 1.0) {
      throw_exception<std::invalid_argument>("load factor must be <= 1.0");
    }
    std::lock_guard<Mutex> g(m_);
    load_factor_ = factor;
    auto ccount = chunk_count_.load(std::memory_order_relaxed);
    grow_threshold_ = ccount * Chunk::kCapacity * load_factor_;
  }

  Iterator cbegin() {
    Iterator res;
    size_t ccount;
    Chunks* chunks;
    getChunksAndCount(ccount, chunks, res.hazptrs_[0]);
    res.setNode(nullptr, chunks, ccount, 0, 0);
    res.next();
    return res;
  }

  Iterator cend() { return Iterator(nullptr); }

 private:
  static HashPair splitHash(std::size_t hash) {
    std::size_t c = _mm_crc32_u64(0, hash);
    size_t tag = (c >> 24) | 0x80;
    hash += c;
    return std::make_pair(hash, tag);
  }

  static size_t probeDelta(HashPair hp) { return 2 * hp.second + 1; }

  // Must hold lock.
  template <typename K>
  Node* find_internal(
      const K& k,
      const HashPair& hp,
      Chunks* chunks,
      size_t ccount,
      size_t& chunk_idx,
      size_t& tag_idx) {
    // must be called with mutex held
    size_t step = probeDelta(hp);
    chunk_idx = hp.first;

    for (size_t tries = 0; tries < ccount; ++tries) {
      Chunk* chunk = chunks->getChunk(chunk_idx, ccount);
      auto hits = chunk->tagMatchIter(hp.second);
      while (hits.hasNext()) {
        tag_idx = hits.next();
        Node* node = chunk->item(tag_idx).load(std::memory_order_acquire);
        if (LIKELY(node && KeyEqual()(k, node->getItem().first))) {
          chunk_idx = (chunk_idx & (ccount - 1));
          return node;
        }
      }
      if (LIKELY(chunk->outboundOverflowCount() == 0)) {
        break;
      }
      chunk_idx += step;
    }
    return nullptr;
  }

  template <typename MatchFunc, typename K, typename... Args>
  bool prepare_insert(
      Iterator& it,
      const K& k,
      InsertType type,
      MatchFunc match,
      hazptr_obj_cohort<Atom>* cohort,
      size_t& chunk_idx,
      size_t& tag_idx,
      Node*& node,
      Chunks*& chunks,
      size_t& ccount,
      const HashPair& hp) {
    ccount = chunk_count_.load(std::memory_order_relaxed);
    chunks = chunks_.load(std::memory_order_relaxed);

    if (size() >= grow_threshold_ && type == InsertType::DOES_NOT_EXIST) {
      if (max_size_ && size() << 1 > max_size_) {
        // Would exceed max size.
        throw_exception<std::bad_alloc>();
      }
      rehash_internal(ccount << 1, cohort);
      ccount = chunk_count_.load(std::memory_order_relaxed);
      chunks = chunks_.load(std::memory_order_relaxed);
    }

    DCHECK(chunks); // Use-after-destruction by user.
    node = find_internal(k, hp, chunks, ccount, chunk_idx, tag_idx);

    it.hazptrs_[0].reset_protection(chunks);
    if (node) {
      it.hazptrs_[1].reset_protection(node);
      it.setNode(node, chunks, ccount, chunk_idx, tag_idx);
      if (type == InsertType::MATCH) {
        if (!match(node->getItem().second)) {
          return false;
        }
      } else if (type == InsertType::DOES_NOT_EXIST) {
        return false;
      }
    } else {
      if (type != InsertType::DOES_NOT_EXIST && type != InsertType::ANY) {
        it.hazptrs_[0].reset_protection();
        return false;
      }
      // Already checked for rehash on DOES_NOT_EXIST, now check on ANY
      if (size() >= grow_threshold_ && type == InsertType::ANY) {
        if (max_size_ && size() << 1 > max_size_) {
          // Would exceed max size.
          throw_exception<std::bad_alloc>();
        }
        rehash_internal(ccount << 1, cohort);
        ccount = chunk_count_.load(std::memory_order_relaxed);
        chunks = chunks_.load(std::memory_order_relaxed);
        DCHECK(chunks); // Use-after-destruction by user.
        it.hazptrs_[0].reset_protection(chunks);
      }
    }
    return true;
  }

  void rehash_internal(
      size_t new_chunk_count, hazptr_obj_cohort<Atom>* cohort) {
    DCHECK(isPowTwo(new_chunk_count));
    auto old_chunk_count = chunk_count_.load(std::memory_order_relaxed);
    if (old_chunk_count >= new_chunk_count) {
      return;
    }
    auto new_chunks = Chunks::create(new_chunk_count, cohort);
    auto old_chunks = chunks_.load(std::memory_order_relaxed);
    grow_threshold_ = new_chunk_count * Chunk::kCapacity * load_factor_;

    for (size_t i = 0; i < old_chunk_count; i++) {
      DCHECK(old_chunks); // Use-after-destruction by user.
      Chunk* oldchunk = old_chunks->getChunk(i, old_chunk_count);
      auto occupied = oldchunk->occupiedIter();
      while (occupied.hasNext()) {
        auto idx = occupied.next();
        Node* node = oldchunk->item(idx).load(std::memory_order_relaxed);
        size_t new_chunk_idx;
        size_t new_tag_idx;
        auto h = HashFn()(node->getItem().first);
        auto hp = splitHash(h);
        std::tie(new_chunk_idx, new_tag_idx) =
            findEmptyInsertLocation(new_chunks, new_chunk_count, hp);
        Chunk* newchunk = new_chunks->getChunk(new_chunk_idx, new_chunk_count);
        newchunk->setNodeAndTag(new_tag_idx, node, hp.second);
      }
    }

    seqlock_.fetch_add(1, std::memory_order_release);
    chunk_count_.store(new_chunk_count, std::memory_order_release);
    chunks_.store(new_chunks, std::memory_order_release);
    seqlock_.fetch_add(1, std::memory_order_release);
    if (old_chunks) {
      old_chunks->retire(HazptrTableDeleter(old_chunk_count));
    }
  }

  void getChunksAndCount(
      size_t& ccount, Chunks*& chunks, hazptr_holder<Atom>& hazptr) {
    while (true) {
      auto seqlock = seqlock_.load(std::memory_order_acquire);
      ccount = chunk_count_.load(std::memory_order_acquire);
      chunks = hazptr.protect(chunks_);
      auto seqlock2 = seqlock_.load(std::memory_order_acquire);
      if (!(seqlock & 1) && (seqlock == seqlock2)) {
        break;
      }
    }
    DCHECK(chunks);
  }

  std::pair<size_t, size_t> findEmptyInsertLocation(
      Chunks* chunks, size_t ccount, const HashPair& hp) {
    size_t chunk_idx = hp.first;
    Chunk* dst_chunk = chunks->getChunk(chunk_idx, ccount);
    auto firstEmpty = dst_chunk->firstEmpty();

    if (!firstEmpty.hasIndex()) {
      size_t delta = probeDelta(hp);
      do {
        dst_chunk->incrOutboundOverflowCount();
        chunk_idx += delta;
        dst_chunk = chunks->getChunk(chunk_idx, ccount);
        firstEmpty = dst_chunk->firstEmpty();
      } while (!firstEmpty.hasIndex());
      dst_chunk->incrHostedOverflowCount();
    }
    size_t dst_tag_idx = firstEmpty.index();
    return std::make_pair(chunk_idx & (ccount - 1), dst_tag_idx);
  }

  Mutex m_;
  float load_factor_; // ceil of 1.0
  size_t grow_threshold_;
  Atom<size_t> size_{0};
  size_t const max_size_;

  // Fields needed for read-only access, on separate cacheline.
  alignas(64) Atom<Chunks*> chunks_{nullptr};
  std::atomic<uint64_t> seqlock_{0};
  Atom<size_t> chunk_count_;
};
} // namespace simd

#endif // FOLLY_SSE_PREREQ(4, 2) && !FOLLY_MOBILE

} // namespace concurrenthashmap

/* A Segment is a single shard of the ConcurrentHashMap.
 * All writes take the lock, while readers are all wait-free.
 * Readers always proceed in parallel with the single writer.
 *
 *
 * Possible additional optimizations:
 *
 * * insert / erase could be lock / wait free.  Would need to be
 *   careful that assign and rehash don't conflict (possibly with
 *   reader/writer lock, or microlock per node or per bucket, etc).
 *   Java 8 goes halfway, and does lock per bucket, except for the
 *   first item, that is inserted with a CAS (which is somewhat
 *   specific to java having a lock per object)
 *
 * * I tried using trylock() and find() to warm the cache for insert()
 *   and erase() similar to Java 7, but didn't have much luck.
 *
 * * We could order elements using split ordering, for faster rehash,
 *   and no need to ever copy nodes.  Note that a full split ordering
 *   including dummy nodes increases the memory usage by 2x, but we
 *   could split the difference and still require a lock to set bucket
 *   pointers.
 */
template <
    typename KeyType,
    typename ValueType,
    uint8_t ShardBits = 0,
    typename HashFn = std::hash<KeyType>,
    typename KeyEqual = std::equal_to<KeyType>,
    typename Allocator = std::allocator<uint8_t>,
    template <typename> class Atom = std::atomic,
    class Mutex = std::mutex,
    template <
        typename,
        typename,
        uint8_t,
        typename,
        typename,
        typename,
        template <typename>
        class,
        class>
    class Impl = concurrenthashmap::bucket::BucketTable>
class alignas(64) ConcurrentHashMapSegment {
  using ImplT = Impl<
      KeyType,
      ValueType,
      ShardBits,
      HashFn,
      KeyEqual,
      Allocator,
      Atom,
      Mutex>;

 public:
  typedef KeyType key_type;
  typedef ValueType mapped_type;
  typedef std::pair<const KeyType, ValueType> value_type;
  typedef std::size_t size_type;

  using InsertType = concurrenthashmap::InsertType;
  using Iterator = typename ImplT::Iterator;
  using Node = typename ImplT::Node;
  static constexpr float kDefaultLoadFactor = ImplT::kDefaultLoadFactor;

  ConcurrentHashMapSegment(
      size_t initial_buckets,
      float load_factor,
      size_t max_size,
      hazptr_obj_cohort<Atom>* cohort)
      : impl_(initial_buckets, load_factor, max_size, cohort), cohort_(cohort) {
    DCHECK(cohort);
  }

  ~ConcurrentHashMapSegment() = default;

  size_t size() { return impl_.size(); }

  bool empty() { return impl_.empty(); }

  template <typename Key>
  bool insert(Iterator& it, std::pair<Key, mapped_type>&& foo) {
    return insert(it, std::move(foo.first), std::move(foo.second));
  }

  template <typename Key, typename Value>
  bool insert(Iterator& it, Key&& k, Value&& v) {
    auto node = (Node*)Allocator().allocate(sizeof(Node));
    new (node) Node(cohort_, std::forward<Key>(k), std::forward<Value>(v));
    auto res = insert_internal(
        it,
        node->getItem().first,
        InsertType::DOES_NOT_EXIST,
        [](const ValueType&) { return false; },
        node);
    if (!res) {
      node->~Node();
      Allocator().deallocate((uint8_t*)node, sizeof(Node));
    }
    return res;
  }

  template <typename Key, typename... Args>
  bool try_emplace(Iterator& it, Key&& k, Args&&... args) {
    // Note: first key is only ever compared.  Second is moved in to
    // create the node, and the first key is never touched again.
    return insert_internal(
        it,
        std::forward<Key>(k),
        InsertType::DOES_NOT_EXIST,
        [](const ValueType&) { return false; },
        std::forward<Key>(k),
        std::forward<Args>(args)...);
  }

  template <typename... Args>
  bool emplace(Iterator& it, const KeyType& k, Node* node) {
    return insert_internal(
        it,
        k,
        InsertType::DOES_NOT_EXIST,
        [](const ValueType&) { return false; },
        node);
  }

  template <typename Key, typename Value>
  bool insert_or_assign(Iterator& it, Key&& k, Value&& v) {
    auto node = (Node*)Allocator().allocate(sizeof(Node));
    new (node) Node(cohort_, std::forward<Key>(k), std::forward<Value>(v));
    auto res = insert_internal(
        it,
        node->getItem().first,
        InsertType::ANY,
        [](const ValueType&) { return false; },
        node);
    if (!res) {
      node->~Node();
      Allocator().deallocate((uint8_t*)node, sizeof(Node));
    }
    return res;
  }

  template <typename Key, typename Value>
  bool assign(Iterator& it, Key&& k, Value&& v) {
    auto node = (Node*)Allocator().allocate(sizeof(Node));
    new (node) Node(cohort_, std::forward<Key>(k), std::forward<Value>(v));
    auto res = insert_internal(
        it,
        node->getItem().first,
        InsertType::MUST_EXIST,
        [](const ValueType&) { return false; },
        node);
    if (!res) {
      node->~Node();
      Allocator().deallocate((uint8_t*)node, sizeof(Node));
    }
    return res;
  }

  template <typename Key, typename Value>
  bool assign_if_equal(
      Iterator& it, Key&& k, const ValueType& expected, Value&& desired) {
    auto node = (Node*)Allocator().allocate(sizeof(Node));
    new (node)
        Node(cohort_, std::forward<Key>(k), std::forward<Value>(desired));
    auto res = insert_internal(
        it,
        node->getItem().first,
        InsertType::MATCH,
        [&expected](const ValueType& v) { return v == expected; },
        node);
    if (!res) {
      node->~Node();
      Allocator().deallocate((uint8_t*)node, sizeof(Node));
    }
    return res;
  }

  template <typename MatchFunc, typename K, typename... Args>
  bool insert_internal(
      Iterator& it,
      const K& k,
      InsertType type,
      MatchFunc match,
      Args&&... args) {
    return impl_.insert(
        it, k, type, match, cohort_, std::forward<Args>(args)...);
  }

  template <typename MatchFunc, typename K, typename... Args>
  bool insert_internal(
      Iterator& it, const K& k, InsertType type, MatchFunc match, Node* cur) {
    return impl_.insert(it, k, type, match, cur, cohort_);
  }

  // Must hold lock.
  void rehash(size_t bucket_count) {
    impl_.rehash(folly::nextPowTwo(bucket_count), cohort_);
  }

  template <typename K>
  bool find(Iterator& res, const K& k) {
    return impl_.find(res, k);
  }

  // Listed separately because we need a prev pointer.
  template <typename K>
  size_type erase(const K& key) {
    return erase_internal(key, nullptr, [](const ValueType&) { return true; });
  }

  template <typename K, typename Predicate>
  size_type erase_key_if(const K& key, Predicate&& predicate) {
    return erase_internal(key, nullptr, std::forward<Predicate>(predicate));
  }

  template <typename K, typename MatchFunc>
  size_type erase_internal(const K& key, Iterator* iter, MatchFunc match) {
    return impl_.erase(key, iter, match);
  }

  // Unfortunately because we are reusing nodes on rehash, we can't
  // have prev pointers in the bucket chain.  We have to start the
  // search from the bucket.
  //
  // This is a small departure from standard stl containers: erase may
  // throw if hash or key_eq functions throw.
  void erase(Iterator& res, Iterator& pos) {
    erase_internal(pos->first, &res, [](const ValueType&) { return true; });
    // Invalidate the iterator.
    pos = cend();
  }

  void clear() { impl_.clear(cohort_); }

  void max_load_factor(float factor) { impl_.max_load_factor(factor); }

  Iterator cbegin() { return impl_.cbegin(); }

  Iterator cend() { return impl_.cend(); }

 private:
  ImplT impl_;
  hazptr_obj_cohort<Atom>* cohort_;
};
} // namespace detail
} // namespace folly
