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

#include <new>
#include <type_traits>
#include <folly/container/EvictingCacheMap.h>

namespace folly {

/**
 * A variant of EvictingCacheMap that assigns weights to entries and
 * evicts entries in LRU order to ensure the total weight of all entries
 * stays below some set maximum. ImplicitlyWeighted means this variant
 * derives the weights from the key-values using a chosen function. TWeightFn
 * must be a type implementing `size_t operator()(const TKey&, const Tvalue&)`
 *
 * Example usage: if TKey and TValue are std::string, the weight could be the
 * sum of the string sizes (already stored in the key and value) so that the
 * total weight approximates the total memory usage.
 *
 * Also consider WeightedEvictingCacheMap below, which tracks weights
 * explicitly, along with keys and values.
 *
 * TValue must be either movable or copyable. TKey must be copyable.
 *
 * IMPORTANT NOTES:
 * * Returned references, pointers, or iterators are potentially invalid
 * after any pruning operation (set, insert, etc. that might increase total
 * weight), or any set/insert on the same key (which are allowed to create a
 * new entry or modify an existing entry becoming obsolete).
 * * Operations are more restrictive than EvictingCacheMap to reduce the
 * risk of modifying a value in a way that changes its weight without proper
 * tracking. TValue can be a const type if appropriate.
 * * This is NOT a thread-safe structure.
 * * For simplicity, functions taking a key implicitly inherit type
 * constraints from EvictingCacheMap. (Must either match TKey or
 * EligibleForHeterogeneousFind/Insert.)
 *
 * This implementation has not been highly optimized and is a wrapper around
 * EvictingCacheMap.
 */
template <
    class TKey,
    class TValue,
    class TWeightFn,
    class THash = HeterogeneousAccessHash<TKey>,
    class TKeyEqual = HeterogeneousAccessEqualTo<TKey>>
class ImplicitlyWeightedEvictingCacheMap {
 private: // typedefs
  using ECM = EvictingCacheMap<TKey, TValue, THash, TKeyEqual>;

 public:
  using PruneHookCall = std::function<void(TKey, TValue&&)>;

  explicit ImplicitlyWeightedEvictingCacheMap(
      std::size_t maxTotalWeight,
      const TWeightFn& weightFn = TWeightFn(),
      const THash& keyHash = THash(),
      const TKeyEqual& keyEqual = TKeyEqual())
      : ecm_(/* no max size*/ 0, 1, keyHash, keyEqual),
        weightFn_(weightFn),
        maxTotalWeight_(maxTotalWeight),
        currentTotalWeight_(0) {
    setupPruneHook();
  }

  // Like EvictingCacheMap
  ImplicitlyWeightedEvictingCacheMap(
      const ImplicitlyWeightedEvictingCacheMap&) = delete;
  ImplicitlyWeightedEvictingCacheMap& operator=(
      const ImplicitlyWeightedEvictingCacheMap&) = delete;
  ImplicitlyWeightedEvictingCacheMap& operator=(
      ImplicitlyWeightedEvictingCacheMap&& that) {
    // Put moved-from in a valid but empty state
    ecm_ = std::move(that.ecm_);
    that.ecm_.clear();
    weightFn_ = std::move(that.weightFn_);
    that.weightFn_ = TWeightFn();
    maxTotalWeight_ = std::move(that.maxTotalWeight_);
    that.maxTotalWeight_ = 0;
    currentTotalWeight_ = std::move(that.currentTotalWeight_);
    that.currentTotalWeight_ = 0;
    // Set prune hook for this 'this' (not that this)
    setupPruneHook();
    return *this;
  }
  ImplicitlyWeightedEvictingCacheMap(ImplicitlyWeightedEvictingCacheMap&& that)
      : ecm_(/* no max size*/ 0) {
    *this = std::move(that);
  }

  ~ImplicitlyWeightedEvictingCacheMap() {
#ifndef NDEBUG
    // Verify remaining total weight is properly tracked. This can break if
    // improperly mutating the TValues that changes the result of TWeightFn.
    std::size_t actual_total_weight = 0;
    for (auto& e : ecm_) {
      actual_total_weight += weightFn_(e.first, e.second);
    }
    assert(actual_total_weight == currentTotalWeight_);
#endif
  }

  static constexpr std::size_t kApproximateEntryMemUsage =
      ECM::kApproximateEntryMemUsage;

  // iterators. NOTE: mutable iterators not included because of challenges and
  // confusion over writes affecting the implicit weights and whether entries
  // are promoted or potentially invalidated due to pruning.
  using const_iterator = typename ECM::const_iterator;
  using const_reverse_iterator = typename ECM::const_reverse_iterator;

