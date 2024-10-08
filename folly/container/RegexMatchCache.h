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

#include <chrono>
#include <iosfwd>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <folly/Chrono.h>
#include <folly/FBString.h>
#include <folly/Function.h>
#include <folly/container/F14Map.h>
#include <folly/container/F14Set.h>
#include <folly/container/span.h>

namespace folly {

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

 private:
  using regex_value = fbstring;
  using regex_pointer = fbstring const*;
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
  folly::F14NodeMap<regex_value, RegexToMatchEntry> cacheRegexToMatch_;

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
  class InspectView {
    friend RegexMatchCache;

   private:
    RegexMatchCache const& ref_;

    explicit InspectView(RegexMatchCache const& ref) noexcept : ref_{ref} {}

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
    using regex_pointer = RegexMatchCache::regex_pointer;
    using string_pointer = RegexMatchCache::string_pointer;

    ConsistencyReportMatcher();
    virtual ~ConsistencyReportMatcher();

    virtual bool match(regex_pointer regex, string_pointer string);
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

  std::vector<std::string_view> getRegexList() const;
  std::vector<string_pointer> getStringList() const;
  InspectView inspect() const noexcept { return InspectView{*this}; }
  void consistency(
      ConsistencyReportMatcher& crcache,
      FunctionRef<void(std::string)> report) const;

  bool hasRegex(std::string_view regex) const noexcept;
  void addRegex(std::string_view regex);
  void eraseRegex(std::string_view regex);

  bool hasString(string_pointer string) const noexcept;
  void addString(string_pointer string);
  void eraseString(string_pointer string);

  std::vector<string_pointer> findMatchesUncached(std::string_view regex) const;

  bool isReadyToFindMatches(std::string_view regex) const noexcept;
  void prepareToFindMatches(std::string_view regex);
  FindMatchesUnsafeResult findMatchesUnsafe(
      std::string_view regex, time_point now) const;
  std::vector<string_pointer> findMatches(
      std::string_view regex, time_point now) const;

  void clear();
  void purge(time_point expiry);
};

} // namespace folly
