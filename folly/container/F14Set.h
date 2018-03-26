/*
 * Copyright 2017-present Facebook, Inc.
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

#pragma once

/**
 * F14NodeSet, F14ValueSet, and F14VectorSet
 *
 * F14FastSet conditionally inherits from F14ValueSet or F14VectorSet
 *
 * See F14.md
 *
 * @author Nathan Bronson <ngbronson@fb.com>
 * @author Xiao Shi       <xshi@fb.com>
 */

#include <folly/lang/SafeAssert.h>

#include <folly/container/F14Set-pre.h>
#include <folly/container/detail/F14Policy.h>
#include <folly/container/detail/F14Table.h>

#if !FOLLY_F14_VECTOR_INTRINSICS_AVAILABLE

#include <unordered_set>

namespace folly {

template <typename K, typename H, typename E, typename A>
class F14NodeSet : public std::unordered_set<K, H, E, A> {
  using Super = std::unordered_set<K, H, E, A>;

 public:
  using Super::Super;
  F14NodeSet() : Super() {}
};

template <typename K, typename H, typename E, typename A>
class F14ValueSet : public std::unordered_set<K, H, E, A> {
  using Super = std::unordered_set<K, H, E, A>;

 public:
  using Super::Super;
  F14ValueSet() : Super() {}
};

template <typename K, typename H, typename E, typename A>
class F14VectorSet : public std::unordered_set<K, H, E, A> {
  using Super = std::unordered_set<K, H, E, A>;

 public:
  using Super::Super;
  F14VectorSet() : Super() {}
};

} // namespace folly

#else // FOLLY_F14_VECTOR_INTRINSICS_AVAILABLE

namespace folly {
namespace f14 {
namespace detail {

template <typename Policy>
class F14BasicSet {
  template <
      typename K,
      typename T,
      typename H = typename Policy::Hasher,
      typename E = typename Policy::KeyEqual>
  using IfIsTransparent = folly::_t<EnableIfIsTransparent<void, H, E, K, T>>;

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
  using pointer = typename std::allocator_traits<allocator_type>::pointer;
  using const_pointer =
      typename std::allocator_traits<allocator_type>::const_pointer;
  using iterator = typename Policy::Iter;
  using const_iterator = iterator;

  //// PUBLIC - Member functions

  F14BasicSet() noexcept(F14Table<Policy>::kDefaultConstructIsNoexcept)
      : F14BasicSet(0) {}

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

  explicit F14BasicSet(allocator_type const& alloc) : F14BasicSet(0, alloc) {}

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
      F14Table<Policy>::kAllocIsAlwaysEqual)
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

  allocator_type get_allocator() const noexcept {
    return table_.alloc();
  }

  //// PUBLIC - Iterators

  iterator begin() noexcept {
    return cbegin();
  }
  const_iterator begin() const noexcept {
    return cbegin();
  }
  const_iterator cbegin() const noexcept {
    return table_.makeIter(table_.begin());
  }

  iterator end() noexcept {
    return cend();
  }
  const_iterator end() const noexcept {
    return cend();
  }
  const_iterator cend() const noexcept {
    return table_.makeIter(table_.end());
  }

  //// PUBLIC - Capacity

  bool empty() const noexcept {
    return table_.empty();
  }

  std::size_t size() const noexcept {
    return table_.size();
  }

  std::size_t max_size() const noexcept {
    return table_.max_size();
  }

  F14TableStats computeStats() const {
    return table_.computeStats();
  }

  //// PUBLIC - Modifiers

  void clear() noexcept {
    table_.clear();
  }

  std::pair<iterator, bool> insert(value_type const& value) {
    auto rv = table_.tryEmplaceValue(value, value);
    return std::make_pair(table_.makeIter(rv.first), rv.second);
  }

  std::pair<iterator, bool> insert(value_type&& value) {
    // tryEmplaceValue guarantees not to touch the first arg after touching
    // any others, so although this looks fishy it is okay
    value_type const& searchKey = value;
    auto rv = table_.tryEmplaceValue(searchKey, std::move(value));
    return std::make_pair(table_.makeIter(rv.first), rv.second);
  }

  // std::unordered_set's hinted insertion API is misleading.  No
  // implementation I've seen actually uses the hint.  Code restructuring
  // by the caller to use the hinted API is at best unnecessary, and at
  // worst a pessimization.  It is used, however, so we provide it.

  iterator insert(const_iterator /*hint*/, value_type const& value) {
    return insert(value).first;
  }