  const_iterator begin() const { return ecm_.begin(); }
  const_iterator end() const { return ecm_.end(); }

  const_reverse_iterator rbegin() const { return ecm_.rbegin(); }
  const_reverse_iterator rend() const { return ecm_.rend(); }

  /**
   * Get the number of elements in the dictionary
   * @return the size of the dictionary
   */
  std::size_t size() const { return ecm_.size(); }

  /**
   * Typical empty function
   * @return true if empty, false otherwise
   */
  bool empty() const { return ecm_.empty(); }

  /**
   * Returns total weight of all entries currently in the cache.
   */
  std::size_t getCurrentTotalWeight() const { return currentTotalWeight_; }

  /**
   * Returns the maximum allowed total weight of all entries in
   * the cache.
   */
  std::size_t getMaxTotalWeight() const { return maxTotalWeight_; }

  /**
   * Sets the maximum allowed total weight of all entries in
   * the cache, evicting entries as needed for the new limit.
   */
  void setMaxTotalWeight(std::size_t newMaxTotalWeight) {
    maxTotalWeight_ = newMaxTotalWeight;
    pruneToMaxTotalWeight();
  }

  /**
   * Check for existence of a specific key in the map.  This operation has
   *     no effect on LRU order.
   * @param key key to search for
   * @return true if exists, false otherwise
   */
  template <typename K>
  bool exists(const K& key) const {
    return ecm_.exists(key);
  }

  /**
   * Get the value associated with a specific key.  This function always
   * promotes a found value to the head of the LRU. The returned reference
   * is const and might be inavlidated by many subsequent operations. See
   * IMPORTANT NOTES above. See also replace().
   *
   * @param key key to search for
   * @return the value if it exists
   * @throw std::out_of_range exception of the key does not exist
   */
  template <typename K>
  const TValue& get(const K& key) {
    return ecm_.get(key);
  }

  // Same but without LRU promotion
  template <typename K>
  const TValue& getWithoutPromotion(const K& key) const {
    return ecm_.getWithoutPromotion(key);
  }

  /**
   * Set a key-value pair in the dictionary with a given weight. If its weight
   * is more than maxTotalWeight, the entry is inserted anyway and all other
   * entries are evicted, so that get() after set() always succeeds. The
   * structure can be temporarily over max weight until the next modification.
   * The new or modified entry is always inserted at or promoted to the head
   * of the LRU.
   *
   * @param key key to associate with value
   * @param value value to associate with the key
   */
  template <typename K>
  void set(const K& key, TValue&& value) {
    std::size_t new_weight = weightFn_(key, value);
    std::size_t old_weight = 0;
    auto it = find(key); // Does promotion
    if (it != end()) {
      using TMutableValue = std::remove_const_t<TValue>;

      // Existing entry
      old_weight = weightFn_(it->first, it->second);
      auto ptr = const_cast<TMutableValue*>(&it->second);
      // Overwrite
      // Would be this, but need to work around
      // const values, which don't allow move-assignment:
      //   it->second = std::move(value);
      // Below hack is OK because we reserve the right to "entirely new
      // entry" semantics for set() (invalidate pointers/iterators/etc.) but
      // optimize with "replace in place" implementation.
      ptr->~TValue();
      new (ptr) TValue(std::move(value));
    } else {
      // No existing entry
      ecm_.insert(key, std::move(value));
    }
    // Protect the entry we just put at the head of the LRU
    entryWeightUpdated(old_weight, new_weight, /*protect_one*/ true);
  }

  template <typename K>
  void set(const K& key, const TValue& value) {
    TValue tmp{value}; // can't yet rely on C++17 temporary materialization
    set(key, std::move(tmp));
  }

  // TODO: insert() functions, which would refuse insert if new entry weight
  // exceeds max (or existing entry for key)
  // template <typename K>
  // std::pair<const_iterator, bool> insert(const K& key, TValue&& value) {}

  /**
   * Erases any entry with given key or iterator.
   *
   * @param key_or_it key to search for or iterator
   */
  template <typename KI>
  void erase(const KI& key_or_it) {
    // Use prune hook as erase hook to keep total weight updated
    ecm_.erase(key_or_it, ecm_.getPruneHook());
  }

  /**
   * Get the iterator associated with a specific key.  This function always
   * promotes a found value to the head of the LRU. See IMPORTANT NOTES above
   * for why this only returns a const_iterator. See also replace().
   * @param key key to associate with value
   * @return the const_iterator of the object (a std::pair of const TKey,
   *     TValue) or end() if it does not exist
   */
  template <typename K>
  const_iterator find(const K& key) {
    return const_iterator(ecm_.find(key).base());
  }

