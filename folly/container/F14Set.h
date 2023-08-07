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

/**
 * F14NodeSet, F14ValueSet, and F14VectorSet
 *
 * F14FastSet conditionally works like F14ValueSet or F14VectorSet
 *
 * See F14.md
 *
 * @author Nathan Bronson <ngbronson@fb.com>
 * @author Xiao Shi       <xshi@fb.com>
 */

#include <cstddef>
#include <initializer_list>
#include <tuple>

#include <folly/Portability.h>
#include <folly/container/View.h>
#include <folly/lang/SafeAssert.h>

#include <folly/container/F14Set-fwd.h>
#include <folly/container/Iterator.h>
#include <folly/container/detail/F14Policy.h>
#include <folly/container/detail/F14Table.h>
#include <folly/container/detail/Util.h>

#if FOLLY_F14_VECTOR_INTRINSICS_AVAILABLE

//////// Common case for supported platforms

namespace folly {
namespace f14 {
namespace detail {

template <typename Policy>
class F14BasicSet {
  template <typename K, typename T>
  using EnableHeterogeneousFind = std::enable_if_t<
      ::folly::detail::EligibleForHeterogeneousFind<
          typename Policy::Value,
          typename Policy::Hasher,
          typename Policy::KeyEqual,
          K>::value,
      T>;

  template <typename K, typename T>
  using EnableHeterogeneousInsert = std::enable_if_t<
      ::folly::detail::EligibleForHeterogeneousInsert<
          typename Policy::Value,
          typename Policy::Hasher,
          typename Policy::KeyEqual,
          K>::value,
      T>;

  template <typename K>
  using IsIter = std::is_same<typename Policy::Iter, remove_cvref_t<K>>;

  template <typename K, typename T>
  using EnableHeterogeneousErase = std::enable_if_t<
      ::folly::detail::EligibleForHeterogeneousFind<
          typename Policy::Value,
          typename Policy::Hasher,
          typename Policy::KeyEqual,
          std::conditional_t<IsIter<K>::value, typename Policy::Value, K>>::
              value &&
          !IsIter<K>::value,
      T>;

 public:
  //// PUBLIC - Member types

  using key_type = typename Policy::Value;
  using value_type = key_type;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using hasher = typename Policy::Hasher;
  using key_equal = typename Policy::KeyEqual;
  using allocator_type = typename Policy::Alloc;
  using reference = value_type&;
  using const_reference = value_type const&;
  using pointer = typename Policy::AllocTraits::pointer;
  using const_pointer = typename Policy::AllocTraits::const_pointer;
  using iterator = typename Policy::Iter;
  using const_iterator = iterator;

 private:
  using ItemIter = typename Policy::ItemIter;

 public:
  //// PUBLIC - Member functions

  F14BasicSet() noexcept(Policy::kDefaultConstructIsNoexcept) : table_{} {}

  explicit F14BasicSet(
      std::size_t initialCapacity,
      hasher const& hash = hasher{},
      key_equal const& eq = key_equal{},
      allocator_type const& alloc = allocator_type{})
      : table_{initialCapacity, hash, eq, alloc} {}

  explicit F14BasicSet(std::size_t initialCapacity, allocator_type const& alloc)
      : F14BasicSet(initialCapacity, hasher{}, key_equal{}, alloc) {}

  explicit F14BasicSet(
      std::size_t initialCapacity,
      hasher const& hash,
      allocator_type const& alloc)
      : F14BasicSet(initialCapacity, hash, key_equal{}, alloc) {}

  explicit F14BasicSet(allocator_type const& alloc)
      : F14BasicSet(0, hasher{}, key_equal{}, alloc) {}

  template <typename InputIt>
  F14BasicSet(
      InputIt first,
      InputIt last,
      std::size_t initialCapacity = 0,
      hasher const& hash = hasher{},
      key_equal const& eq = key_equal{},
      allocator_type const& alloc = allocator_type{})
      : table_{initialCapacity, hash, eq, alloc} {
    initialInsert(first, last, initialCapacity);
  }

  template <typename InputIt>
  F14BasicSet(
      InputIt first,
      InputIt last,
      std::size_t initialCapacity,
      allocator_type const& alloc)
      : table_{initialCapacity, hasher{}, key_equal{}, alloc} {
    initialInsert(first, last, initialCapacity);
  }

  template <typename InputIt>
  F14BasicSet(
      InputIt first,
      InputIt last,
      std::size_t initialCapacity,
      hasher const& hash,
      allocator_type const& alloc)
      : table_{initialCapacity, hash, key_equal{}, alloc} {
    initialInsert(first, last, initialCapacity);
  }

  F14BasicSet(F14BasicSet const& rhs) = default;

  F14BasicSet(F14BasicSet const& rhs, allocator_type const& alloc)
      : table_(rhs.table_, alloc) {}

  F14BasicSet(F14BasicSet&& rhs) = default;

  F14BasicSet(F14BasicSet&& rhs, allocator_type const& alloc) noexcept(
      Policy::kAllocIsAlwaysEqual)
      : table_{std::move(rhs.table_), alloc} {}

  F14BasicSet(
      std::initializer_list<value_type> init,
      std::size_t initialCapacity = 0,
      hasher const& hash = hasher{},
      key_equal const& eq = key_equal{},
      allocator_type const& alloc = allocator_type{})
      : table_{initialCapacity, hash, eq, alloc} {
    initialInsert(init.begin(), init.end(), initialCapacity);
  }

  F14BasicSet(
      std::initializer_list<value_type> init,
      std::size_t initialCapacity,
      allocator_type const& alloc)
      : table_{initialCapacity, hasher{}, key_equal{}, alloc} {
    initialInsert(init.begin(), init.end(), initialCapacity);
  }

  F14BasicSet(
      std::initializer_list<value_type> init,
      std::size_t initialCapacity,
      hasher const& hash,
      allocator_type const& alloc)
      : table_{initialCapacity, hash, key_equal{}, alloc} {
    initialInsert(init.begin(), init.end(), initialCapacity);
  }

  F14BasicSet& operator=(F14BasicSet const&) = default;

  F14BasicSet& operator=(F14BasicSet&&) = default;

  F14BasicSet& operator=(std::initializer_list<value_type> ilist) {
    clear();
    bulkInsert(ilist.begin(), ilist.end(), true);
    return *this;
  }

  /// Get the allocator for this container.
  allocator_type get_allocator() const noexcept { return table_.alloc(); }

  //// PUBLIC - Iterators

  /// @methodset Iterators
  iterator begin() noexcept { return cbegin(); }
  const_iterator begin() const noexcept { return cbegin(); }
  /// @methodset Iterators
  const_iterator cbegin() const noexcept {
    return table_.makeIter(table_.begin());
  }

