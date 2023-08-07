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
#include <exception>
#include <functional>

#include <boost/intrusive/list.hpp>
#include <boost/iterator/iterator_adaptor.hpp>

#include <folly/container/F14Set.h>
#include <folly/container/HeterogeneousAccess.h>
#include <folly/lang/Exception.h>

namespace folly {

/**
 * A general purpose LRU evicting cache designed to support constant time
 * set/get/insert/erase ops. The only required configuration parameter is the
 * `maxSize`, which is the maximum number of entries held by the cache, which
 * is also dynamically changeable. Insertion will evict (and destroy with ~TKey
 * and ~TValue) existing entries in LRU order as needed to keep number of
 * entries less than maxSize. When automatic eviction is triggered, the
 * minimum number of evictions is `clearSize`, which is configurable with a
 * default of 1. If a callback is specified with setPruneHook, it is invoked
 * for each eviction. However, the prune hook cannot manage object lifetimes
 * because it is not invoked on erase nor cache destruction.
 *
 * This is NOT a thread-safe implementation.
 *
 * Iterators and references are only invalidated when the referenced entry
 * might have been removed (pruned or erased), like std::map.
 *
 * NOTE: maxSize==0 is a special case that disables automatic evictions.
 * prune() can be used for manually trimming down the number of entries.
 *
 * Implementaion: Maintains a doubly linked list (`lru_`) of entry nodes in
 * LRU order, which are also connected to hash table index (`index_`). The
 * access order is maintained on the list by moving an element to the front
 * of list on a get, and adding to the front on insert. Assuming quality
 * hashing, set/get are both constant time operations.
 *
 * NOTE: Previous versions of this structure used a hash table size that was
 * fixed at creation time, but that limitation is no longer present.
 */
template <
    class TKey,
    class TValue,
    class THash = HeterogeneousAccessHash<TKey>,
    class TKeyEqual = HeterogeneousAccessEqualTo<TKey>>
class EvictingCacheMap {
 private:
  // typedefs for brevity
  struct Node;
  struct NodeList;
  struct KeyHasher;
  struct KeyValueEqual;
  using NodeMap = F14VectorSet<Node*, KeyHasher, KeyValueEqual>;
  using TPair = std::pair<const TKey, TValue>;

 public:
  using PruneHookCall = std::function<void(TKey, TValue&&)>;

  // iterator base : returns TPair on dereference
  template <typename Value, typename TIterator>
  class iterator_base : public boost::iterator_adaptor<
                            iterator_base<Value, TIterator>,
                            TIterator,
                            Value,
                            boost::bidirectional_traversal_tag> {
   public:
    iterator_base() {}

    explicit iterator_base(TIterator it)
        : iterator_base::iterator_adaptor_(it) {}

    template <
        typename V,
        typename I,
        std::enable_if_t<
            std::is_same<V const, Value>::value &&
                std::is_convertible<I, TIterator>::value,
            int> = 0>
    /* implicit */ iterator_base(iterator_base<V, I> const& other)
        : iterator_base::iterator_adaptor_(other.base()) {}

    Value& dereference() const { return this->base_reference()->pr; }
  };

  // iterators
  using iterator = iterator_base<TPair, typename NodeList::iterator>;
  using const_iterator =
      iterator_base<const TPair, typename NodeList::const_iterator>;
  using reverse_iterator =
      iterator_base<TPair, typename NodeList::reverse_iterator>;
  using const_reverse_iterator =
      iterator_base<const TPair, typename NodeList::const_reverse_iterator>;

  // public type aliases for convenience
  using key_type = TKey;
  using mapped_type = TValue;
  using hasher = THash;

  /*
   * Approximate size of memory used by each entry added to the cache,
   * including the shallow bits (sizeof) of TKey and TValue, but not the deep
   * bits. Using 128 (bytes per chunk) / 10 (avg entries per chunk) as
   * approximate F14 index entry size.
   */
  static constexpr std::size_t kApproximateEntryMemUsage = 13 + sizeof(Node);