  // Same but without LRU promotion
  template <typename K>
  const_iterator findWithoutPromotion(const K& key) const {
    return ecm_.findWithoutPromotion(key);
  }

  /**
   * Replace the value associated with an entry from a const_iterator, in a
   * safe way that tracks any modification to the weight. Entries are evicted
   * so that max total weight it not exceeded, except this function protects
   * the entry at the head of the LRU list from eviction. Thus, for find()
   * immediately followed by replace(), the modified entry is protected
   * against eviction even if exceeding max total weight (like set(), iterator
   * remains valid). The iterator also remains valid if the weight does not
   * increase. Otherwise, the iterator is potentially invalidated by eviction
   * during this operation.
   *
   * If TValue is a const type, this function is invalid.
   * @param it const_iterator for the entry to modify, which must come from
   * this cache map
   * @param value replacement value
   */
  void replace(const_iterator it, TValue&& value) {
    assert(it != end());
    size_t old_weight = weightFn_(it->first, it->second);
    size_t new_weight = weightFn_(it->first, value);
    // Overwrite in place
    const_cast<TValue&>(it->second) = std::move(value);
    // Evict as needed (possibly including this entry, unless it's LRU head)
    entryWeightUpdated(old_weight, new_weight, /*protect_one*/ true);
  }

  void replace(const_iterator it, const TValue& value) {
    TValue tmp{value}; // can't yet rely on C++17 temporary materialization
    replace(it, std::move(tmp));
  }

  /**
   * Clear the cache to an empty state.
   */
  void clear() {
    ecm_.clear();
    assert(currentTotalWeight_ == 0);
  }

  /**
   * Set the prune hook, which is the function invoked on the key and value
   *     on each eviction. An operation will throw if the pruneHook throws.
   *     Note that this prune hook is not automatically called on entries
   *     explicitly erase()ed nor on remaining entries at destruction time.
   * @param pruneHook eviction callback to set as default, or nullptr to clear
   */
  void setPruneHook(PruneHookCall pruneHook) { pruneHook_ = pruneHook; }

 private: // fns
  void setupPruneHook() {
    ecm_.setPruneHook([this](const TKey& key, TValue&& value) {
      std::size_t weight = weightFn_(key, value);
      assert(currentTotalWeight_ >= weight);
      currentTotalWeight_ -= weight;
      if (pruneHook_) {
        pruneHook_(key, std::move(value));
      }
    });
  }

  void entryWeightUpdated(
      std::size_t old_weight,
      std::size_t new_weight,
      bool protect_one = false) {
    assert(old_weight <= currentTotalWeight_);
    currentTotalWeight_ += new_weight - old_weight;
    pruneToMaxTotalWeight(protect_one);
  }

  void pruneToMaxTotalWeight(bool protect_one = false) {
    // NOTE: Avoid infinite loop even in the case of weight tracking bug
    size_t min_count = protect_one ? 1 : 0;
    while (currentTotalWeight_ > maxTotalWeight_ && ecm_.size() > min_count) {
      ecm_.prune(1);
    }
  }

  template <class _TKey, class _TValue, class _THash, class _TKeyEqual>
  friend class WeightedEvictingCacheMap;

 private: // data
  PruneHookCall pruneHook_;
  ECM ecm_;
  TWeightFn weightFn_;
  std::size_t maxTotalWeight_;
  std::size_t currentTotalWeight_;
};

/**
 * A variant of EvictingCacheMap that tracks weights for entries and
 * evicts entries in LRU order to ensure the total weight of all entries
 * stays below some set maximum. Weights are stored as a size_t with each
 * entry.
 *
 * Example usage: if TKey is std::string and TValue is some large, complex
 * object type, the weight could be the estimated memory size of the key
 * and complex object. Tracking the weight explicitly minimizes costly
 * recomputation of the estimated memory size. Thus, the total weight of all
 * entries approximates the total memory usage.
 *
 * TValue must be either movable or copyable. TKey must be copyable.
 *
 * IMPORTANT NOTES:
 * * Returned references, pointers, or iterators are potentially invalid
 * after any pruning operation (set, insert, etc. that might increase total
 * weight), or any set/insert on the same key (which are allowed to create a
 * new entry or modify an existing entry becoming obsolete).
 * * This is NOT a thread-safe structure.
 * * For simplicity, functions taking a key implicitly inherit type
 * constraints from EvictingCacheMap. (Must either match TKey or
 * EligibleForHeterogeneousFind/Insert.)
 *
 * This implementation has not been highly optimized.
 */
template <
    class TKey,
    class TValue,
    class THash = HeterogeneousAccessHash<TKey>,
    class TKeyEqual = HeterogeneousAccessEqualTo<TKey>>
class WeightedEvictingCacheMap {
 public: // types
  struct ValueAndWeight {
    /*implicit*/ ValueAndWeight(TValue&& _value, std::size_t _weight)
        : value(std::move(_value)), weight(_weight) {}
    // Value is mutable through non-const iterator
    TValue value;
    // Weight is not mutable to ensure proper tracking. See updateWeight().
    const std::size_t weight;
  };