  /// @methodset Iterators
  iterator end() noexcept { return cend(); }
  const_iterator end() const noexcept { return cend(); }
  /// @methodset Iterators
  const_iterator cend() const noexcept { return table_.makeIter(table_.end()); }

  //// PUBLIC - Capacity

  /**
   * Check if this container has any elements.
   * @methodset Capacity
   */
  bool empty() const noexcept { return table_.empty(); }

  /**
   * Number of elements in this container.
   * @methodset Capacity
   */
  std::size_t size() const noexcept { return table_.size(); }

  /**
   * The maximum size of this container.
   * @methodset Capacity
   */
  std::size_t max_size() const noexcept { return table_.max_size(); }

  //// PUBLIC - Modifiers

  /**
   * Remove all elements.
   *
   * Does not free heap-allocated memory; capacity is unchanged.
   *
   * @methodset Modifiers
   */
  void clear() noexcept { table_.clear(); }

  /**
   * Add a single element.
   *
   * @overloadbrief Add elements.
   * @methodset Modifiers
   */
  std::pair<iterator, bool> insert(value_type const& value) {
    return emplace(value);
  }

  /// Add a single element.
  std::pair<iterator, bool> insert(value_type&& value) {
    return emplace(std::move(value));
  }

  /**
   * Add a single element, with a locational hint.
   *
   * std::unordered_set's hinted insertion API is misleading.  No implementation
   * I've seen actually uses the hint.  Code restructuring by the caller to use
   * the hinted API is at best unnecessary, and at worst a pessimization.  It is
   * used, however, so we provide it.
   */
  iterator insert(const_iterator /*hint*/, value_type const& value) {
    return insert(value).first;
  }

  /// Add a single element, with a locational hint.
  iterator insert(const_iterator /*hint*/, value_type&& value) {
    return insert(std::move(value)).first;
  }

  /// Add a single element, of a heterognous type.
  template <typename K>
  EnableHeterogeneousInsert<K, std::pair<iterator, bool>> insert(K&& value) {
    return emplace(std::forward<K>(value));
  }

 private:
  template <class InputIt>
  FOLLY_ALWAYS_INLINE void bulkInsert(
      InputIt first, InputIt last, bool autoReserve) {
    if (autoReserve) {
      auto n = std::distance(first, last);
      if (n == 0) {
        return;
      }
      table_.reserveForInsert(n);
    }
    while (first != last) {
      insert(*first);
      ++first;
    }
  }

  template <class InputIt>
  void initialInsert(InputIt first, InputIt last, std::size_t initialCapacity) {
    FOLLY_SAFE_DCHECK(empty() && bucket_count() >= initialCapacity, "");

    // It's possible that there are a lot of duplicates in first..last and
    // so we will oversize ourself.  The common case, however, is that
    // we can avoid a lot of rehashing if we pre-expand.  The behavior
    // is easy to disable at a particular call site by asking for an
    // initialCapacity of 1.
    bool autoReserve =
        std::is_base_of<
            std::random_access_iterator_tag,
            typename std::iterator_traits<InputIt>::iterator_category>::value &&
        initialCapacity == 0;
    bulkInsert(first, last, autoReserve);
  }

 public:
  /// Add a range of elements.
  template <class InputIt>
  void insert(InputIt first, InputIt last) {
    // Bulk reserve is a heuristic choice, so it can backfire.  We restrict
    // ourself to situations that mimic bulk construction without an
    // explicit initialCapacity.
    bool autoReserve =
        std::is_base_of<
            std::random_access_iterator_tag,
            typename std::iterator_traits<InputIt>::iterator_category>::value &&
        bucket_count() == 0;
    bulkInsert(first, last, autoReserve);
  }

  /// Add elements from an initializer list.
  void insert(std::initializer_list<value_type> ilist) {
    insert(ilist.begin(), ilist.end());
  }

 private:
  template <typename Arg>
  using UsableAsKey = ::folly::detail::
      EligibleForHeterogeneousFind<key_type, hasher, key_equal, Arg>;

 public:
  /**
   * Add an element by constructing it in-place.
   *
   * @overloadbrief Add elements in-place.
   * @methodset Modifiers
   */
  template <class... Args>
  std::pair<iterator, bool> emplace(Args&&... args) {
    auto rv = folly::detail::callWithConstructedKey<key_type, UsableAsKey>(
        table_.alloc(),
        [&](auto&&... inner) {
          return table_.tryEmplaceValue(
              std::forward<decltype(inner)>(inner)...);
        },
        std::forward<Args>(args)...);
    return std::make_pair(table_.makeIter(rv.first), rv.second);
  }

  // Emplace with prehash token
  template <class... Args>
  std::pair<iterator, bool> emplace_token(
      F14HashToken const& token, Args&&... args) {
    auto rv = folly::detail::callWithConstructedKey<key_type, UsableAsKey>(
        table_.alloc(),
        [&](auto&&... inner) {
          return table_.tryEmplaceValueWithToken(
              token, std::forward<decltype(inner)>(inner)...);
        },
        std::forward<Args>(args)...);
    return std::make_pair(table_.makeIter(rv.first), rv.second);
  }

  /// Emplace with hint.
  template <class... Args>
  iterator emplace_hint(const_iterator /*hint*/, Args&&... args) {
    return emplace(std::forward<Args>(args)...).first;
  }

  /**
   * Remove element at a specific position (iterator).
   * @overloadbrief Remove elements.
   * @methodset Modifiers
   */
  FOLLY_ALWAYS_INLINE iterator erase(const_iterator pos) {
    return eraseInto(pos, [](value_type&&) {});
  }

  /// Remove a range of elements.
  iterator erase(const_iterator first, const_iterator last) {
    return eraseInto(first, last, [](value_type&&) {});
  }

  /// Remove a specific key.
  size_type erase(key_type const& key) {
    return eraseInto(key, [](value_type&&) {});
  }

  /// Remove a key, using a heterogeneous representation.
  template <typename K>
  EnableHeterogeneousErase<K, size_type> erase(K const& key) {
    return eraseInto(key, [](value_type&&) {});
  }

  /**
   * Callback-erase a single iterator.
   *
   * Like erase, but with an additional callback argument which is called with
   * an rvalue reference to the item directly before it is destroyed. This can
   * be used to extract an item out of a F14Set while avoiding a copy.
   *
   * @overloadbrief Erase with pre-destruction callback.
   * @methodset Modifiers
   */
  template <typename BeforeDestroy>
  FOLLY_ALWAYS_INLINE iterator
  eraseInto(const_iterator pos, BeforeDestroy&& beforeDestroy) {
    auto itemPos = table_.unwrapIter(pos);
    table_.eraseIterInto(itemPos, beforeDestroy);

    // If we are inlined then gcc and clang can optimize away all of the
    // work of ++pos if the caller discards it.
    itemPos.advanceLikelyDead();
    return table_.makeIter(itemPos);
  }