 private:
  template <typename K, typename T>
  using EnableHeterogeneousFind = std::enable_if_t<
      detail::EligibleForHeterogeneousFind<TKey, THash, TKeyEqual, K>::value,
      T>;

  template <typename K, typename T>
  using EnableHeterogeneousInsert = std::enable_if_t<
      detail::EligibleForHeterogeneousInsert<TKey, THash, TKeyEqual, K>::value,
      T>;

  template <typename K>
  using IsIter = Disjunction<
      std::is_same<iterator, remove_cvref_t<K>>,
      std::is_same<const_iterator, remove_cvref_t<K>>>;

  template <typename K, typename T>
  using EnableHeterogeneousErase = std::enable_if_t<
      detail::EligibleForHeterogeneousFind<
          TKey,
          THash,
          TKeyEqual,
          std::conditional_t<IsIter<K>::value, TKey, K>>::value &&
          !IsIter<K>::value,
      T>;

 public:
  /**
   * Construct a EvictingCacheMap
   * @param maxSize maximum size of the cache map.  Once the map size exceeds
   *     maxSize, the map will begin to evict.
   * @param clearSize the number of elements to clear at a time when automatic
   *     eviction on insert is triggered.
   */
  explicit EvictingCacheMap(
      std::size_t maxSize,
      std::size_t clearSize = 1,
      const THash& keyHash = THash(),
      const TKeyEqual& keyEqual = TKeyEqual())
      : keyHash_(keyHash),
        keyEqual_(keyEqual),
        index_(maxSize + /*transient*/ 1, keyHash_, keyEqual_),
        maxSize_(maxSize),
        clearSize_(clearSize) {}

  EvictingCacheMap(const EvictingCacheMap&) = delete;
  EvictingCacheMap& operator=(const EvictingCacheMap&) = delete;
  EvictingCacheMap(EvictingCacheMap&&) = default;
  EvictingCacheMap& operator=(EvictingCacheMap&&) = default;

  ~EvictingCacheMap() { assert(lru_.size() == index_.size()); }

  /**
   * Adjust the max size of EvictingCacheMap, evicting as needed to ensure the
   * new max is not exceeded.
   *
   * Calling this function with an arugment of 0 removes the limit on the cache
   * size and elements are not evicted unless clients explicitly call prune.
   *
   * @param maxSize new maximum size of the cache map.
   * @param pruneHook eviction callback to use INSTEAD OF the configured one
   */
  void setMaxSize(size_t maxSize, PruneHookCall pruneHook = nullptr) {
    if (maxSize != 0 && maxSize < size()) {
      // Prune the excess elements with our new constraints.
      prune(std::max(size() - maxSize, clearSize_), pruneHook);
    }
    maxSize_ = maxSize;
  }

  std::size_t getMaxSize() const { return maxSize_; }

  void setClearSize(std::size_t clearSize) { clearSize_ = clearSize; }

  /**
   * Check for existence of a specific key in the map.  This operation has
   *     no effect on LRU order.
   * @param key key to search for
   * @return true if exists, false otherwise
   */
  bool exists(const TKey& key) const { return existsImpl(key); }

  template <typename K, EnableHeterogeneousFind<K, int> = 0>
  bool exists(const K& key) const {
    return existsImpl(key);
  }

  /**
   * Get the value associated with a specific key.  This function always
   *     promotes a found value to the head of the LRU.
   * @param key key associated with the value
   * @return the value if it exists
   * @throw std::out_of_range exception of the key does not exist
   */
  TValue& get(const TKey& key) { return getImpl(key); }

  template <typename K, EnableHeterogeneousFind<K, int> = 0>
  TValue& get(const K& key) {
    return getImpl(key);
  }

