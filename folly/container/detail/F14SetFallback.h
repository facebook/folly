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
#include <type_traits>
#include <unordered_set>

#include <folly/container/detail/F14Table.h>
#include <folly/container/detail/Util.h>

/**
 * This file is intended to be included only by F14Set.h. It contains fallback
 * implementations of F14Set types for platforms that do not support the
 * required SIMD instructions, based on std::unordered_set.
 */

#if !FOLLY_F14_VECTOR_INTRINSICS_AVAILABLE

namespace folly {

namespace f14 {
namespace detail {
template <typename KeyType, typename Hasher, typename KeyEqual, typename Alloc>
class F14BasicSet
    : public std::unordered_set<KeyType, Hasher, KeyEqual, Alloc> {
  using Super = std::unordered_set<KeyType, Hasher, KeyEqual, Alloc>;

 public:
  using typename Super::allocator_type;
  using typename Super::const_iterator;
  using typename Super::hasher;
  using typename Super::iterator;
  using typename Super::key_equal;
  using typename Super::key_type;
  using typename Super::pointer;
  using typename Super::size_type;
  using typename Super::value_type;

 private:
  template <typename K, typename T>
  using EnableHeterogeneousFind = std::enable_if_t<
      ::folly::detail::
          EligibleForHeterogeneousFind<key_type, hasher, key_equal, K>::value,
      T>;

  template <typename K, typename T>
  using EnableHeterogeneousInsert = std::enable_if_t<
      ::folly::detail::
          EligibleForHeterogeneousInsert<key_type, hasher, key_equal, K>::value,
      T>;

  template <typename K>
  using IsIter = Disjunction<
      std::is_same<iterator, remove_cvref_t<K>>,
      std::is_same<const_iterator, remove_cvref_t<K>>>;

  template <typename K, typename T>
  using EnableHeterogeneousErase = std::enable_if_t<
      ::folly::detail::EligibleForHeterogeneousFind<
          key_type,
          hasher,
          key_equal,
          std::conditional_t<IsIter<K>::value, key_type, K>>::value &&
          !IsIter<K>::value,
      T>;

 public:
  F14BasicSet() = default;

  using Super::Super;

  //// PUBLIC - Modifiers

  using Super::insert;

  template <typename K>
  EnableHeterogeneousInsert<K, std::pair<iterator, bool>> insert(K&& value) {
    return emplace(std::forward<K>(value));
  }

  template <class InputIt>
  void insert(InputIt first, InputIt last) {
    while (first != last) {
      insert(*first);
      ++first;
    }
  }

 private:
  template <typename Arg>
  using UsableAsKey = ::folly::detail::
      EligibleForHeterogeneousFind<key_type, hasher, key_equal, Arg>;

 public:
  template <class... Args>
  std::pair<iterator, bool> emplace(Args&&... args) {
    auto a = this->get_allocator();
    return folly::detail::callWithConstructedKey<key_type, UsableAsKey>(
        a,
        [&](auto const&, auto&& key) {
          if (!std::is_same<key_type, remove_cvref_t<decltype(key)>>::value) {
            // this is a heterogeneous emplace
            auto it = find(key);
            if (it != this->end()) {
              return std::make_pair(it, false);
            }
            auto rv = Super::emplace(std::forward<decltype(key)>(key));
            FOLLY_SAFE_DCHECK(
                rv.second, "post-find emplace should always insert");
            return rv;
          } else {
            return Super::emplace(std::forward<decltype(key)>(key));
          }
        },
        std::forward<Args>(args)...);
  }

  template <class... Args>
  iterator emplace_hint(const_iterator /*hint*/, Args&&... args) {
    return emplace(std::forward<Args>(args)...).first;
  }

  using Super::erase;

  template <typename K>
  EnableHeterogeneousErase<K, size_type> erase(K const& key) {
    auto it = find(key);
    if (it != this->end()) {
      erase(it);
      return 1;
    } else {
      return 0;
    }
  }

  //// PUBLIC - Lookup

 private:
  // BottomKeyEqual must have same size, alignment, emptiness, and finality as
  // KeyEqual
  struct BottomKeyEqualEmpty {};
  template <size_t S, size_t A>
  struct BottomKeyEqualNonEmpty {
    alignas(A) char data[S];
  };
  using BottomKeyEqualBase = conditional_t<
      std::is_empty<KeyEqual>::value,
      BottomKeyEqualEmpty,
      BottomKeyEqualNonEmpty<sizeof(KeyEqual), alignof(KeyEqual)>>;
  template <bool IsFinal, typename K>
  struct BottomKeyEqualCond : BottomKeyEqualBase {
    [[noreturn]] bool operator()(K const&, K const&) const {
      assume_unreachable();
    }
  };
  template <typename K>
  struct BottomKeyEqualCond<true, K> final : BottomKeyEqualCond<false, K> {};
  template <typename K>
  using BottomKeyEqual = BottomKeyEqualCond<
      std::is_final<KeyEqual>::value || std::is_union<KeyEqual>::value,
      K>;
  using BottomTest = BottomKeyEqual<char>;
  static_assert(sizeof(BottomTest) == sizeof(KeyEqual), "mismatch size");
  static_assert(alignof(BottomTest) == alignof(KeyEqual), "mismatch align");
  static_assert(
      std::is_empty<BottomTest>::value == std::is_empty<KeyEqual>::value,
      "mismatch is-empty");
  static_assert(
      (std::is_final<BottomTest>::value || std::is_union<BottomTest>::value) ==
          (std::is_final<KeyEqual>::value || std::is_union<KeyEqual>::value),
      "mismatch is-final");

  template <typename Iter, typename LocalIter>
  static std::
      enable_if_t<std::is_constructible<Iter, LocalIter const&>::value, Iter>
      fromLocal(LocalIter const& src, int = 0) {
    return Iter(src);
  }

  template <typename Iter, typename LocalIter>
  static std::
      enable_if_t<!std::is_constructible<Iter, LocalIter const&>::value, Iter>
      fromLocal(LocalIter const& src) {
    Iter dst;
    static_assert(sizeof(dst) <= sizeof(src), "");
    std::memcpy(std::addressof(dst), std::addressof(src), sizeof(dst));
    FOLLY_SAFE_CHECK(
        std::addressof(*src) == std::addressof(*dst),
        "ABI-assuming local_iterator to iterator conversion failed");
    return dst;
  }

  template <typename Iter, typename Self, typename K>
  static Iter findImpl(Self& self, K const& key) {
    if (self.empty()) {
      return self.end();
    }
    using A = typename std::allocator_traits<
        allocator_type>::template rebind_alloc<K>;
    using E = BottomKeyEqual<K>;
    // this is exceedingly wicked!
    auto slot =
        reinterpret_cast<std::unordered_set<K, hasher, E, A> const&>(self)
            .bucket(key);
    auto b = self.begin(slot);
    auto e = self.end(slot);
    while (b != e) {
      if (self.key_eq()(key, *b)) {
        return fromLocal<Iter>(b);
      }
      ++b;
    }
    FOLLY_SAFE_DCHECK(
        self.size() > 3 ||
            std::none_of(
                self.begin(),
                self.end(),
                [&](auto const& k) { return self.key_eq()(key, k); }),
        "");
    return self.end();
  }

 public:
  using Super::count;

  template <typename K>
  EnableHeterogeneousFind<K, size_type> count(K const& key) const {
    return contains(key) ? 1 : 0;
  }

  using Super::find;

  template <typename K>
  EnableHeterogeneousFind<K, iterator> find(K const& key) {
    return findImpl<iterator>(*this, key);
  }

  template <typename K>
  EnableHeterogeneousFind<K, const_iterator> find(K const& key) const {
    return findImpl<const_iterator>(*this, key);
  }

  bool contains(key_type const& key) const { return find(key) != this->end(); }

  template <typename K>
  EnableHeterogeneousFind<K, bool> contains(K const& key) const {
    return find(key) != this->end();
  }

 private:
  template <typename Self, typename K>
  static auto equalRangeImpl(Self& self, K const& key) {
    auto first = self.find(key);
    auto last = first;
    if (last != self.end()) {
      ++last;
    }
    return std::make_pair(first, last);
  }

 public:
  using Super::equal_range;

  template <typename K>
  EnableHeterogeneousFind<K, std::pair<iterator, iterator>> equal_range(
      K const& key) {
    return equalRangeImpl(*this, key);
  }

  template <typename K>
  EnableHeterogeneousFind<K, std::pair<const_iterator, const_iterator>>
  equal_range(K const& key) const {
    return equalRangeImpl(*this, key);
  }

  //// PUBLIC - F14 Extensions

#if FOLLY_F14_ERASE_INTO_AVAILABLE
 private:
  // converts const_iterator to iterator when they are different types
  // such as in libstdc++
  template <typename... Args>
  iterator citerToIter(const_iterator cit, Args&&...) {
    iterator it = erase(cit, cit);
    FOLLY_SAFE_CHECK(std::addressof(*it) == std::addressof(*cit), "");
    return it;
  }

  // converts const_iterator to iterator when they are the same type
  // such as in libc++
  iterator citerToIter(iterator it) { return it; }

 public:
  template <typename BeforeDestroy>
  iterator eraseInto(const_iterator pos, BeforeDestroy&& beforeDestroy) {
    iterator next = citerToIter(pos);
    ++next;
    auto nh = this->extract(pos);
    if (!nh.empty()) {
      beforeDestroy(std::move(nh.value()));
    }
    return next;
  }

  template <typename BeforeDestroy>
  iterator eraseInto(
      const_iterator first,
      const_iterator last,
      BeforeDestroy&& beforeDestroy) {
    iterator pos = citerToIter(first);
    while (pos != last) {
      pos = eraseInto(pos, beforeDestroy);
    }
    return pos;
  }

 private:
  template <typename K, typename BeforeDestroy>
  size_type eraseIntoImpl(K const& key, BeforeDestroy& beforeDestroy) {
    auto it = find(key);
    if (it != this->end()) {
      eraseInto(it, beforeDestroy);
      return 1;
    } else {
      return 0;
    }
  }

 public:
  template <typename BeforeDestroy>
  size_type eraseInto(key_type const& key, BeforeDestroy&& beforeDestroy) {
    return eraseIntoImpl(key, beforeDestroy);
  }

  template <typename K, typename BeforeDestroy>
  EnableHeterogeneousErase<K, size_type> eraseInto(
      K const& key, BeforeDestroy&& beforeDestroy) {
    return eraseIntoImpl(key, beforeDestroy);
  }
#endif

  bool containsEqualValue(value_type const& value) const {
    // bucket is only valid if bucket_count is non-zero
    if (this->empty()) {
      return false;
    }
    auto slot = this->bucket(value);
    auto e = this->end(slot);
    for (auto b = this->begin(slot); b != e; ++b) {
      if (*b == value) {
        return true;
      }
    }
    return false;
  }

  // exact for libstdc++, approximate for others
  std::size_t getAllocatedMemorySize() const {
    std::size_t rv = 0;
    visitAllocationClasses(
        [&](std::size_t bytes, std::size_t n) { rv += bytes * n; });
    return rv;
  }

  // exact for libstdc++, approximate for others
  template <typename V>
  void visitAllocationClasses(V&& visitor) const {
    auto bc = this->bucket_count();
    if (bc > 1) {
      visitor(bc * sizeof(pointer), 1);
    }
    if (this->size() > 0) {
      visitor(
          sizeof(StdNodeReplica<key_type, value_type, hasher>), this->size());
    }
  }

  template <typename V>
  void visitContiguousRanges(V&& visitor) const {
    for (value_type const& entry : *this) {
      value_type const* b = std::addressof(entry);
      visitor(b, b + 1);
    }
  }
};
} // namespace detail
} // namespace f14

template <typename Key, typename Hasher, typename KeyEqual, typename Alloc>
class F14NodeSet
    : public f14::detail::F14BasicSet<Key, Hasher, KeyEqual, Alloc> {
  using Super = f14::detail::F14BasicSet<Key, Hasher, KeyEqual, Alloc>;

 public:
  using typename Super::value_type;

  F14NodeSet() = default;

  using Super::Super;

  F14NodeSet& operator=(std::initializer_list<value_type> ilist) {
    Super::operator=(ilist);
    return *this;
  }
};

template <typename Key, typename Hasher, typename KeyEqual, typename Alloc>
class F14ValueSet
    : public f14::detail::F14BasicSet<Key, Hasher, KeyEqual, Alloc> {
  using Super = f14::detail::F14BasicSet<Key, Hasher, KeyEqual, Alloc>;

 public:
  using typename Super::value_type;

  F14ValueSet() : Super() {}

  using Super::Super;

  F14ValueSet& operator=(std::initializer_list<value_type> ilist) {
    Super::operator=(ilist);
    return *this;
  }
};

template <typename Key, typename Hasher, typename KeyEqual, typename Alloc>
class F14VectorSet
    : public f14::detail::F14BasicSet<Key, Hasher, KeyEqual, Alloc> {
  using Super = f14::detail::F14BasicSet<Key, Hasher, KeyEqual, Alloc>;

 public:
  using typename Super::value_type;

  F14VectorSet() = default;

  using Super::Super;

  F14VectorSet& operator=(std::initializer_list<value_type> ilist) {
    Super::operator=(ilist);
    return *this;
  }
};

template <typename Key, typename Hasher, typename KeyEqual, typename Alloc>
class F14FastSet
    : public f14::detail::F14BasicSet<Key, Hasher, KeyEqual, Alloc> {
  using Super = f14::detail::F14BasicSet<Key, Hasher, KeyEqual, Alloc>;

 public:
  using typename Super::value_type;

  F14FastSet() = default;

  using Super::Super;

  F14FastSet& operator=(std::initializer_list<value_type> ilist) {
    Super::operator=(ilist);
    return *this;
  }
};

} // namespace folly

#endif // !if FOLLY_F14_VECTOR_INTRINSICS_AVAILABLE