  /// Callback-erase a range of values.
  template <typename BeforeDestroy>
  iterator eraseInto(
      const_iterator first,
      const_iterator last,
      BeforeDestroy&& beforeDestroy) {
    while (first != last) {
      first = eraseInto(first, beforeDestroy);
    }
    return first;
  }

  /// Callback-erase a specific key.
  template <typename BeforeDestroy>
  size_type eraseInto(key_type const& key, BeforeDestroy&& beforeDestroy) {
    return table_.eraseKeyInto(key, beforeDestroy);
  }

  /// Callback-erase a specific key, using a heterogeneous representation.
  template <typename K, typename BeforeDestroy>
  EnableHeterogeneousErase<K, size_type> eraseInto(
      K const& key, BeforeDestroy&& beforeDestroy) {
    return table_.eraseKeyInto(key, beforeDestroy);
  }

  //// PUBLIC - Lookup

  /**
   * Number of elements matching the given key.
   * @methodset Lookup
   */
  FOLLY_ALWAYS_INLINE size_type count(key_type const& key) const {
    return contains(key) ? 1 : 0;
  }

  template <typename K>
  FOLLY_ALWAYS_INLINE EnableHeterogeneousFind<K, size_type> count(
      K const& key) const {
    return contains(key) ? 1 : 0;
  }

  /**
   * @overloadbrief Prehash a key.
   * @methodset Lookup
   *
   * prehash(key) does the work of evaluating hash_function()(key)
   * (including additional bit-mixing for non-avalanching hash functions),
   * and wraps the result of that work in a token for later reuse.
   *
   * The returned token may be used at any time, may be used more than
   * once, and may be used in other F14 sets and maps.  Tokens are
   * transferrable between any F14 containers (maps and sets) with the
   * same key_type and equal hash_function()s.
   *
   * Hash tokens are not hints -- it is a bug to call any method on this
   * class with a token t and key k where t isn't the result of a call
   * to prehash(k2) with k2 == k.
   */
  F14HashToken prehash(key_type const& key) const {
    return table_.prehash(key);
  }

  template <typename K>
  EnableHeterogeneousFind<K, F14HashToken> prehash(K const& key) const {
    return table_.prehash(key);
  }

  /**
   * @overloadbrief Prefetch cachelines associated with a key.
   * @methodset Lookup
   *
   * prefetch(token) begins prefetching the first steps of looking for key into
   * the local CPU cache.
   *
   * Example Scenario: Loading 2 values from a cold map.
   * You have a map that is cold, meaning it is out of the local CPU cache,
   * and you want to load two values from the map. This can be extended to
   * load N values, but we're loading 2 for simplicity.
   *
   * When the map is cold the dominating factor in the latency is loading the
   * cache line of the entry into the local CPU cache. Using prehash() will
   * issue these cache line fetches in parallel.  That means that by the time we
   * finish map.find(token1, key1) the cache lines needed by map.find(token2,
   * key2) may already be in the local CPU cache. In the best case this will
   * half the latency.
   *
   * It is always okay to call prefetch() before a find() or other lookup
   * operation, as it only prefetches cache lines that are guaranteed to be
   * needed by the lookup.
   *
   *   std::pair<iterator, iterator> find2(
   *       auto& set, key_type const& key1, key_type const& key2) {
   *     auto const token1 = set.prehash(key1);
   *     set.prefetch(token1);
   *     auto const token2 = set.prehash(key2);
   *     set.prefetch(token2);
   *     return std::make_pair(set.find(token1, key1), set.find(token2, key2));
   *   }
   */
  void prefetch(F14HashToken const& token) const { table_.prefetch(token); }

  /**
   * @overloadbrief Get the iterator for a key.
   * @methodset Lookup
   */
  FOLLY_ALWAYS_INLINE iterator find(key_type const& key) {
    return const_cast<F14BasicSet const*>(this)->find(key);
  }

  FOLLY_ALWAYS_INLINE const_iterator find(key_type const& key) const {
    return table_.makeIter(table_.find(key));
  }

  FOLLY_ALWAYS_INLINE iterator
  find(F14HashToken const& token, key_type const& key) {
    return const_cast<F14BasicSet const*>(this)->find(token, key);
  }

  FOLLY_ALWAYS_INLINE const_iterator
  find(F14HashToken const& token, key_type const& key) const {
    return table_.makeIter(table_.find(token, key));
  }

  template <typename K>
  FOLLY_ALWAYS_INLINE EnableHeterogeneousFind<K, iterator> find(K const& key) {
    return const_cast<F14BasicSet const*>(this)->find(key);
  }

  template <typename K>
  FOLLY_ALWAYS_INLINE EnableHeterogeneousFind<K, const_iterator> find(
      K const& key) const {
    return table_.makeIter(table_.find(key));
  }

  template <typename K>
  FOLLY_ALWAYS_INLINE EnableHeterogeneousFind<K, iterator> find(
      F14HashToken const& token, K const& key) {
    return const_cast<F14BasicSet const*>(this)->find(token, key);
  }

  template <typename K>
  FOLLY_ALWAYS_INLINE EnableHeterogeneousFind<K, const_iterator> find(
      F14HashToken const& token, K const& key) const {
    return table_.makeIter(table_.find(token, key));
  }

  /**
   * @overloadbrief Checks if the container contains an element with the
   * specific key.
   * @methodset Lookup
   */
  FOLLY_ALWAYS_INLINE bool contains(key_type const& key) const {
    return !table_.find(key).atEnd();
  }

  template <typename K>
  FOLLY_ALWAYS_INLINE EnableHeterogeneousFind<K, bool> contains(
      K const& key) const {
    return !table_.find(key).atEnd();
  }

  FOLLY_ALWAYS_INLINE bool contains(
      F14HashToken const& token, key_type const& key) const {
    return !table_.find(token, key).atEnd();
  }

  template <typename K>
  FOLLY_ALWAYS_INLINE EnableHeterogeneousFind<K, bool> contains(
      F14HashToken const& token, K const& key) const {
    return !table_.find(token, key).atEnd();
  }

  /**
   * @overloadbrief Returns the range of elements matching a specific key.
   * @methodset Lookup
   */
  std::pair<iterator, iterator> equal_range(key_type const& key) {
    return equal_range(*this, key);
  }

  std::pair<const_iterator, const_iterator> equal_range(
      key_type const& key) const {
    return equal_range(*this, key);
  }