  /**
   * Get the iterator associated with a specific key.  This function always
   *     promotes a found value to the head of the LRU.
   * @param key key to associate with value
   * @return the iterator of the object (a std::pair of const TKey, TValue) or
   *     end() if it does not exist
   */
  iterator find(const TKey& key) { return findImpl(*this, key); }

  template <typename K, EnableHeterogeneousFind<K, int> = 0>
  iterator find(const K& key) {
    return findImpl(*this, key);
  }

  /**
   * Get the value associated with a specific key.  This function never
   *     promotes a found value to the head of the LRU.
   * @param key key associated with the value
   * @return the value if it exists
   * @throw std::out_of_range exception of the key does not exist
   */
  const TValue& getWithoutPromotion(const TKey& key) const {
    return getWithoutPromotionImpl(*this, key);
  }

  template <typename K, EnableHeterogeneousFind<K, int> = 0>
  const TValue& getWithoutPromotion(const K& key) const {
    return getWithoutPromotionImpl(*this, key);
  }

  TValue& getWithoutPromotion(const TKey& key) {
    return getWithoutPromotionImpl(*this, key);
  }

  template <typename K, EnableHeterogeneousFind<K, int> = 0>
  TValue& getWithoutPromotion(const K& key) {
    return getWithoutPromotionImpl(*this, key);
  }

  /**
   * Get the iterator associated with a specific key.  This function never
   *     promotes a found value to the head of the LRU.
   * @param key key to associate with value
   * @return the iterator of the object (a std::pair of const TKey, TValue) or
   *     end() if it does not exist
   */
  const_iterator findWithoutPromotion(const TKey& key) const {
    return findWithoutPromotionImpl(*this, key);
  }

  template <typename K, EnableHeterogeneousFind<K, int> = 0>
  const_iterator findWithoutPromotion(const K& key) const {
    return findWithoutPromotionImpl(*this, key);
  }

  iterator findWithoutPromotion(const TKey& key) {
    return findWithoutPromotionImpl(*this, key);
  }

  template <typename K, EnableHeterogeneousFind<K, int> = 0>
  iterator findWithoutPromotion(const K& key) {
    return findWithoutPromotionImpl(*this, key);
  }

  /**
   * Erase the key-value pair associated with key if it exists. Prune hook
   * is not called unless one passed in here.
   * @param key key associated with the value
   * @param eraseHook callback to use with erased entry (similar to a prune
   * hook)
   * @return true if the key existed and was erased, else false
   */
  bool erase(const TKey& key, PruneHookCall eraseHook = nullptr) {
    return eraseKeyImpl(key, eraseHook);
  }

  template <typename K, EnableHeterogeneousErase<K, int> = 0>
  bool erase(const K& key, PruneHookCall eraseHook = nullptr) {
    return eraseKeyImpl(key, eraseHook);
  }

  /**
   * Erase the key-value pair associated with pos. Prune hook is not called
   * unless one passed in here.
   * @param pos iterator to the element to be erased
   * @param eraseHook callback to use with erased entry (similar to a prune
   * hook)
   * @return iterator to the following element or end() if pos was the last
   *     element
   */
  iterator erase(const_iterator pos, PruneHookCall eraseHook = nullptr) {
    return iterator(
        eraseImpl(const_cast<Node*>(&(*pos.base())), pos.base(), eraseHook));
  }

  /**
   * Set a key-value pair in the dictionary
   * @param key key to associate with value
   * @param value value to associate with the key
   * @param promote boolean flag indicating whether or not to move something
   *     to the front of an LRU.  This only really matters if you're setting
   *     a value that already exists.
   * @param pruneHook eviction callback to use INSTEAD OF the configured one
   */
  void set(
      const TKey& key,
      TValue&& value,
      bool promote = true,
      PruneHookCall pruneHook = nullptr) {
    setImpl(key, std::move(value), promote, pruneHook);
  }

  void set(
      const TKey& key,
      const TValue& value,
      bool promote = true,
      PruneHookCall pruneHook = nullptr) {
    TValue tmp{value}; // can't yet rely on temporary materialization
    setImpl(key, std::move(tmp), promote, pruneHook);
  }

