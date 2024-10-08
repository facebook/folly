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

#include <array>
#include <chrono>
#include <iosfwd>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <folly/Chrono.h>
#include <folly/Function.h>
#include <folly/container/F14Map.h>
#include <folly/container/F14Set.h>
#include <folly/container/span.h>

namespace folly {

/// RegexMatchCacheKey
///
/// A key derived from a string. Used with RegexMatchCache.
///
/// Intended for use only with RegexMatchCache.
///
/// Incomplete as a generic facility.
class RegexMatchCacheKey {
 private:
  using self = RegexMatchCacheKey;

  static inline constexpr size_t data_size = 32;
  static inline constexpr size_t data_align = alignof(size_t);

  using data_type = std::array<unsigned char, data_size>;

  alignas(data_align) data_type const data_;

  static data_type init(std::string_view regex) noexcept;

  template <typename T, typename V = std::remove_cv_t<T>>
  static constexpr bool is_span_compatible(size_t const e) noexcept {
    return //
        !std::is_volatile_v<T> && //
        std::is_integral_v<V> && //
        std::is_unsigned_v<V> && //
        !std::is_same_v<bool, V> && //
        !std::is_same_v<char, V> && //
        alignof(V) <= data_align &&
        (e == data_size / sizeof(V) || e == dynamic_extent);
  }

 public:
  explicit RegexMatchCacheKey(std::string_view regex) noexcept
      : data_{init(regex)} {}

  template <
      typename T,
      std::size_t E,
      std::enable_if_t<is_span_compatible<T>(E), int> = 0>
  explicit operator span<T const, E>() const noexcept {
    return {reinterpret_cast<T const*>(data_.data()), E};
  }

  friend auto operator==(self const& a, self const& b) noexcept {
    return a.data_ == b.data_;
  }
  friend auto operator!=(self const& a, self const& b) noexcept {
    return a.data_ != b.data_;
  }
};

} // namespace folly

namespace std {

template <>
struct hash<::folly::RegexMatchCacheKey> {
  using folly_is_avalanching = std::true_type;

  size_t operator()(::folly::RegexMatchCacheKey const& key) const noexcept {
    return ::folly::span<size_t const>{key}[0];
  }
};

} // namespace std

namespace folly {

/// RegexMatchCacheKeyAndView
///
/// A composite key and view derived from a string. Used with RegexMatchCache.
///
/// Intended for use only with RegexMatchCache.
///
/// Incomplete as a generic facility.
class RegexMatchCacheKeyAndView {
 public:
  using regex_key = RegexMatchCacheKey;

  regex_key const key;
  std::string_view const view;

  explicit RegexMatchCacheKeyAndView(std::string_view regex) noexcept
      : key{regex}, view{regex} {}

  /* implicit */ operator RegexMatchCacheKey const&() const noexcept {
    return key;
  }
  /* implicit */ operator std::string_view const&() const noexcept {
    return view;
  }

 private:
  RegexMatchCacheKeyAndView(
      regex_key const& k, std::string_view const v) noexcept
      : key{k}, view{v} {}
};

/// RegexMatchCache
///
/// A cache around boost::regex_match(string, regex).
///
/// For efficiency, assumes several constraints and makes several guarantees.
///
/// The data structure owns regexes but does not own strings. The lifetimes of
/// all strings in the cache must surround their additions to the cache and
/// their subsequent removals from the cache or destruction of the cache.
///
/// The data structure is in two parts:
/// * A bidirectional match-cache contains all known matches.
/// * a bidirectional string-queue contains unknown, hypothetical matches.
///
/// Cached lookup operates only over the match-cache. When the string-queue for
/// a given regex is not empty, that regex is said to be uncoalesced. Cached
/// lookups are not permitted for an uncoalesced regex; that regex must first be
/// coalesced.
///
/// Addition of a string adds the string to the string-queue corresponding to
/// all known regexes. It does not perform any regex-match operations.
///
/// Addition and coalesce of a regex performs regex-matches for that regex only.
/// The string-queue for the given regex is removed and all elements matched
/// against the regex, and matching strings are added to the match-cache.
///
/// Lookup must follow a pattern like this:
///
///    if (!cache.isReadyToFindMatches(regex)) { // const
///      cache.prepareToFindMatches(regex); // non-const
///    }
///    auto matches = cache.findMatches(regex); // const
///
/// This is to support concurrent lookups, where the cache is protected by a
/// shared mutex.
///
/// The data structure is exception-safe in a sense. If an exception is thrown
/// within any non-const member function and escapes, the data structure may
/// purge all cached regexes while leaving all strings. In most such member
/// functions, only a memory-allocation failure would cause an exception to be
/// thrown. But in prepareToFindMatches, the provided regex may be syntactically
/// invalid and parsing it may throw, or it may be pathological and evaluating
/// it over a string may throw. In any event, the resolution is to clear out all
/// added regexes and to leave only the added strings. The reason is that this
/// resolution is simple and likely to be correct, while any other mechanism
/// would be complex and would be likely to have bugs.
class RegexMatchCache {
 public:
  using clock = folly::chrono::coarse_steady_clock;
  using time_point = clock::time_point;

  using regex_key = RegexMatchCacheKey;
  using regex_key_and_view = RegexMatchCacheKeyAndView;

 private:
  using regex_pointer = regex_key const*;
  using string_pointer = std::string const*;

  class RegexObject;