  iterator insert(const_iterator /*hint*/, value_type&& value) {
    return insert(std::move(value)).first;
  }

 private:
  template <class InputIt>
  FOLLY_ALWAYS_INLINE void
  bulkInsert(InputIt first, InputIt last, bool autoReserve) {
    if (autoReserve) {
      table_.reserveForInsert(std::distance(first, last));
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
        std::is_same<
            typename std::iterator_traits<InputIt>::iterator_category,
            std::random_access_iterator_tag>::value &&
        initialCapacity == 0;
    bulkInsert(first, last, autoReserve);
  }

 public:
  template <class InputIt>
  void insert(InputIt first, InputIt last) {
    // Bulk reserve is a heuristic choice, so it can backfire.  We restrict
    // ourself to situations that mimic bulk construction without an
    // explicit initialCapacity.
    bool autoReserve =
        std::is_same<
            typename std::iterator_traits<InputIt>::iterator_category,
            std::random_access_iterator_tag>::value &&
        bucket_count() == 0;
    bulkInsert(first, last, autoReserve);
  }

  void insert(std::initializer_list<value_type> ilist) {
    insert(ilist.begin(), ilist.end());
  }

  // node API doesn't make sense for value set, which stores values inline

  // emplace won't actually be more efficient than insert until we
  // add heterogeneous lookup, but it is still useful now from a code
  // compactness standpoint.
  template <class... Args>
  std::pair<iterator, bool> emplace(Args&&... args) {
    key_type key(std::forward<Args>(args)...);
    return insert(std::move(key));
  }

  template <class... Args>
  iterator emplace_hint(const_iterator /*hint*/, Args&&... args) {
    return emplace(std::forward<Args>(args)...).first;
  }

  FOLLY_ALWAYS_INLINE iterator erase(const_iterator pos) {
    // If we are inlined then gcc and clang can optimize away all of the
    // work of ++pos if the caller discards it.
    table_.erase(table_.unwrapIter(pos));
    return ++pos;
  }

  iterator erase(const_iterator first, const_iterator last) {
    while (first != last) {
      table_.erase(table_.unwrapIter(first));
      ++first;
    }
    return first;
  }

  size_type erase(key_type const& key) {
    return table_.erase(key);
  }

  //// PUBLIC - Lookup

  FOLLY_ALWAYS_INLINE std::size_t count(key_type const& key) const {
    return table_.find(key).atEnd() ? 0 : 1;
  }

  template <typename K>
  FOLLY_ALWAYS_INLINE IfIsTransparent<K, size_type> count(K const& key) const {
    return table_.find(key).atEnd() ? 0 : 1;
  }

  F14HashToken prehash(key_type const& key) const {
    return table_.prehash(key);
  }

  template <typename K>
  IfIsTransparent<K, F14HashToken> prehash(K const& key) const {
    return table_.prehash(key);
  }

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
  FOLLY_ALWAYS_INLINE IfIsTransparent<K, iterator> find(K const& key) {
    return const_cast<F14BasicSet const*>(this)->find(key);
  }

  template <typename K>
  FOLLY_ALWAYS_INLINE IfIsTransparent<K, const_iterator> find(
      K const& key) const {
    return table_.makeIter(table_.find(key));
  }

  template <typename K>
  FOLLY_ALWAYS_INLINE IfIsTransparent<K, iterator> find(
      F14HashToken const& token,
      K const& key) {
    return const_cast<F14BasicSet const*>(this)->find(token, key);
  }

  template <typename K>
  FOLLY_ALWAYS_INLINE IfIsTransparent<K, const_iterator> find(
      F14HashToken const& token,
      K const& key) const {
    return table_.makeIter(table_.find(token, key));
  }

  std::pair<iterator, iterator> equal_range(key_type const& key) {
    return equal_range(*this, key);
  }

  std::pair<const_iterator, const_iterator> equal_range(
      key_type const& key) const {
    return equal_range(*this, key);
  }

  template <typename K>
  IfIsTransparent<K, std::pair<iterator, iterator>> equal_range(K const& key) {
    return equal_range(*this, key);
  }

  template <typename K>
  IfIsTransparent<K, std::pair<const_iterator, const_iterator>> equal_range(
      K const& key) const {
    return equal_range(*this, key);
  }

  //// PUBLIC - Bucket interface

  std::size_t bucket_count() const noexcept {
    return table_.bucket_count();
  }

  std::size_t max_bucket_count() const noexcept {
    return table_.max_bucket_count();
  }

  //// PUBLIC - Hash policy

  float load_factor() const noexcept {
    return table_.load_factor();
  }

  float max_load_factor() const noexcept {
    return table_.max_load_factor();
  }

  void max_load_factor(float v) {
    table_.max_load_factor(v);
  }

  void rehash(std::size_t bucketCapacity) {
    // The standard's rehash() requires understanding the max load factor,
    // which is easy to get wrong.  Since we don't actually allow adjustment
    // of max_load_factor there is no difference.
    reserve(bucketCapacity);
  }

  void reserve(std::size_t capacity) {
    table_.reserve(capacity);
  }

  //// PUBLIC - Observers

  hasher hash_function() const {
    return table_.hasher();
  }

  key_equal key_eq() const {
    return table_.keyEqual();
  }

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

template <typename S>
bool setsEqual(S const& lhs, S const& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (auto& k : lhs) {
    auto iter = rhs.find(k);
    if (iter == rhs.end()) {
      return false;
    }
    if (!std::is_same<
            typename S::key_equal,
            std::equal_to<typename S::value_type>>::value) {
      // spec says we compare key with == as well as with key_eq()
      if (!(k == *iter)) {
        return false;
      }
    }
  }
  return true;
}
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
  using Policy = f14::detail::SetPolicyWithDefaults<
      f14::detail::ValueContainerPolicy,
      Key,
      Hasher,
      KeyEqual,
      Alloc>;
  using Super = f14::detail::F14BasicSet<Policy>;

 public:
  F14ValueSet() noexcept(
      f14::detail::F14Table<Policy>::kDefaultConstructIsNoexcept)
      : Super{} {}

  using Super::Super;

  void swap(F14ValueSet& rhs) noexcept(
      f14::detail::F14Table<Policy>::kSwapIsNoexcept) {
    this->table_.swap(rhs.table_);
  }
};

template <typename K, typename H, typename E, typename A>
bool operator==(
    F14ValueSet<K, H, E, A> const& lhs,
    F14ValueSet<K, H, E, A> const& rhs) {
  return setsEqual(lhs, rhs);
}

template <typename K, typename H, typename E, typename A>
bool operator!=(
    F14ValueSet<K, H, E, A> const& lhs,
    F14ValueSet<K, H, E, A> const& rhs) {
  return !(lhs == rhs);
}

template <typename Key, typename Hasher, typename KeyEqual, typename Alloc>
class F14NodeSet
    : public f14::detail::F14BasicSet<f14::detail::SetPolicyWithDefaults<
          f14::detail::NodeContainerPolicy,
          Key,
          Hasher,
          KeyEqual,
          Alloc>> {
  using Policy = f14::detail::SetPolicyWithDefaults<
      f14::detail::NodeContainerPolicy,
      Key,
      Hasher,
      KeyEqual,
      Alloc>;
  using Super = f14::detail::F14BasicSet<Policy>;

 public:
  F14NodeSet() noexcept(
      f14::detail::F14Table<Policy>::kDefaultConstructIsNoexcept)
      : Super{} {}

  using Super::Super;

  void swap(F14NodeSet& rhs) noexcept(
      f14::detail::F14Table<Policy>::kSwapIsNoexcept) {
    this->table_.swap(rhs.table_);
  }
};

template <typename K, typename H, typename E, typename A>
bool operator==(
    F14NodeSet<K, H, E, A> const& lhs,
    F14NodeSet<K, H, E, A> const& rhs) {
  return setsEqual(lhs, rhs);
}

template <typename K, typename H, typename E, typename A>
bool operator!=(
    F14NodeSet<K, H, E, A> const& lhs,
    F14NodeSet<K, H, E, A> const& rhs) {
  return !(lhs == rhs);
}

template <typename Key, typename Hasher, typename KeyEqual, typename Alloc>
class F14VectorSet
    : public f14::detail::F14BasicSet<f14::detail::SetPolicyWithDefaults<
          f14::detail::VectorContainerPolicy,
          Key,
          Hasher,
          KeyEqual,
          Alloc>> {
  using Policy = f14::detail::SetPolicyWithDefaults<
      f14::detail::VectorContainerPolicy,
      Key,
      Hasher,
      KeyEqual,
      Alloc>;
  using Super = f14::detail::F14BasicSet<Policy>;

 public:
  using typename Super::const_iterator;
  using typename Super::iterator;
  using typename Super::key_type;
  using reverse_iterator = typename Policy::ReverseIter;
  using const_reverse_iterator = typename Policy::ConstReverseIter;

  F14VectorSet() noexcept(
      f14::detail::F14Table<Policy>::kDefaultConstructIsNoexcept)
      : Super{} {}

  // inherit constructors
  using Super::Super;

  void swap(F14VectorSet& rhs) noexcept(
      f14::detail::F14Table<Policy>::kSwapIsNoexcept) {
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

  iterator begin() {
    return cbegin();
  }
  const_iterator begin() const {
    return cbegin();
  }
  const_iterator cbegin() const {
    return this->table_.linearBegin(this->size());
  }

  iterator end() {
    return cend();
  }
  const_iterator end() const {
    return cend();
  }
  const_iterator cend() const {
    return this->table_.linearEnd();
  }

  reverse_iterator rbegin() {
    return this->table_.values_;
  }
  const_reverse_iterator rbegin() const {
    return crbegin();
  }
  const_reverse_iterator crbegin() const {
    return this->table_.values_;
  }

  reverse_iterator rend() {
    return this->table_.values_ + this->table_.size();
  }
  const_reverse_iterator rend() const {
    return crend();
  }
  const_reverse_iterator crend() const {
    return this->table_.values_ + this->table_.size();
  }

  // explicit conversions between iterator and reverse_iterator
  iterator iter(reverse_iterator riter) {
    return this->table_.iter(riter);
  }
  const_iterator iter(const_reverse_iterator riter) const {
    return this->table_.iter(riter);
  }

  reverse_iterator riter(iterator it) {
    return this->table_.riter(it);
  }
  const_reverse_iterator riter(const_iterator it) const {
    return this->table_.riter(it);
  }

 private:
  void eraseUnderlying(typename Policy::ItemIter underlying) {
    Alloc& a = this->table_.alloc();
    auto values = this->table_.values_;

    // destroy the value and remove the ptr from the base table
    auto index = underlying.item();
    std::allocator_traits<Alloc>::destroy(a, std::addressof(values[index]));
    this->table_.erase(underlying);

    // move the last element in values_ down and fix up the inbound index
    auto tailIndex = this->size();
    if (tailIndex != index) {
      auto tail = this->table_.find(f14::detail::VectorContainerIndexSearch{
          static_cast<uint32_t>(tailIndex)});
      tail.item() = index;
      auto p = std::addressof(values[index]);
      folly::assume(p != nullptr);
      this->table_.transfer(a, std::addressof(values[tailIndex]), p, 1);
    }
  }

 public:
  FOLLY_ALWAYS_INLINE iterator erase(const_iterator pos) {
    auto underlying = this->table_.find(
        f14::detail::VectorContainerIndexSearch{this->table_.iterToIndex(pos)});
    eraseUnderlying(underlying);
    return ++pos;
  }

  iterator erase(const_iterator first, const_iterator last) {
    while (first != last) {
      first = erase(first);
    }
    return first;
  }

  std::size_t erase(key_type const& key) {
    auto underlying = this->table_.find(key);
    if (underlying.atEnd()) {
      return 0;
    } else {
      eraseUnderlying(underlying);
      return 1;
    }
  }
};

template <typename K, typename H, typename E, typename A>
bool operator==(
    F14VectorSet<K, H, E, A> const& lhs,
    F14VectorSet<K, H, E, A> const& rhs) {
  return setsEqual(lhs, rhs);
}

template <typename K, typename H, typename E, typename A>
bool operator!=(
    F14VectorSet<K, H, E, A> const& lhs,
    F14VectorSet<K, H, E, A> const& rhs) {
  return !(lhs == rhs);
}
} // namespace folly

#endif // FOLLY_F14_VECTOR_INTRINSICS_AVAILABLE

namespace folly {
template <typename Key, typename Hasher, typename KeyEqual, typename Alloc>
class F14FastSet : public std::conditional_t<
                       sizeof(Key) < 24,
                       F14ValueSet<Key, Hasher, KeyEqual, Alloc>,
                       F14VectorSet<Key, Hasher, KeyEqual, Alloc>> {
  using Super = std::conditional_t<
      sizeof(Key) < 24,
      F14ValueSet<Key, Hasher, KeyEqual, Alloc>,
      F14VectorSet<Key, Hasher, KeyEqual, Alloc>>;

 public:
  using Super::Super;
  F14FastSet() : Super() {}
};

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

} // namespace folly