  template <typename K, EnableHeterogeneousInsert<K, int> = 0>
  void set(
      const K& key,
      TValue&& value,
      bool promote = true,
      PruneHookCall pruneHook = nullptr) {
    setImpl(key, std::move(value), promote, pruneHook);
  }

  template <typename K, EnableHeterogeneousInsert<K, int> = 0>
  void set(
      const K& key,
      const TValue& value,
      bool promote = true,
      PruneHookCall pruneHook = nullptr) {
    TValue tmp{value}; // can't yet rely on temporary materialization
    setImpl(key, std::move(tmp), promote, pruneHook);
  }

  /**
   * Insert a new key-value pair in the dictionary if no element exists for key
   * @param key key to associate with value
   * @param value value to associate with the key
   * @param pruneHook eviction callback to use INSTEAD OF the configured one
   * @return a pair consisting of an iterator to the inserted element (or to the
   *     element that prevented the insertion) and a bool denoting whether the
   *     insertion took place.
   */
  std::pair<iterator, bool> insert(
      const TKey& key, TValue&& value, PruneHookCall pruneHook = nullptr) {
    return insertImpl(key, std::move(value), pruneHook);
  }

  std::pair<iterator, bool> insert(
      const TKey& key, const TValue& value, PruneHookCall pruneHook = nullptr) {
    TValue tmp{value}; // can't yet rely on temporary materialization
    return insertImpl(key, std::move(tmp), pruneHook);
  }

  template <typename K, EnableHeterogeneousInsert<K, int> = 0>
  std::pair<iterator, bool> insert(
      const K& key, TValue&& value, PruneHookCall pruneHook = nullptr) {
    return insertImpl(key, std::move(value), pruneHook);
  }

  template <typename K, EnableHeterogeneousInsert<K, int> = 0>
  std::pair<iterator, bool> insert(
      const K& key, const TValue& value, PruneHookCall pruneHook = nullptr) {
    TValue tmp{value}; // can't yet rely on temporary materialization
    return insertImpl(key, std::move(tmp), pruneHook);
  }

  /**
   * Get the number of elements in the dictionary
   * @return the size of the dictionary
   */
  std::size_t size() const {
    assert(index_.size() == lru_.size());
    return index_.size();
  }

  /**
   * Typical empty function
   * @return true if empty, false otherwise
   */
  bool empty() const { return index_.empty(); }

  /**
   * Remove all entries (as if all evicted)
   * @param pruneHook eviction callback to use INSTEAD OF the configured one
   */
  void clear(PruneHookCall pruneHook = nullptr) { prune(size(), pruneHook); }

  /**
   * Set the prune hook, which is the function invoked on the key and value
   *     on each eviction and on any entries left in final destruction.
   *     An operation will throw if the pruneHook throws, unless the
   *     EvictingCacheMap object is being destroyed in which case it will
   *     be ignored.
   * @param pruneHook eviction callback to set as default, or nullptr to clear
   */
  void setPruneHook(PruneHookCall pruneHook) { pruneHook_ = pruneHook; }

  PruneHookCall getPruneHook() { return pruneHook_; }

  /**
   * Prune the minimum of pruneSize and size() from the back of the LRU.
   * Will throw if pruneHook throws.
   * @param pruneSize minimum number of elements to prune
   * @param pruneHook eviction callback to use INSTEAD OF the configured one
   */
  void prune(std::size_t pruneSize, PruneHookCall pruneHook = nullptr) {
    auto& ph = (nullptr == pruneHook) ? pruneHook_ : pruneHook;

    for (std::size_t i = 0; i < pruneSize && !lru_.empty(); i++) {
      auto* node = &(*lru_.rbegin());
      std::unique_ptr<Node> node_owner(node);

      lru_.erase(lru_.iterator_to(*node));
      index_.erase(node);
      if (ph) {
        // NOTE: might throw, so we are in an exception-safe state
        ph(node->pr.first, std::move(node->pr.second));
      }
    }
  }