  template <typename K>
  EnableHeterogeneousFind<K, std::pair<iterator, iterator>> equal_range(
      K const& key) {
    return equal_range(*this, key);
  }

  template <typename K>
  EnableHeterogeneousFind<K, std::pair<const_iterator, const_iterator>>
  equal_range(K const& key) const {
    return equal_range(*this, key);
  }

  //// PUBLIC - Bucket interface

  /**
   * The number of buckets in this container.
   * @methodset Bucket interface
   */
  std::size_t bucket_count() const noexcept { return table_.bucket_count(); }

  /**
   * The maximum number of buckets for this container.
   * @methodset Bucket interface
   */
  std::size_t max_bucket_count() const noexcept {
    return table_.max_bucket_count();
  }

  //// PUBLIC - Hash policy

  /**
   * Load factor of the underlying hashtable.
   * @methodset Hash policy
   */
  float load_factor() const noexcept { return table_.load_factor(); }

  /**
   * @overloadbrief Load factor control.
   * Get the maximum load factor for this container.
   * @methodset Hash policy
   */
  float max_load_factor() const noexcept { return table_.max_load_factor(); }

  /**
   * Set the maximum load factor for this container.
   * @methodset Hash policy
   */
  void max_load_factor(float v) { table_.max_load_factor(v); }

  /**
   * Rehash this container.
   *
   * This function is provided for compliance with C++'s requirements for
   * hashtables, but is no better than a simple `reserve` call for F14.
   *
   * @param bucketCapcity  The desired capacity across all buckets.
   *
   * @methodset Hash policy
   */
  void rehash(std::size_t bucketCapacity) {
    // The standard's rehash() requires understanding the max load factor,
    // which is easy to get wrong.  Since we don't actually allow adjustment
    // of max_load_factor there is no difference.
    reserve(bucketCapacity);
  }

  /**
   * Pre-allocate space for at least this many elements.
   *
   * @param capacity  The number of elements to pre-allocate space for.
   *
   * @methodset Capacity
   */
  void reserve(std::size_t capacity) { table_.reserve(capacity); }

  //// PUBLIC - Observers

  /**
   * Get the hasher.
   * @methodset Observers
   */
  hasher hash_function() const { return table_.hasher(); }

  /**
   * Get the key_equal.
   * @methodset Observers
   */
  key_equal key_eq() const { return table_.keyEqual(); }

  //// PUBLIC - F14 Extensions

  /**
   * Checks for a value using operator==
   *
   * returns true iff there is an element in the set
   * that compares equal to key using operator==.  It is undefined
   * behavior to call this function if operator== on key_type can ever
   * return true when the same keys passed to key_eq() would return false
   * (the opposite is allowed).  When using the default key_eq this function
   * is equivalent to contains().
   *
   * @methodset Lookup
   */
  bool containsEqualValue(value_type const& value) const {
    return !table_.findMatching(value, [&](auto& k) { return value == k; })
                .atEnd();
  }

  /**
   * Get memory footprint, not including sizeof(*this).
   * @methodset Capacity
   */
  std::size_t getAllocatedMemorySize() const {
    return table_.getAllocatedMemorySize();
  }

  /**
   * In-depth memory analysis.
   *
   * Enumerates classes of allocated memory blocks currently owned
   * by this table, calling visitor(allocationSize, allocationCount).
   * This can be used to get a more accurate indication of memory footprint
   * than getAllocatedMemorySize() if you have some way of computing the
   * internal fragmentation of the allocator, such as JEMalloc's nallocx.
   * The visitor might be called twice with the same allocationSize. The
   * visitor's computation should produce the same result for visitor(8,
   * 2) as for two calls to visitor(8, 1), for example.  The visitor may
   * be called with a zero allocationCount.
   *
   * @methodset Capacity
   */
  template <typename V>
  void visitAllocationClasses(V&& visitor) const {
    return table_.visitAllocationClasses(visitor);
  }

  /**
   * Visit contiguous ranges of elements.
   *
   * Calls visitor with two value_type const*, b and e, such that every
   * entry in the table is included in exactly one of the ranges [b,e).
   * This can be used to efficiently iterate elements in bulk when crossing
   * an API boundary that supports contiguous blocks of items.
   *
   * @methodset Iterators
   */
  template <typename V>
  void visitContiguousRanges(V&& visitor) const;

  /**
   * Get stats.
   * @methodset Hash policy
   */
  F14TableStats computeStats() const noexcept { return table_.computeStats(); }

 private:
  template <typename Self, typename K>
  static auto equal_range(Self& self, K const& key) {
    auto first = self.find(key);
    auto last = first;
    if (last != self.end()) {
      ++last;
    }
    return std::make_pair(first, last);
  }