  struct RegexToMatchEntry : MoveOnly {
    mutable std::atomic<time_point> accessed_at{};

    folly::F14VectorSet<string_pointer> matches;
  };

  struct MatchToRegexEntry : MoveOnly {
    folly::F14VectorSet<regex_pointer> regexes;
  };

  struct StringQueueForwardEntry : MoveOnly {
    folly::F14VectorSet<regex_pointer> regexes;
  };

  struct StringQueueReverseEntry : MoveOnly {
    folly::F14VectorSet<string_pointer> strings;
  };

  /// cacheRegexToMatch_
  ///
  /// A match-cache map from regexes to the sets of matching strings.
  ///
  /// The set of matching strings for a given regex may be incomplete. This
  /// happens when strings are added to the universe but have not yet been
  /// coalesced for the given regex. The set of uncoalesced strings for a
  /// given regex is in stringQueueReverse_.
  ///
  /// For each regex, includes a last-accessed-at timestamp. This timestamp
  /// is used when purging old regexes from the cache, for the caller's own
  /// definition of old.
  folly::F14NodeMap<regex_key, RegexToMatchEntry> cacheRegexToMatch_;

  /// cacheMatchToRegex_
  ///
  /// A match-cache map from strings to the sets of matching regexes.
  ///
  /// The set of matching regexes for a given string may be incomplete. This
  /// happens when strings are added to the universe but have not yet been
  /// coalesced for all regexes in the universe. The set of regexes for which
  /// a given string has not yet been coalesced is in stringQueueForward_.
  folly::F14FastMap<string_pointer, MatchToRegexEntry> cacheMatchToRegex_;

  /// stringQueueForward_
  ///
  /// A pending-coalesce map from strings to regexes for which the strings have
  /// not yet been coalesced, that is, for which it is not yet known that the
  /// strings do or do not match the given regexes.
  ///
  /// In a steady-state when all strings have been coalesced for all regexes,
  /// this map would be empty.
  folly::F14FastMap<string_pointer, StringQueueForwardEntry>
      stringQueueForward_;

  /// stringQueueReverse_
  ///
  /// A pending-coalesce map from regexes to strings which have not yet been
  /// coalesced for the given regex, that is, for which it is not yet known that
  /// the strings do or do not match the given regexes.
  ///
  /// In a steady-state when all strings have been coalesced for all regexes,
  /// this map would be empty.
  folly::F14FastMap<regex_pointer, StringQueueReverseEntry> stringQueueReverse_;

  void repair() noexcept;

 public:
  class KeyMap {
   public:
    using regex_key = RegexMatchCacheKey;
    using regex_key_and_view = RegexMatchCacheKeyAndView;

    virtual ~KeyMap() = 0;

    virtual std::string_view lookup(regex_key const& regex) const = 0;
  };

  class InspectView {
    friend RegexMatchCache;

   private:
    RegexMatchCache const& ref_;
    KeyMap const& keys_;

    explicit InspectView(
        RegexMatchCache const& ref, KeyMap const& keys) noexcept
        : ref_{ref}, keys_{keys} {}

    void print(std::ostream& o) const;

   public:
    friend std::ostream& operator<<(std::ostream& o, InspectView const view) {
      return (view.print(o), o);
    }
  };

  class ConsistencyReportMatcher {
   private:
    struct state;
    std::unique_ptr<state> state_;

   public:
    using regex_key = RegexMatchCache::regex_key;
    using regex_key_and_view = RegexMatchCache::regex_key_and_view;
    using string_pointer = RegexMatchCache::string_pointer;

    ConsistencyReportMatcher();
    virtual ~ConsistencyReportMatcher();

    virtual bool match(
        KeyMap const& keys, regex_key regex, string_pointer string);
  };

  class FindMatchesUnsafeResult {
   private:
    friend class RegexMatchCache;

    using map_t = folly::F14VectorSet<string_pointer>;

    map_t const& matches_;

    /* implicit */ FindMatchesUnsafeResult(map_t const& matches) noexcept
        : matches_{matches} {}

   public:
    using value_type = map_t::value_type;

    auto size() const noexcept { return matches_.size(); }
    auto begin() const noexcept { return matches_.begin(); }
    auto end() const noexcept { return matches_.end(); }
  };

  RegexMatchCache() noexcept;
  ~RegexMatchCache();

  std::vector<std::string_view> getRegexList(KeyMap const& keys) const;
  std::vector<string_pointer> getStringList() const;
  InspectView inspect(KeyMap const& keys) const noexcept {
    return InspectView{*this, keys};
  }
  void consistency(
      ConsistencyReportMatcher& crcache,
      KeyMap const& keys,
      FunctionRef<void(std::string)> report) const;

  bool hasRegex(regex_key const& regex) const noexcept;
  void addRegex(regex_key const& regex);
  void eraseRegex(regex_key const& regex);

  bool hasString(string_pointer string) const noexcept;
  void addString(string_pointer string);
  void eraseString(string_pointer string);

  std::vector<string_pointer> findMatchesUncached(std::string_view regex) const;

  bool isReadyToFindMatches(regex_key const& regex) const noexcept;
  void prepareToFindMatches(regex_key_and_view const& regex);
  FindMatchesUnsafeResult findMatchesUnsafe(
      regex_key const& regex, time_point now) const;
  std::vector<string_pointer> findMatches(
      regex_key const& regex, time_point now) const;

  void clear();
  void purge(time_point expiry);
};

} // namespace folly