  // Iterators and such
  iterator begin() { return iterator(lru_.begin()); }
  iterator end() { return iterator(lru_.end()); }
  const_iterator begin() const { return const_iterator(lru_.begin()); }
  const_iterator end() const { return const_iterator(lru_.end()); }

  const_iterator cbegin() const { return const_iterator(lru_.cbegin()); }
  const_iterator cend() const { return const_iterator(lru_.cend()); }

  reverse_iterator rbegin() { return reverse_iterator(lru_.rbegin()); }
  reverse_iterator rend() { return reverse_iterator(lru_.rend()); }

  const_reverse_iterator rbegin() const {
    return const_reverse_iterator(lru_.rbegin());
  }
  const_reverse_iterator rend() const {
    return const_reverse_iterator(lru_.rend());
  }

  const_reverse_iterator crbegin() const {
    return const_reverse_iterator(lru_.crbegin());
  }
  const_reverse_iterator crend() const {
    return const_reverse_iterator(lru_.crend());
  }

 private:
  struct Node : public boost::intrusive::list_base_hook<
                    boost::intrusive::link_mode<boost::intrusive::safe_link>> {
    template <typename K>
    Node(const K& key, TValue&& value) : pr(key, std::move(value)) {}
    TPair pr;
  };
  using NodePtr = Node*;

  // NOTE: deriving from boost::intrusive::list is likely discouraged. This is
  // simply an alternative to an ugly explicit move operator for
  // EvictingCacheMap. Change to that if this derivation proves problematic.
  struct NodeList : public boost::intrusive::list<Node> {
    NodeList() {}
    NodeList& operator=(NodeList&& that) noexcept {
      // Clear the moved-from rather than swap, for consistency with NodeMap
      clear_nodes();
      // Now invoke base class move operator without using static_cast
      boost::intrusive::list<Node>& this_parent = *this;
      boost::intrusive::list<Node>&& that_parent = std::move(that);
      this_parent = std::move(that_parent);
      return *this;
    }
    NodeList(NodeList&& that) noexcept { *this = std::move(that); }
    ~NodeList() {
      // Adds leak-free final destruction to the intrusive container
      clear_nodes();
    }

   private:
    void clear_nodes() {
      boost::intrusive::list<Node>::clear_and_dispose(
          [](Node* ptr) { delete ptr; });
    }
  };

  struct KeyHasher {
    using is_transparent = void;
    using folly_is_avalanching = IsAvalanchingHasher<THash, TKey>;

    KeyHasher() : hash() {}
    KeyHasher(const THash& keyHash) : hash(keyHash) {}
    std::size_t operator()(const NodePtr& node) const {
      return hash(node->pr.first);
    }
    template <typename K>
    std::size_t operator()(const K& key) const {
      return hash(key);
    }
    THash hash;
  };

  struct KeyValueEqual {
    using is_transparent = void;

    KeyValueEqual() : equal() {}
    KeyValueEqual(const TKeyEqual& keyEqual) : equal(keyEqual) {}
    template <typename K>
    bool operator()(const K& lhs, const NodePtr& rhs) const {
      return equal(lhs, rhs->pr.first);
    }
    template <typename K>
    bool operator()(const NodePtr& lhs, const K& rhs) const {
      return equal(lhs->pr.first, rhs);
    }
    bool operator()(const NodePtr& lhs, const NodePtr& rhs) const {
      return equal(lhs->pr.first, rhs->pr.first);
    }
    TKeyEqual equal;
  };

  template <typename K>
  bool existsImpl(const K& key) const {
    return findInIndex(key) != nullptr;
  }

  template <typename K>
  TValue& getImpl(const K& key) {
    auto it = findImpl(*this, key);
    if (it == end()) {
      throw_exception<std::out_of_range>("Key does not exist");
    }
    return it->second;
  }