 protected:
  F14Table<Policy> table_;
};
} // namespace detail
} // namespace f14

template <typename Key, typename Hasher, typename KeyEqual, typename Alloc>
class F14ValueSet
    : public f14::detail::F14BasicSet<f14::detail::SetPolicyWithDefaults<
          f14::detail::ValueContainerPolicy,
          Key,
          Hasher,
          KeyEqual,
          Alloc>> {
 protected:
  friend struct F14ValueSetTester;
  using Policy = f14::detail::SetPolicyWithDefaults<
      f14::detail::ValueContainerPolicy,
      Key,
      Hasher,
      KeyEqual,
      Alloc>;

 private:
  using Super = f14::detail::F14BasicSet<Policy>;

 public:
  using typename Super::value_type;

  F14ValueSet() = default;

  using Super::Super;

  F14ValueSet& operator=(std::initializer_list<value_type> ilist) {
    Super::operator=(ilist);
    return *this;
  }

  void swap(F14ValueSet& rhs) noexcept(Policy::kSwapIsNoexcept) {
    this->table_.swap(rhs.table_);
  }

  template <typename V>
  void visitContiguousRanges(V&& visitor) const {
    this->table_.visitContiguousItemRanges(std::forward<V>(visitor));
  }
};

#if FOLLY_HAS_DEDUCTION_GUIDES
template <
    typename InputIt,
    typename Hasher = f14::DefaultHasher<iterator_value_type_t<InputIt>>,
    typename KeyEqual = f14::DefaultKeyEqual<iterator_value_type_t<InputIt>>,
    typename Alloc = f14::DefaultAlloc<iterator_value_type_t<InputIt>>,
    typename = detail::RequireInputIterator<InputIt>,
    typename = detail::RequireNotAllocator<Hasher>,
    typename = detail::RequireNotAllocator<KeyEqual>,
    typename = detail::RequireAllocator<Alloc>>
F14ValueSet(
    InputIt, InputIt, std::size_t = {}, Hasher = {}, KeyEqual = {}, Alloc = {})
    -> F14ValueSet<iterator_value_type_t<InputIt>, Hasher, KeyEqual, Alloc>;

template <
    typename InputIt,
    typename Alloc,
    typename = detail::RequireInputIterator<InputIt>,
    typename = detail::RequireAllocator<Alloc>>
F14ValueSet(InputIt, InputIt, std::size_t, Alloc) -> F14ValueSet<
    iterator_value_type_t<InputIt>,
    f14::DefaultHasher<iterator_value_type_t<InputIt>>,
    f14::DefaultKeyEqual<iterator_value_type_t<InputIt>>,
    Alloc>;

template <
    typename InputIt,
    typename Hasher,
    typename Alloc,
    typename = detail::RequireInputIterator<InputIt>,
    typename = detail::RequireNotAllocator<Hasher>,
    typename = detail::RequireAllocator<Alloc>>
F14ValueSet(InputIt, InputIt, std::size_t, Hasher, Alloc) -> F14ValueSet<
    iterator_value_type_t<InputIt>,
    Hasher,
    f14::DefaultKeyEqual<iterator_value_type_t<InputIt>>,
    Alloc>;

template <
    typename Key,
    typename Hasher = f14::DefaultHasher<Key>,
    typename KeyEqual = f14::DefaultKeyEqual<Key>,
    typename Alloc = f14::DefaultAlloc<Key>,
    typename = detail::RequireNotAllocator<Hasher>,
    typename = detail::RequireNotAllocator<KeyEqual>,
    typename = detail::RequireAllocator<Alloc>>
F14ValueSet(
    std::initializer_list<Key>,
    std::size_t = {},
    Hasher = {},
    KeyEqual = {},
    Alloc = {}) -> F14ValueSet<Key, Hasher, KeyEqual, Alloc>;

template <
    typename Key,
    typename Alloc,
    typename = detail::RequireAllocator<Alloc>>
F14ValueSet(std::initializer_list<Key>, std::size_t, Alloc) -> F14ValueSet<
    Key,
    f14::DefaultHasher<Key>,
    f14::DefaultKeyEqual<Key>,
    Alloc>;

template <
    typename Key,
    typename Hasher,
    typename Alloc,
    typename = detail::RequireAllocator<Alloc>>
F14ValueSet(std::initializer_list<Key>, std::size_t, Hasher, Alloc)
    -> F14ValueSet<Key, Hasher, f14::DefaultKeyEqual<Key>, Alloc>;
#endif

template <typename Key, typename Hasher, typename KeyEqual, typename Alloc>
class F14NodeSet
    : public f14::detail::F14BasicSet<f14::detail::SetPolicyWithDefaults<
          f14::detail::NodeContainerPolicy,
          Key,
          Hasher,
          KeyEqual,
          Alloc>> {
 protected:
  using Policy = f14::detail::SetPolicyWithDefaults<
      f14::detail::NodeContainerPolicy,
      Key,
      Hasher,
      KeyEqual,
      Alloc>;

 private:
  using Super = f14::detail::F14BasicSet<Policy>;

 public:
  using typename Super::value_type;

  F14NodeSet() = default;

  using Super::Super;

  F14NodeSet& operator=(std::initializer_list<value_type> ilist) {
    Super::operator=(ilist);
    return *this;
  }

  void swap(F14NodeSet& rhs) noexcept(Policy::kSwapIsNoexcept) {
    this->table_.swap(rhs.table_);
  }

  template <typename V>
  void visitContiguousRanges(V&& visitor) const {
    this->table_.visitItems([&](typename Policy::Item ptr) {
      value_type const* b = std::addressof(*ptr);
      visitor(b, b + 1);
    });
  }
};

#if FOLLY_HAS_DEDUCTION_GUIDES
template <
    typename InputIt,
    typename Hasher = f14::DefaultHasher<iterator_value_type_t<InputIt>>,
    typename KeyEqual = f14::DefaultKeyEqual<iterator_value_type_t<InputIt>>,
    typename Alloc = f14::DefaultAlloc<iterator_value_type_t<InputIt>>,
    typename = detail::RequireInputIterator<InputIt>,
    typename = detail::RequireNotAllocator<Hasher>,
    typename = detail::RequireNotAllocator<KeyEqual>,
    typename = detail::RequireAllocator<Alloc>>
F14NodeSet(
    InputIt, InputIt, std::size_t = {}, Hasher = {}, KeyEqual = {}, Alloc = {})
    -> F14NodeSet<iterator_value_type_t<InputIt>, Hasher, KeyEqual, Alloc>;

template <
    typename InputIt,
    typename Alloc,
    typename = detail::RequireInputIterator<InputIt>,
    typename = detail::RequireAllocator<Alloc>>
F14NodeSet(InputIt, InputIt, std::size_t, Alloc) -> F14NodeSet<
    iterator_value_type_t<InputIt>,
    f14::DefaultHasher<iterator_value_type_t<InputIt>>,
    f14::DefaultKeyEqual<iterator_value_type_t<InputIt>>,
    Alloc>;

template <
    typename InputIt,
    typename Hasher,
    typename Alloc,
    typename = detail::RequireInputIterator<InputIt>,
    typename = detail::RequireNotAllocator<Hasher>,
    typename = detail::RequireAllocator<Alloc>>
F14NodeSet(InputIt, InputIt, std::size_t, Hasher, Alloc) -> F14NodeSet<
    iterator_value_type_t<InputIt>,
    Hasher,
    f14::DefaultKeyEqual<iterator_value_type_t<InputIt>>,
    Alloc>;

template <
    typename Key,
    typename Hasher = f14::DefaultHasher<Key>,
    typename KeyEqual = f14::DefaultKeyEqual<Key>,
    typename Alloc = f14::DefaultAlloc<Key>,
    typename = detail::RequireNotAllocator<Hasher>,
    typename = detail::RequireNotAllocator<KeyEqual>,
    typename = detail::RequireAllocator<Alloc>>
F14NodeSet(
    std::initializer_list<Key>,
    std::size_t = {},
    Hasher = {},
    KeyEqual = {},
    Alloc = {}) -> F14NodeSet<Key, Hasher, KeyEqual, Alloc>;

template <
    typename Key,
    typename Alloc,
    typename = detail::RequireAllocator<Alloc>>
F14NodeSet(std::initializer_list<Key>, std::size_t, Alloc) -> F14NodeSet<
    Key,
    f14::DefaultHasher<Key>,
    f14::DefaultKeyEqual<Key>,
    Alloc>;

template <
    typename Key,
    typename Hasher,
    typename Alloc,
    typename = detail::RequireAllocator<Alloc>>
F14NodeSet(std::initializer_list<Key>, std::size_t, Hasher, Alloc)
    -> F14NodeSet<Key, Hasher, f14::DefaultKeyEqual<Key>, Alloc>;
#endif

namespace f14 {
namespace detail {
template <
    typename Key,
    typename Hasher,
    typename KeyEqual,
    typename Alloc,
    typename EligibleForPerturbedInsertionOrder>
class F14VectorSetImpl : public F14BasicSet<SetPolicyWithDefaults<
                             VectorContainerPolicy,
                             Key,
                             Hasher,
                             KeyEqual,
                             Alloc,
                             EligibleForPerturbedInsertionOrder>> {
 protected:
  using Policy = SetPolicyWithDefaults<
      VectorContainerPolicy,
      Key,
      Hasher,
      KeyEqual,
      Alloc,
      EligibleForPerturbedInsertionOrder>;

 private:
  using Super = F14BasicSet<Policy>;

  template <typename K>
  using IsIter = Disjunction<
      std::is_same<typename Policy::Iter, remove_cvref_t<K>>,
      std::is_same<typename Policy::ReverseIter, remove_cvref_t<K>>>;

  template <typename K, typename T>
  using EnableHeterogeneousVectorErase = std::enable_if_t<
      ::folly::detail::EligibleForHeterogeneousFind<
          typename Policy::Value,
          typename Policy::Hasher,
          typename Policy::KeyEqual,
          std::conditional_t<IsIter<K>::value, typename Policy::Value, K>>::
              value &&
          !IsIter<K>::value,
      T>;

 public:
  using typename Super::const_iterator;
  using typename Super::iterator;
  using typename Super::key_type;
  using typename Super::value_type;

  F14VectorSetImpl() = default;

  using Super::Super;

  F14VectorSetImpl& operator=(std::initializer_list<value_type> ilist) {
    Super::operator=(ilist);
    return *this;
  }

  iterator begin() { return cbegin(); }
  const_iterator begin() const { return cbegin(); }
  const_iterator cbegin() const {
    return this->table_.linearBegin(this->size());
  }

  iterator end() { return cend(); }
  const_iterator end() const { return cend(); }
  const_iterator cend() const { return this->table_.linearEnd(); }

 private:
  template <typename BeforeDestroy>
  void eraseUnderlying(
      typename Policy::ItemIter underlying, BeforeDestroy&& beforeDestroy) {
    Alloc& a = this->table_.alloc();
    auto values = this->table_.values_;

    // destroy the value and remove the ptr from the base table
    auto index = underlying.item();
    this->table_.eraseIterInto(underlying, beforeDestroy);
    Policy::AllocTraits::destroy(a, std::addressof(values[index]));

    // move the last element in values_ down and fix up the inbound index
    auto tailIndex = this->size();
    if (tailIndex != index) {
      auto tail = this->table_.find(
          VectorContainerIndexSearch{static_cast<uint32_t>(tailIndex)});
      tail.item() = index;
      auto p = std::addressof(values[index]);
      assume(p != nullptr);
      this->table_.transfer(a, std::addressof(values[tailIndex]), p, 1);
    }
  }

  template <typename K, typename BeforeDestroy>
  std::size_t eraseUnderlyingKey(K const& key, BeforeDestroy&& beforeDestroy) {
    auto underlying = this->table_.find(key);
    if (underlying.atEnd()) {
      return 0;
    } else {
      eraseUnderlying(underlying, beforeDestroy);
      return 1;
    }
  }

 public:
  FOLLY_ALWAYS_INLINE iterator erase(const_iterator pos) {
    return eraseInto(pos, [](value_type&&) {});
  }

  iterator erase(const_iterator first, const_iterator last) {
    return eraseInto(first, last, [](value_type&&) {});
  }

  std::size_t erase(key_type const& key) {
    return eraseInto(key, [](value_type&&) {});
  }

  template <typename K>
  EnableHeterogeneousVectorErase<K, std::size_t> erase(K const& key) {
    return eraseInto(key, [](value_type&&) {});
  }

  template <typename BeforeDestroy>
  FOLLY_ALWAYS_INLINE iterator
  eraseInto(const_iterator pos, BeforeDestroy&& beforeDestroy) {
    auto underlying = this->table_.find(
        VectorContainerIndexSearch{this->table_.iterToIndex(pos)});
    eraseUnderlying(underlying, beforeDestroy);
    return ++pos;
  }

  template <typename BeforeDestroy>
  iterator eraseInto(
      const_iterator first,
      const_iterator last,
      BeforeDestroy&& beforeDestroy) {
    while (first != last) {
      first = eraseInto(first, beforeDestroy);
    }
    return first;
  }

  template <typename BeforeDestroy>
  std::size_t eraseInto(key_type const& key, BeforeDestroy&& beforeDestroy) {
    return eraseUnderlyingKey(key, beforeDestroy);
  }

  template <typename K, typename BeforeDestroy>
  EnableHeterogeneousVectorErase<K, std::size_t> eraseInto(
      K const& key, BeforeDestroy&& beforeDestroy) {
    return eraseUnderlyingKey(key, beforeDestroy);
  }

  template <typename V>
  void visitContiguousRanges(V&& visitor) const {
    auto n = this->table_.size();
    if (n > 0) {
      value_type const* b = std::addressof(this->table_.values_[0]);
      visitor(b, b + n);
    }
  }
};
} // namespace detail
} // namespace f14

template <typename Key, typename Hasher, typename KeyEqual, typename Alloc>
class F14VectorSet
    : public f14::detail::
          F14VectorSetImpl<Key, Hasher, KeyEqual, Alloc, std::false_type> {
  using Super = f14::detail::
      F14VectorSetImpl<Key, Hasher, KeyEqual, Alloc, std::false_type>;

 public:
  using typename Super::const_iterator;
  using typename Super::iterator;
  using typename Super::value_type;
  using reverse_iterator = typename Super::Policy::ReverseIter;
  using const_reverse_iterator = reverse_iterator;

  F14VectorSet() = default;

  using Super::Super;

  F14VectorSet& operator=(std::initializer_list<value_type> ilist) {
    Super::operator=(ilist);
    return *this;
  }

  void swap(F14VectorSet& rhs) noexcept(Super::Policy::kSwapIsNoexcept) {
    this->table_.swap(rhs.table_);
  }

  // ITERATION ORDER
  //
  // Deterministic iteration order for insert-only workloads is part of
  // F14VectorSet's supported API: iterator is LIFO and reverse_iterator
  // is FIFO.
  //
  // If there have been no calls to erase() then iterator and
  // const_iterator enumerate entries in the opposite of insertion order.
  // begin()->first is the key most recently inserted.  reverse_iterator
  // and reverse_const_iterator, therefore, enumerate in LIFO (insertion)
  // order for insert-only workloads.  Deterministic iteration order is
  // only guaranteed if no keys were removed since the last time the
  // set was empty.  Iteration order is preserved across rehashes and
  // F14VectorSet copies and moves.
  //
  // iterator uses LIFO order so that erasing while iterating with begin()
  // and end() is safe using the erase(it++) idiom, which is supported
  // by std::set and std::unordered_set.  erase(iter) invalidates iter
  // and all iterators before iter in the non-reverse iteration order.
  // Every successful erase invalidates all reverse iterators.
  //
  // No erase is provided for reverse_iterator (AKA const_reverse_iterator)
  // to make it harder to shoot yourself in the foot by erasing while
  // reverse-iterating.  You can write that as set.erase(set.iter(riter))
  // if you need it.

  reverse_iterator rbegin() { return this->table_.values_; }
  const_reverse_iterator rbegin() const { return crbegin(); }
  const_reverse_iterator crbegin() const { return this->table_.values_; }

  reverse_iterator rend() { return this->table_.values_ + this->table_.size(); }
  const_reverse_iterator rend() const { return crend(); }
  const_reverse_iterator crend() const {
    return this->table_.values_ + this->table_.size();
  }

  // explicit conversions between iterator and reverse_iterator
  iterator iter(reverse_iterator riter) { return this->table_.iter(riter); }
  const_iterator iter(const_reverse_iterator riter) const {
    return this->table_.iter(riter);
  }

  reverse_iterator riter(iterator it) { return this->table_.riter(it); }
  const_reverse_iterator riter(const_iterator it) const {
    return this->table_.riter(it);
  }

  friend Range<const_reverse_iterator> tag_invoke(
      order_preserving_reinsertion_view_fn, F14VectorSet const& c) noexcept {
    return {c.rbegin(), c.rend()};
  }
};

#if FOLLY_HAS_DEDUCTION_GUIDES
template <
    typename InputIt,
    typename Hasher = f14::DefaultHasher<iterator_value_type_t<InputIt>>,
    typename KeyEqual = f14::DefaultKeyEqual<iterator_value_type_t<InputIt>>,
    typename Alloc = f14::DefaultAlloc<iterator_value_type_t<InputIt>>,
    typename = detail::RequireInputIterator<InputIt>,
    typename = detail::RequireNotAllocator<Hasher>,
    typename = detail::RequireNotAllocator<KeyEqual>,
    typename = detail::RequireAllocator<Alloc>>
F14VectorSet(
    InputIt, InputIt, std::size_t = {}, Hasher = {}, KeyEqual = {}, Alloc = {})
    -> F14VectorSet<iterator_value_type_t<InputIt>, Hasher, KeyEqual, Alloc>;

template <
    typename InputIt,
    typename Alloc,
    typename = detail::RequireInputIterator<InputIt>,
    typename = detail::RequireAllocator<Alloc>>
F14VectorSet(InputIt, InputIt, std::size_t, Alloc) -> F14VectorSet<
    iterator_value_type_t<InputIt>,
    f14::DefaultHasher<iterator_value_type_t<InputIt>>,
    f14::DefaultKeyEqual<iterator_value_type_t<InputIt>>,
    Alloc>;

template <
    typename InputIt,
    typename Hasher,
    typename Alloc,
    typename = detail::RequireInputIterator<InputIt>,
    typename = detail::RequireNotAllocator<Hasher>,
    typename = detail::RequireAllocator<Alloc>>
F14VectorSet(InputIt, InputIt, std::size_t, Hasher, Alloc) -> F14VectorSet<
    iterator_value_type_t<InputIt>,
    Hasher,
    f14::DefaultKeyEqual<iterator_value_type_t<InputIt>>,
    Alloc>;

template <
    typename Key,
    typename Hasher = f14::DefaultHasher<Key>,
    typename KeyEqual = f14::DefaultKeyEqual<Key>,
    typename Alloc = f14::DefaultAlloc<Key>,
    typename = detail::RequireNotAllocator<Hasher>,
    typename = detail::RequireNotAllocator<KeyEqual>,
    typename = detail::RequireAllocator<Alloc>>
F14VectorSet(
    std::initializer_list<Key>,
    std::size_t = {},
    Hasher = {},
    KeyEqual = {},
    Alloc = {}) -> F14VectorSet<Key, Hasher, KeyEqual, Alloc>;

template <
    typename Key,
    typename Alloc,
    typename = detail::RequireAllocator<Alloc>>
F14VectorSet(std::initializer_list<Key>, std::size_t, Alloc) -> F14VectorSet<
    Key,
    f14::DefaultHasher<Key>,
    f14::DefaultKeyEqual<Key>,
    Alloc>;

template <
    typename Key,
    typename Hasher,
    typename Alloc,
    typename = detail::RequireAllocator<Alloc>>
F14VectorSet(std::initializer_list<Key>, std::size_t, Hasher, Alloc)
    -> F14VectorSet<Key, Hasher, f14::DefaultKeyEqual<Key>, Alloc>;
#endif

template <typename Key, typename Hasher, typename KeyEqual, typename Alloc>
class F14FastSet
    : public std::conditional_t<
          sizeof(Key) < 24,
          F14ValueSet<Key, Hasher, KeyEqual, Alloc>,
          f14::detail::
              F14VectorSetImpl<Key, Hasher, KeyEqual, Alloc, std::true_type>> {
  using Super = std::conditional_t<
      sizeof(Key) < 24,
      F14ValueSet<Key, Hasher, KeyEqual, Alloc>,
      f14::detail::
          F14VectorSetImpl<Key, Hasher, KeyEqual, Alloc, std::true_type>>;

 public:
  using typename Super::value_type;

  F14FastSet() = default;

  using Super::Super;

  F14FastSet& operator=(std::initializer_list<value_type> ilist) {
    Super::operator=(ilist);
    return *this;
  }

  void swap(F14FastSet& rhs) noexcept(Super::Policy::kSwapIsNoexcept) {
    this->table_.swap(rhs.table_);
  }
};

#if FOLLY_HAS_DEDUCTION_GUIDES
template <
    typename InputIt,
    typename Hasher = f14::DefaultHasher<iterator_value_type_t<InputIt>>,
    typename KeyEqual = f14::DefaultKeyEqual<iterator_value_type_t<InputIt>>,
    typename Alloc = f14::DefaultAlloc<iterator_value_type_t<InputIt>>,
    typename = detail::RequireInputIterator<InputIt>,
    typename = detail::RequireNotAllocator<Hasher>,
    typename = detail::RequireNotAllocator<KeyEqual>,
    typename = detail::RequireAllocator<Alloc>>
F14FastSet(
    InputIt, InputIt, std::size_t = {}, Hasher = {}, KeyEqual = {}, Alloc = {})
    -> F14FastSet<iterator_value_type_t<InputIt>, Hasher, KeyEqual, Alloc>;

template <
    typename InputIt,
    typename Alloc,
    typename = detail::RequireInputIterator<InputIt>,
    typename = detail::RequireAllocator<Alloc>>
F14FastSet(InputIt, InputIt, std::size_t, Alloc) -> F14FastSet<
    iterator_value_type_t<InputIt>,
    f14::DefaultHasher<iterator_value_type_t<InputIt>>,
    f14::DefaultKeyEqual<iterator_value_type_t<InputIt>>,
    Alloc>;

template <
    typename InputIt,
    typename Hasher,
    typename Alloc,
    typename = detail::RequireInputIterator<InputIt>,
    typename = detail::RequireNotAllocator<Hasher>,
    typename = detail::RequireAllocator<Alloc>>
F14FastSet(InputIt, InputIt, std::size_t, Hasher, Alloc) -> F14FastSet<
    iterator_value_type_t<InputIt>,
    Hasher,
    f14::DefaultKeyEqual<iterator_value_type_t<InputIt>>,
    Alloc>;

template <
    typename Key,
    typename Hasher = f14::DefaultHasher<Key>,
    typename KeyEqual = f14::DefaultKeyEqual<Key>,
    typename Alloc = f14::DefaultAlloc<Key>,
    typename = detail::RequireNotAllocator<Hasher>,
    typename = detail::RequireNotAllocator<KeyEqual>,
    typename = detail::RequireAllocator<Alloc>>
F14FastSet(
    std::initializer_list<Key>,
    std::size_t = {},
    Hasher = {},
    KeyEqual = {},
    Alloc = {}) -> F14FastSet<Key, Hasher, KeyEqual, Alloc>;

template <
    typename Key,
    typename Alloc,
    typename = detail::RequireAllocator<Alloc>>
F14FastSet(std::initializer_list<Key>, std::size_t, Alloc) -> F14FastSet<
    Key,
    f14::DefaultHasher<Key>,
    f14::DefaultKeyEqual<Key>,
    Alloc>;

template <
    typename Key,
    typename Hasher,
    typename Alloc,
    typename = detail::RequireAllocator<Alloc>>
F14FastSet(std::initializer_list<Key>, std::size_t, Hasher, Alloc)
    -> F14FastSet<Key, Hasher, f14::DefaultKeyEqual<Key>, Alloc>;
#endif

} // namespace folly

