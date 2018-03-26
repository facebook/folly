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
 * F14NodeMap, F14ValueMap, and F14VectorMap
 *
 * F14FastMap conditionally inherits from F14ValueMap or F14VectorMap
 *
 * See F14.md
 *
 * @author Nathan Bronson <ngbronson@fb.com>
 * @author Xiao Shi       <xshi@fb.com>
 */

#include <stdexcept>

#include <folly/Traits.h>
#include <folly/functional/ApplyTuple.h>
#include <folly/lang/Exception.h>
#include <folly/lang/SafeAssert.h>

#include <folly/container/F14Map-pre.h>
#include <folly/container/detail/F14Policy.h>
#include <folly/container/detail/F14Table.h>

#if !FOLLY_F14_VECTOR_INTRINSICS_AVAILABLE

#include <unordered_map>

namespace folly {

template <typename K, typename M, typename H, typename E, typename A>
class F14ValueMap : public std::unordered_map<K, M, H, E, A> {
  using Super = std::unordered_map<K, M, H, E, A>;

 public:
  using Super::Super;
  F14ValueMap() : Super() {}
};

template <typename K, typename M, typename H, typename E, typename A>
class F14NodeMap : public std::unordered_map<K, M, H, E, A> {
  using Super = std::unordered_map<K, M, H, E, A>;

 public:
  using Super::Super;
  F14NodeMap() : Super() {}
};

template <typename K, typename M, typename H, typename E, typename A>
class F14VectorMap : public std::unordered_map<K, M, H, E, A> {
  using Super = std::unordered_map<K, M, H, E, A>;

 public:
  using Super::Super;
  F14VectorMap() : Super() {}
};

} // namespace folly

#else // FOLLY_F14_VECTOR_INTRINSICS_AVAILABLE

namespace folly {
namespace f14 {
namespace detail {

template <typename Policy>
class F14BasicMap {
  template <
      typename K,
      typename T,
      typename H = typename Policy::Hasher,
      typename E = typename Policy::KeyEqual>
  using IfIsTransparent = folly::_t<EnableIfIsTransparent<void, H, E, K, T>>;

 public:
  //// PUBLIC - Member types

  using key_type = typename Policy::Key;
  using mapped_type = typename Policy::Mapped;
  using value_type = typename Policy::Value;
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
  using const_iterator = typename Policy::ConstIter;

 private:
  using ItemIter = typename Policy::ItemIter;

 public:
  //// PUBLIC - Member functions

  F14BasicMap() noexcept(F14Table<Policy>::kDefaultConstructIsNoexcept)
      : F14BasicMap(0) {}

  explicit F14BasicMap(
      std::size_t initialCapacity,
      hasher const& hash = hasher{},
      key_equal const& eq = key_equal{},
      allocator_type const& alloc = allocator_type{})
      : table_{initialCapacity, hash, eq, alloc} {}

  explicit F14BasicMap(std::size_t initialCapacity, allocator_type const& alloc)
      : F14BasicMap(initialCapacity, hasher{}, key_equal{}, alloc) {}

  explicit F14BasicMap(
      std::size_t initialCapacity,
      hasher const& hash,
      allocator_type const& alloc)
      : F14BasicMap(initialCapacity, hash, key_equal{}, alloc) {}

  explicit F14BasicMap(allocator_type const& alloc) : F14BasicMap(0, alloc) {}