  template <typename Self>
  using self_iterator_t =
      std::conditional_t<std::is_const<Self>::value, const_iterator, iterator>;

  template <typename Self, typename K>
  static auto findImpl(Self& self, const K& key) {
    Node* ptr = self.findInIndex(key);
    if (!ptr) {
      return self.end();
    }
    self.lru_.splice(self.lru_.begin(), self.lru_, self.lru_.iterator_to(*ptr));
    return self_iterator_t<Self>(self.lru_.iterator_to(*ptr));
  }

  template <typename Self, typename K>
  static auto& getWithoutPromotionImpl(Self& self, const K& key) {
    auto it = self.findWithoutPromotion(key);
    if (it == self.end()) {
      throw_exception<std::out_of_range>("Key does not exist");
    }
    return it->second;
  }

  template <typename Self, typename K>
  static auto findWithoutPromotionImpl(Self& self, const K& key) {
    Node* ptr = self.findInIndex(key);
    return ptr ? self_iterator_t<Self>(self.lru_.iterator_to(*ptr))
               : self.end();
  }

  typename NodeList::iterator eraseImpl(
      Node* ptr,
      typename NodeList::const_iterator base_iter,
      PruneHookCall eraseHook) {
    std::unique_ptr<Node> node_owner(ptr);
    index_.erase(ptr);
    auto next_base_iter = lru_.erase(base_iter);
    if (eraseHook) {
      // NOTE: might throw, so we are in an exception-safe state
      eraseHook(ptr->pr.first, std::move(ptr->pr.second));
    }
    return next_base_iter;
  }

  template <typename K>
  bool eraseKeyImpl(const K& key, PruneHookCall eraseHook) {
    Node* ptr = findInIndex(key);
    if (ptr) {
      eraseImpl(ptr, lru_.iterator_to(*ptr), eraseHook);
      return true;
    }
    return false;
  }

  template <typename K>
  void setImpl(
      const K& key, TValue&& value, bool promote, PruneHookCall pruneHook) {
    Node* ptr = findInIndex(key);
    if (ptr) {
      ptr->pr.second = std::move(value);
      if (promote) {
        lru_.splice(lru_.begin(), lru_, lru_.iterator_to(*ptr));
      }
    } else {
      auto node = new Node(key, std::move(value));
      index_.insert(node);
      lru_.push_front(*node);

      // no evictions if maxSize_ is 0 i.e. unlimited capacity
      if (maxSize_ > 0 && size() > maxSize_) {
        prune(clearSize_, pruneHook);
      }
    }
  }

  template <typename K>
  auto insertImpl(const K& key, TValue&& value, PruneHookCall pruneHook) {
    auto node_owner = std::make_unique<Node>(key, std::move(value));
    Node* node = node_owner.get();
    {
      auto pair = index_.insert(node);
      if (!pair.second) {
        // No change. Abandon/destroy new node.
        return std::pair<iterator, bool>(lru_.iterator_to(**pair.first), false);
      }

      // upcoming prune might invalidate iterator
      assert(*pair.first == node);
    }

    // Complete insertion
    lru_.push_front(*node_owner.release());

    // no evictions if maxSize_ is 0 i.e. unlimited capacity
    if (maxSize_ > 0 && size() > maxSize_) {
      prune(clearSize_, pruneHook);
    }

    return std::pair<iterator, bool>(lru_.iterator_to(*node), true);
  }

  template <typename K>
  Node* findInIndex(const K& key) const {
    auto it = index_.find(key);
    if (it != index_.end()) {
      return *it;
    } else {
      return nullptr;
    }
  }

  PruneHookCall pruneHook_;
  KeyHasher keyHash_;
  KeyValueEqual keyEqual_;
  NodeMap index_;
  NodeList lru_;
  std::size_t maxSize_;
  std::size_t clearSize_;
};

} // namespace folly