#endif // if FOLLY_F14_VECTOR_INTRINSICS_AVAILABLE

//////// Compatibility for unsupported platforms (not x86_64 and not aarch64)
#include <folly/container/detail/F14SetFallback.h>

namespace folly {
namespace f14 {
namespace detail {
template <typename S>
bool setsEqual(S const& lhs, S const& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (auto& k : lhs) {
    if (!rhs.containsEqualValue(k)) {
      return false;
    }
  }
  return true;
}
} // namespace detail
} // namespace f14

template <typename K, typename H, typename E, typename A>
bool operator==(
    F14ValueSet<K, H, E, A> const& lhs, F14ValueSet<K, H, E, A> const& rhs) {
  return setsEqual(lhs, rhs);
}

template <typename K, typename H, typename E, typename A>
bool operator!=(
    F14ValueSet<K, H, E, A> const& lhs, F14ValueSet<K, H, E, A> const& rhs) {
  return !(lhs == rhs);
}

template <typename K, typename H, typename E, typename A>
bool operator==(
    F14NodeSet<K, H, E, A> const& lhs, F14NodeSet<K, H, E, A> const& rhs) {
  return setsEqual(lhs, rhs);
}

template <typename K, typename H, typename E, typename A>
bool operator!=(
    F14NodeSet<K, H, E, A> const& lhs, F14NodeSet<K, H, E, A> const& rhs) {
  return !(lhs == rhs);
}

template <typename K, typename H, typename E, typename A>
bool operator==(
    F14VectorSet<K, H, E, A> const& lhs, F14VectorSet<K, H, E, A> const& rhs) {
  return setsEqual(lhs, rhs);
}

template <typename K, typename H, typename E, typename A>
bool operator!=(
    F14VectorSet<K, H, E, A> const& lhs, F14VectorSet<K, H, E, A> const& rhs) {
  return !(lhs == rhs);
}

template <typename K, typename H, typename E, typename A>
bool operator==(
    F14FastSet<K, H, E, A> const& lhs, F14FastSet<K, H, E, A> const& rhs) {
  return setsEqual(lhs, rhs);
}

template <typename K, typename H, typename E, typename A>
bool operator!=(
    F14FastSet<K, H, E, A> const& lhs, F14FastSet<K, H, E, A> const& rhs) {
  return !(lhs == rhs);
}

template <typename K, typename H, typename E, typename A>
void swap(F14ValueSet<K, H, E, A>& lhs, F14ValueSet<K, H, E, A>& rhs) noexcept(
    noexcept(lhs.swap(rhs))) {
  lhs.swap(rhs);
}

template <typename K, typename H, typename E, typename A>
void swap(F14NodeSet<K, H, E, A>& lhs, F14NodeSet<K, H, E, A>& rhs) noexcept(
    noexcept(lhs.swap(rhs))) {
  lhs.swap(rhs);
}

template <typename K, typename H, typename E, typename A>
void swap(
    F14VectorSet<K, H, E, A>& lhs,
    F14VectorSet<K, H, E, A>& rhs) noexcept(noexcept(lhs.swap(rhs))) {
  lhs.swap(rhs);
}

template <typename K, typename H, typename E, typename A>
void swap(F14FastSet<K, H, E, A>& lhs, F14FastSet<K, H, E, A>& rhs) noexcept(
    noexcept(lhs.swap(rhs))) {
  lhs.swap(rhs);
}

template <typename K, typename H, typename E, typename A, typename Pred>
std::size_t erase_if(F14ValueSet<K, H, E, A>& c, Pred pred) {
  return f14::detail::erase_if_impl(c, pred);
}

template <typename K, typename H, typename E, typename A, typename Pred>
std::size_t erase_if(F14NodeSet<K, H, E, A>& c, Pred pred) {
  return f14::detail::erase_if_impl(c, pred);
}

template <typename K, typename H, typename E, typename A, typename Pred>
std::size_t erase_if(F14VectorSet<K, H, E, A>& c, Pred pred) {
  return f14::detail::erase_if_impl(c, pred);
}

template <typename K, typename H, typename E, typename A, typename Pred>
std::size_t erase_if(F14FastSet<K, H, E, A>& c, Pred pred) {
  return f14::detail::erase_if_impl(c, pred);
}

} // namespace folly