 private: // types
  struct WeightFn {
    std::size_t operator()(const TKey& /*key*/, const ValueAndWeight& p) {
      return p.weight;
    }
  };
  using IWECM = ImplicitlyWeightedEvictingCacheMap<
      TKey,
      ValueAndWeight,
      WeightFn,
      THash,
      TKeyEqual>;

 public:
  using PruneHookCall = std::function<void(TKey, TValue&&, size_t)>;

  explicit WeightedEvictingCacheMap(
      std::size_t maxTotalWeight,
      const THash& keyHash = THash(),
      const TKeyEqual& keyEqual = TKeyEqual())
      : iwecm_(maxTotalWeight, WeightFn(), keyHash, keyEqual) {}

  // Like EvictingCacheMap
  WeightedEvictingCacheMap(const WeightedEvictingCacheMap&) = delete;
  WeightedEvictingCacheMap& operator=(const WeightedEvictingCacheMap&) = delete;
  WeightedEvictingCacheMap(WeightedEvictingCacheMap&&) = default;
  WeightedEvictingCacheMap& operator=(WeightedEvictingCacheMap&&) = default;

  static constexpr std::size_t kApproximateEntryMemUsage =
      IWECM::kApproximateEntryMemUsage;

  // iterators that dereference to ValueAndWeight
  using iterator = typename IWECM::ECM::iterator;
  using reverse_iterator = typename IWECM::ECM::reverse_iterator;
  using const_iterator = typename IWECM::const_iterator;
  using const_reverse_iterator = typename IWECM::const_reverse_iterator;

  iterator begin() { return iwecm_.ecm_.begin(); }
  iterator end() { return iwecm_.ecm_.end(); }

  const_iterator begin() const { return cbegin(); }
  const_iterator end() const { return cend(); }

  const_iterator cbegin() const { return iwecm_.begin(); }
  const_iterator cend() const { return iwecm_.end(); }

  reverse_iterator rbegin() { return iwecm_.ecm_.rbegin(); }
  reverse_iterator rend() { return iwecm_.ecm_.rend(); }

  const_reverse_iterator rbegin() const { return crbegin(); }
  const_reverse_iterator rend() const { return crend(); }

  const_reverse_iterator crbegin() const { return iwecm_.rbegin(); }
  const_reverse_iterator crend() const { return iwecm_.rend(); }

  /**
   * Get the number of elements in the dictionary
   * @return the size of the dictionary
   */
  std::size_t size() const { return iwecm_.size(); }

  /**
   * Typical empty function
   * @return true if empty, false otherwise
   */
  bool empty() const { return iwecm_.empty(); }

  /**
   * Returns total weight of all entries currently in the cache.
   */
  std::size_t getCurrentTotalWeight() const {
    return iwecm_.getCurrentTotalWeight();
  }

  /**
   * Returns the maximum allowed total weight of all entries in
   * the cache.
   */
  std::size_t getMaxTotalWeight() const { return iwecm_.getMaxTotalWeight(); }

  /**
   * Sets the maximum allowed total weight of all entries in
   * the cache, evicting entries as needed for the new limit.
   */
  void setMaxTotalWeight(std::size_t newMaxTotalWeight) {
    iwecm_.setMaxTotalWeight(newMaxTotalWeight);
  }

  /**
   * Check for existence of a specific key in the map.  This operation has
   *     no effect on LRU order.
   * @param key key to search for
   * @return true if exists, false otherwise
   */
  template <typename K>
  bool exists(const K& key) const {
    return iwecm_.exists(key);
  }

  /**
   * Get the value associated with a specific key.  This function always
   * promotes a found value to the head of the LRU. The TValue can be
   * modified in place through the reference, keeping in mind the reference
   * can easily be invalidated (IMPORTANT NOTES above).
   * @param key key to search for
   * @return the value if it exists
   * @throw std::out_of_range exception of the key does not exist
   */
  template <typename K>
  TValue& get(const K& key) {
    return const_cast<TValue&>(iwecm_.get(key).value);
  }
  // Same but without LRU promotion
  template <typename K>
  TValue& getWithoutPromotion(const K& key) {
    return const_cast<TValue&>(iwecm_.getWithoutPromotion(key).value);
  }
  template <typename K>
  const TValue& getWithoutPromotion(const K& key) const {
    return iwecm_.getWithoutPromotion(key).value;
  }