  template <typename InputIt>
  F14BasicMap(
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
  F14BasicMap(
      InputIt first,
      InputIt last,
      std::size_t initialCapacity,
      allocator_type const& alloc)
      : table_{initialCapacity, hasher{}, key_equal{}, alloc} {
    initialInsert(first, last, initialCapacity);
  }

  template <typename InputIt>
  F14BasicMap(
      InputIt first,
      InputIt last,
      std::size_t initialCapacity,
      hasher const& hash,
      allocator_type const& alloc)
      : table_{initialCapacity, hash, key_equal{}, alloc} {
    initialInsert(first, last, initialCapacity);
  }

  F14BasicMap(F14BasicMap const& rhs) = default;

  F14BasicMap(F14BasicMap const& rhs, allocator_type const& alloc)
      : table_{rhs.table_, alloc} {}

  F14BasicMap(F14BasicMap&& rhs) = default;

  F14BasicMap(F14BasicMap&& rhs, allocator_type const& alloc) noexcept(
      F14Table<Policy>::kAllocIsAlwaysEqual)
      : table_{std::move(rhs.table_), alloc} {}

  F14BasicMap(
      std::initializer_list<value_type> init,
      std::size_t initialCapacity = 0,
      hasher const& hash = hasher{},
      key_equal const& eq = key_equal{},
      allocator_type const& alloc = allocator_type{})
      : table_{initialCapacity, hash, eq, alloc} {
    initialInsert(init.begin(), init.end(), initialCapacity);
  }

  F14BasicMap(
      std::initializer_list<value_type> init,
      std::size_t initialCapacity,
      allocator_type const& alloc)
      : table_{initialCapacity, hasher{}, key_equal{}, alloc} {
    initialInsert(init.begin(), init.end(), initialCapacity);
  }

  F14BasicMap(
      std::initializer_list<value_type> init,
      std::size_t initialCapacity,
      hasher const& hash,
      allocator_type const& alloc)
      : table_{initialCapacity, hash, key_equal{}, alloc} {
    initialInsert(init.begin(), init.end(), initialCapacity);
  }

  F14BasicMap& operator=(F14BasicMap const&) = default;

  F14BasicMap& operator=(F14BasicMap&&) = default;

  allocator_type get_allocator() const noexcept {
    return table_.alloc();
  }

  //// PUBLIC - Iterators

  iterator begin() noexcept {
    return table_.makeIter(table_.begin());
  }
  const_iterator begin() const noexcept {
    return cbegin();
  }
  const_iterator cbegin() const noexcept {
    return table_.makeConstIter(table_.begin());
  }

  iterator end() noexcept {
    return table_.makeIter(table_.end());
  }
  const_iterator end() const noexcept {
    return cend();
  }
  const_iterator cend() const noexcept {
    return table_.makeConstIter(table_.end());
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

  F14TableStats computeStats() const noexcept {
    return table_.computeStats();
  }

  //// PUBLIC - Modifiers

  void clear() noexcept {
    table_.clear();
  }

  std::pair<iterator, bool> insert(value_type const& value) {
    return emplace(value);
  }

  template <typename P>
  std::enable_if_t<
      std::is_constructible<value_type, P&&>::value,
      std::pair<iterator, bool>>
  insert(P&& value) {
    return emplace(std::forward<P>(value));
  }

  std::pair<iterator, bool> insert(value_type&& value) {
    return emplace(std::move(value));
  }

  // std::unordered_map's hinted insertion API is misleading.  No
  // implementation I've seen actually uses the hint.  Code restructuring
  // by the caller to use the hinted API is at best unnecessary, and at
  // worst a pessimization.  It is used, however, so we provide it.

  iterator insert(const_iterator /*hint*/, value_type const& value) {
    return insert(value).first;
  }

  template <typename P>
  std::enable_if_t<std::is_constructible<value_type, P&&>::value, iterator>
  insert(const_iterator /*hint*/, P&& value) {
    return insert(std::forward<P>(value)).first;
  }

  iterator insert(const_iterator /*hint*/, value_type&& value) {
    return insert(std::move(value)).first;
  }

  template <class... Args>
  iterator emplace_hint(const_iterator /*hint*/, Args&&... args) {
    return emplace(std::forward<Args>(args)...).first;
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

  template <typename M>
  std::pair<iterator, bool> insert_or_assign(key_type const& key, M&& obj) {
    auto rv = try_emplace(key, std::forward<M>(obj));
    if (!rv.second) {
      rv.first->second = std::forward<M>(obj);
    }
    return rv;
  }

  template <typename M>
  std::pair<iterator, bool> insert_or_assign(key_type&& key, M&& obj) {
    auto rv = try_emplace(std::move(key), std::forward<M>(obj));
    if (!rv.second) {
      rv.first->second = std::forward<M>(obj);
    }
    return rv;
  }

  template <typename M>
  iterator
  insert_or_assign(const_iterator /*hint*/, key_type const& key, M&& obj) {
    return insert_or_assign(key, std::move(obj)).first;
  }

  template <typename M>
  iterator insert_or_assign(const_iterator /*hint*/, key_type&& key, M&& obj) {
    return insert_or_assign(std::move(key), std::move(obj)).first;
  }

 private:
  std::pair<ItemIter, bool> emplaceItem() {
    // rare but valid
    return table_.tryEmplaceValue(key_type{});
  }

  template <typename U2>
  std::pair<ItemIter, bool> emplaceItem(key_type&& x, U2&& y) {
    // best case
    return table_.tryEmplaceValue(x, std::move(x), std::forward<U2>(y));
  }

  template <typename U2>
  std::pair<ItemIter, bool> emplaceItem(key_type const& x, U2&& y) {
    // okay case, no construction unless we will actually insert
    return table_.tryEmplaceValue(x, x, std::forward<U2>(y));
  }

  template <typename U1, typename U2>
  std::enable_if_t<
      !std::is_same<key_type, folly::remove_cvref_t<U1>>::value,
      std::pair<ItemIter, bool>>
  emplaceItem(U1&& x, U2&& y) {
    static_assert(
        !std::is_same<key_type, folly::remove_cvref_t<U1>>::value,
        "method signature bug");

    // We can either construct key_type on the stack and move it if we end
    // up inserting, or use a policy-specific mechanism to construct the
    // item (possibly indirect) and then destroy it if we don't end up
    // using it.  The cost of being wrong is much higher for the latter
    // so we choose the former (unlike std::unordered_map::emplace).
    key_type k(std::forward<U1>(x));
    return table_.tryEmplaceValue(k, std::move(k), std::forward<U2>(y));
  }

  template <typename U1, typename U2>
  std::pair<ItemIter, bool> emplaceItem(std::pair<U1, U2> const& p) {
    return emplaceItem(p.first, p.second);
  }

  template <typename U1, typename U2>
  std::pair<ItemIter, bool> emplaceItem(std::pair<U1, U2>&& p) {
    return emplaceItem(std::move(p.first), std::move(p.second));
  }

  template <typename U1, class... Args2>
  std::enable_if_t<
      std::is_same<folly::remove_cvref_t<U1>, key_type>::value,
      std::pair<ItemIter, bool>>
  emplaceItem(
      std::piecewise_construct_t,
      std::tuple<U1>&& first_args,
      std::tuple<Args2...>&& second_args) {
    // We take care to forward by reference even if the caller didn't
    // use forward_as_tuple properly
    return table_.tryEmplaceValue(
        std::get<0>(first_args),
        std::piecewise_construct,
        std::tuple<std::add_rvalue_reference_t<U1>>{std::move(first_args)},
        std::tuple<std::add_rvalue_reference_t<Args2>...>{
            std::move(second_args)});
  }

  template <class... Args1, class... Args2>
  std::enable_if_t<
      std::tuple_size<std::tuple<Args1...>>::value != 1 ||
          !std::is_same<
              folly::remove_cvref_t<
                  std::tuple_element_t<0, std::tuple<Args1..., value_type>>>,
              key_type>::value,
      std::pair<ItemIter, bool>>
  emplaceItem(
      std::piecewise_construct_t,
      std::tuple<Args1...>&& first_args,
      std::tuple<Args2...>&& second_args) {
    auto k = folly::make_from_tuple<key_type>(
        std::tuple<std::add_rvalue_reference_t<Args1>...>{
            std::move(first_args)});
    return table_.tryEmplaceValue(
        k,
        std::piecewise_construct,
        std::forward_as_tuple(std::move(k)),
        std::tuple<std::add_rvalue_reference_t<Args2>...>{
            std::move(second_args)});
  }

 public:
  template <typename... Args>
  std::pair<iterator, bool> emplace(Args&&... args) {
    auto rv = emplaceItem(std::forward<Args>(args)...);
    return std::make_pair(table_.makeIter(rv.first), rv.second);
  }

  template <typename... Args>
  std::pair<iterator, bool> try_emplace(key_type const& key, Args&&... args) {
    auto rv = table_.tryEmplaceValue(
        key,
        std::piecewise_construct,
        std::forward_as_tuple(key),
        std::forward_as_tuple(std::forward<Args>(args)...));
    return std::make_pair(table_.makeIter(rv.first), rv.second);
  }

  template <typename... Args>
  std::pair<iterator, bool> try_emplace(key_type&& key, Args&&... args) {
    auto rv = table_.tryEmplaceValue(
        key,
        std::piecewise_construct,
        std::forward_as_tuple(std::move(key)),
        std::forward_as_tuple(std::forward<Args>(args)...));
    return std::make_pair(table_.makeIter(rv.first), rv.second);
  }

  template <typename... Args>
  iterator
  try_emplace(const_iterator /*hint*/, key_type const& key, Args&&... args) {
    auto rv = table_.tryEmplaceValue(
        key,
        std::piecewise_construct,
        std::forward_as_tuple(key),
        std::forward_as_tuple(std::forward<Args>(args)...));
    return table_.makeIter(rv.first);
  }

  template <typename... Args>
  iterator
  try_emplace(const_iterator /*hint*/, key_type&& key, Args&&... args) {
    auto rv = table_.tryEmplaceValue(
        key,
        std::piecewise_construct,
        std::forward_as_tuple(std::move(key)),
        std::forward_as_tuple(std::forward<Args>(args)...));
    return table_.makeIter(rv.first);
  }

  FOLLY_ALWAYS_INLINE iterator erase(const_iterator pos) {
    // If we are inlined then gcc and clang can optimize away all of the
    // work of itemPos.advance() if our return value is discarded.
    auto itemPos = table_.unwrapIter(pos);
    table_.erase(itemPos);
    itemPos.advance();
    return table_.makeIter(itemPos);
  }

  // This form avoids ambiguity when key_type has a templated constructor
  // that accepts const_iterator
  iterator erase(iterator pos) {
    table_.erase(table_.unwrapIter(pos));
    return ++pos;
  }

  iterator erase(const_iterator first, const_iterator last) {
    auto itemFirst = table_.unwrapIter(first);
    auto itemLast = table_.unwrapIter(last);
    while (itemFirst != itemLast) {
      table_.erase(itemFirst);
      itemFirst.advance();
    }
    return table_.makeIter(itemFirst);
  }

  size_type erase(key_type const& key) {
    return table_.erase(key);
  }

  //// PUBLIC - Lookup

  FOLLY_ALWAYS_INLINE mapped_type& at(key_type const& key) {
    return at(*this, key);
  }

  FOLLY_ALWAYS_INLINE mapped_type const& at(key_type const& key) const {
    return at(*this, key);
  }

  mapped_type& operator[](key_type const& key) {
    return try_emplace(key).first->second;
  }

  mapped_type& operator[](key_type&& key) {
    return try_emplace(std::move(key)).first->second;
  }

  FOLLY_ALWAYS_INLINE std::size_t count(key_type const& key) const {
    return table_.find(key).atEnd() ? 0 : 1;
  }

  template <typename K>
  FOLLY_ALWAYS_INLINE IfIsTransparent<K, std::size_t> count(
      K const& key) const {
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
    return table_.makeIter(table_.find(key));
  }

  FOLLY_ALWAYS_INLINE const_iterator find(key_type const& key) const {
    return table_.makeConstIter(table_.find(key));
  }

  FOLLY_ALWAYS_INLINE iterator
  find(F14HashToken const& token, key_type const& key) {
    return table_.makeIter(table_.find(token, key));
  }

  FOLLY_ALWAYS_INLINE const_iterator
  find(F14HashToken const& token, key_type const& key) const {
    return table_.makeConstIter(table_.find(token, key));
  }

  template <typename K>
  FOLLY_ALWAYS_INLINE IfIsTransparent<K, iterator> find(K const& key) {
    return table_.makeIter(table_.find(key));
  }

  template <typename K>
  FOLLY_ALWAYS_INLINE IfIsTransparent<K, const_iterator> find(
      K const& key) const {
    return table_.makeConstIter(table_.find(key));
  }

  template <typename K>
  FOLLY_ALWAYS_INLINE IfIsTransparent<K, iterator> find(
      F14HashToken const& token,
      K const& key) {
    return table_.makeIter(table_.find(token, key));
  }

  template <typename K>
  FOLLY_ALWAYS_INLINE IfIsTransparent<K, const_iterator> find(
      F14HashToken const& token,
      K const& key) const {
    return table_.makeConstIter(table_.find(token, key));
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
  FOLLY_ALWAYS_INLINE static auto& at(Self& self, K const& key) {
    auto iter = self.find(key);
    if (iter == self.end()) {
      throw_exception<std::out_of_range>("at() did not find key");
    }
    return iter->second;
  }

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

template <typename M>
bool mapsEqual(M const& lhs, M const& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (auto& kv : lhs) {
    auto iter = rhs.find(kv.first);
    if (iter == rhs.end()) {
      return false;
    }
    if (std::is_same<
            typename M::key_equal,
            std::equal_to<typename M::key_type>>::value) {
      // find already checked key, just check value
      if (!(kv.second == iter->second)) {
        return false;
      }
    } else {
      // spec says we compare key with == as well as with key_eq()
      if (!(kv == *iter)) {
        return false;
      }
    }
  }
  return true;
}
} // namespace detail
} // namespace f14

template <
    typename Key,
    typename Mapped,
    typename Hasher,
    typename KeyEqual,
    typename Alloc>
class F14ValueMap
    : public f14::detail::F14BasicMap<f14::detail::MapPolicyWithDefaults<
          f14::detail::ValueContainerPolicy,
          Key,
          Mapped,
          Hasher,
          KeyEqual,
          Alloc>> {
  using Policy = f14::detail::MapPolicyWithDefaults<
      f14::detail::ValueContainerPolicy,
      Key,
      Mapped,
      Hasher,
      KeyEqual,
      Alloc>;
  using Super = f14::detail::F14BasicMap<Policy>;

 public:
  F14ValueMap() noexcept(
      f14::detail::F14Table<Policy>::kDefaultConstructIsNoexcept)
      : Super{} {}

  using Super::Super;

  void swap(F14ValueMap& rhs) noexcept(
      f14::detail::F14Table<Policy>::kSwapIsNoexcept) {
    this->table_.swap(rhs.table_);
  }
};

template <typename K, typename M, typename H, typename E, typename A>
bool operator==(
    F14ValueMap<K, M, H, E, A> const& lhs,
    F14ValueMap<K, M, H, E, A> const& rhs) {
  return mapsEqual(lhs, rhs);
}

template <typename K, typename M, typename H, typename E, typename A>
bool operator!=(
    F14ValueMap<K, M, H, E, A> const& lhs,
    F14ValueMap<K, M, H, E, A> const& rhs) {
  return !(lhs == rhs);
}

template <
    typename Key,
    typename Mapped,
    typename Hasher,
    typename KeyEqual,
    typename Alloc>
class F14NodeMap
    : public f14::detail::F14BasicMap<f14::detail::MapPolicyWithDefaults<
          f14::detail::NodeContainerPolicy,
          Key,
          Mapped,
          Hasher,
          KeyEqual,
          Alloc>> {
  using Policy = f14::detail::MapPolicyWithDefaults<
      f14::detail::NodeContainerPolicy,
      Key,
      Mapped,
      Hasher,
      KeyEqual,
      Alloc>;
  using Super = f14::detail::F14BasicMap<Policy>;

 public:
  F14NodeMap() noexcept(
      f14::detail::F14Table<Policy>::kDefaultConstructIsNoexcept)
      : Super{} {}

  using Super::Super;

  void swap(F14NodeMap& rhs) noexcept(
      f14::detail::F14Table<Policy>::kSwapIsNoexcept) {
    this->table_.swap(rhs.table_);
  }

  // TODO extract and node_handle insert
};

template <typename K, typename M, typename H, typename E, typename A>
bool operator==(
    F14NodeMap<K, M, H, E, A> const& lhs,
    F14NodeMap<K, M, H, E, A> const& rhs) {
  return mapsEqual(lhs, rhs);
}

template <typename K, typename M, typename H, typename E, typename A>
bool operator!=(
    F14NodeMap<K, M, H, E, A> const& lhs,
    F14NodeMap<K, M, H, E, A> const& rhs) {
  return !(lhs == rhs);
}

template <
    typename Key,
    typename Mapped,
    typename Hasher,
    typename KeyEqual,
    typename Alloc>
class F14VectorMap
    : public f14::detail::F14BasicMap<f14::detail::MapPolicyWithDefaults<
          f14::detail::VectorContainerPolicy,
          Key,
          Mapped,
          Hasher,
          KeyEqual,
          Alloc>> {
  using Policy = f14::detail::MapPolicyWithDefaults<
      f14::detail::VectorContainerPolicy,
      Key,
      Mapped,
      Hasher,
      KeyEqual,
      Alloc>;
  using Super = f14::detail::F14BasicMap<Policy>;

 public:
  using typename Super::const_iterator;
  using typename Super::iterator;
  using typename Super::key_type;
  using reverse_iterator = typename Policy::ReverseIter;
  using const_reverse_iterator = typename Policy::ConstReverseIter;

  F14VectorMap() noexcept(
      f14::detail::F14Table<Policy>::kDefaultConstructIsNoexcept)
      : Super{} {}

  // inherit constructors
  using Super::Super;

  void swap(F14VectorMap& rhs) noexcept(
      f14::detail::F14Table<Policy>::kSwapIsNoexcept) {
    this->table_.swap(rhs.table_);
  }

  // ITERATION ORDER
  //
  // Deterministic iteration order for insert-only workloads is part of
  // F14VectorMap's supported API: iterator is LIFO and reverse_iterator
  // is FIFO.
  //
  // If there have been no calls to erase() then iterator and
  // const_iterator enumerate entries in the opposite of insertion order.
  // begin()->first is the key most recently inserted.  reverse_iterator
  // and reverse_const_iterator, therefore, enumerate in LIFO (insertion)
  // order for insert-only workloads.  Deterministic iteration order is
  // only guaranteed if no keys were removed since the last time the
  // map was empty.  Iteration order is preserved across rehashes and
  // F14VectorMap copies and moves.
  //
  // iterator uses LIFO order so that erasing while iterating with begin()
  // and end() is safe using the erase(it++) idiom, which is supported
  // by std::map and std::unordered_map.  erase(iter) invalidates iter
  // and all iterators before iter in the non-reverse iteration order.
  // Every successful erase invalidates all reverse iterators.

  iterator begin() {
    return this->table_.linearBegin(this->size());
  }
  const_iterator begin() const {
    return cbegin();
  }
  const_iterator cbegin() const {
    return this->table_.linearBegin(this->size());
  }

  iterator end() {
    return this->table_.linearEnd();
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
    auto index = this->table_.iterToIndex(pos);
    auto underlying =
        this->table_.find(f14::detail::VectorContainerIndexSearch{index});
    eraseUnderlying(underlying);
    return index == 0 ? end() : this->table_.indexToIter(index - 1);
  }

  // This form avoids ambiguity when key_type has a templated constructor
  // that accepts const_iterator
  FOLLY_ALWAYS_INLINE iterator erase(iterator pos) {
    const_iterator cpos{pos};
    return erase(cpos);
  }

  iterator erase(const_iterator first, const_iterator last) {
    while (first != last) {
      first = erase(first);
    }
    auto index = this->table_.iterToIndex(first);
    return index == 0 ? end() : this->table_.indexToIter(index - 1);
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

template <typename K, typename M, typename H, typename E, typename A>
bool operator==(
    F14VectorMap<K, M, H, E, A> const& lhs,
    F14VectorMap<K, M, H, E, A> const& rhs) {
  return mapsEqual(lhs, rhs);
}

template <typename K, typename M, typename H, typename E, typename A>
bool operator!=(
    F14VectorMap<K, M, H, E, A> const& lhs,
    F14VectorMap<K, M, H, E, A> const& rhs) {
  return !(lhs == rhs);
}

} // namespace folly

#endif // FOLLY_F14_VECTOR_INTRINSICS_AVAILABLE

namespace folly {

template <
    typename Key,
    typename Mapped,
    typename Hasher,
    typename KeyEqual,
    typename Alloc>
class F14FastMap : public std::conditional_t<
                       sizeof(std::pair<Key const, Mapped>) < 24,
                       F14ValueMap<Key, Mapped, Hasher, KeyEqual, Alloc>,
                       F14VectorMap<Key, Mapped, Hasher, KeyEqual, Alloc>> {
  using Super = std::conditional_t<
      sizeof(std::pair<Key const, Mapped>) < 24,
      F14ValueMap<Key, Mapped, Hasher, KeyEqual, Alloc>,
      F14VectorMap<Key, Mapped, Hasher, KeyEqual, Alloc>>;

 public:
  using Super::Super;
  F14FastMap() : Super() {}
};

template <typename K, typename M, typename H, typename E, typename A>
void swap(
    F14ValueMap<K, M, H, E, A>& lhs,
    F14ValueMap<K, M, H, E, A>& rhs) noexcept(noexcept(lhs.swap(rhs))) {
  lhs.swap(rhs);
}

template <typename K, typename M, typename H, typename E, typename A>
void swap(
    F14NodeMap<K, M, H, E, A>& lhs,
    F14NodeMap<K, M, H, E, A>& rhs) noexcept(noexcept(lhs.swap(rhs))) {
  lhs.swap(rhs);
}

template <typename K, typename M, typename H, typename E, typename A>
void swap(
    F14VectorMap<K, M, H, E, A>& lhs,
    F14VectorMap<K, M, H, E, A>& rhs) noexcept(noexcept(lhs.swap(rhs))) {
  lhs.swap(rhs);
}

template <typename K, typename M, typename H, typename E, typename A>
void swap(
    F14FastMap<K, M, H, E, A>& lhs,
    F14FastMap<K, M, H, E, A>& rhs) noexcept(noexcept(lhs.swap(rhs))) {
  lhs.swap(rhs);
}

} // namespace folly