  /**
   * Set a key-value pair in the dictionary with a given weight. If its weight
   * is more than maxTotalWeight, the entry is inserted anyway and all other
   * entries are evicted, so that get() after set() always succeeds. The
   * structure can be temporarily over max weight until the next modification.
   * The new or modified entry is always inserted at or promoted to the head
   * of the LRU.
   *
   * @param key key to associate with value
   * @param value value to associate with the key
   * @param weight weight to associate with the key-value entry
   */
  template <typename K>
  void set(const K& key, TValue&& value, std::size_t weight) {
    ValueAndWeight tmp{std::move(value), weight};
    return iwecm_.set(key, std::move(tmp));
  }

  template <typename K>
  void set(const K& key, const TValue& value, std::size_t weight) {
    TValue tmp{value}; // can't yet rely on C++17 temporary materialization
    return set(key, std::move(tmp), weight);
  }

  // TODO: insert() functions, which would refuse insert if new entry weight
  // exceeds max (or existing entry for key)
  // template <typename K>
  // std::pair<const_iterator, bool> insert(const K& key, TValue&& value) {}

  /**
   * Erases any entry with given key or iterator.
   *
   * @param key_or_it key to search for or iterator
   */
  template <typename KI>
  void erase(const KI& key_or_it) {
    iwecm_.erase(key_or_it);
  }

  /**
   * Get the iterator associated with a specific key. This function always
   * promotes a found value to the head of the LRU. Although values can be
   * modified through iterators, weights are const. See updateWeight().
   * @param key key to associate with value
   * @return the iterator of std::pair<const TKey, ValueAndWeight>, or
   *     end() if it does not exist
   */
  template <typename K>
  iterator find(const K& key) {
    return iwecm_.ecm_.find(key);
  }

  // Same but without LRU promotion
  template <typename K>
  iterator findWithoutPromotion(const K& key) {
    return iwecm_.ecm_.findWithoutPromotion(key);
  }
  template <typename K>
  const_iterator findWithoutPromotion(const K& key) const {
    return iwecm_.findWithoutPromotion(key);
  }

  /**
   * Overwrite the weight associated with a specific entry. As usual, entries
   * are evicted so that max total weight it not exceeded, except this
   * function protects the entry at the head of the LRU list from eviction.
   * Thus, for find() immediately followed by updateWeight(), the modified
   * entry is protected against eviction even if exceeding max total weight
   * (like set(), iterator remains valid). The iterator also remains valid if
   * the weight does not increase. Otherwise, the iterator is potentially
   * invalidated by eviction during this operation.
   *
   * @param it iterator for the entry to modify, which must come from
   * this cache map
   * @param new_weight the updated weight to assign
   */
  void updateWeight(const_iterator it, std::size_t new_weight) {
    updateWeightImpl(it, new_weight);
  }
  void updateWeight(iterator it, std::size_t new_weight) {
    updateWeightImpl(it, new_weight);
  }

  /**
   * Clear the cache to an empty state.
   */
  void clear() { iwecm_.clear(); }

  /**
   * Set the prune hook, which is the function invoked on the key and value
   *     on each eviction. An operation will throw if the pruneHook throws.
   *     Note that this prune hook is not automatically called on entries
   *     explicitly erase()ed nor on remaining entries at destruction time.
   * @param pruneHook eviction callback to set as default, or nullptr to clear
   */
  void setPruneHook(PruneHookCall pruneHook) {
    iwecm_.setPruneHook([pruneHook = std::move(pruneHook)](
                            const TKey& key, ValueAndWeight&& valueAndWeight) {
      if (!pruneHook) {
        return;
      }
      pruneHook(key, std::move(valueAndWeight.value), valueAndWeight.weight);
    });
  }

 private:
  // Like IWECM::replace
  template <typename It>
  void updateWeightImpl(It it, std::size_t new_weight) {
    assert(it != end());
    size_t old_weight = it->second.weight;
    // Overwrite in place
    const_cast<std::size_t&>(it->second.weight) = new_weight;
    // Evict as needed (possibly including this entry, unless it's LRU head)
    iwecm_.entryWeightUpdated(old_weight, new_weight, /*protect_one*/ true);
  }

  PruneHookCall pruneHook_;
  IWECM iwecm_;
};

} // namespace folly
