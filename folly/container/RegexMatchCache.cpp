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

#include <folly/container/RegexMatchCache.h>

#include <folly/portability/Windows.h>

#include <ostream>

#include <boost/regex.hpp>
#include <fmt/format.h>
#include <glog/logging.h>

#include <folly/MapUtil.h>
#include <folly/String.h>
#include <folly/container/Reserve.h>
#include <folly/ssl/OpenSSLHash.h>
#include <folly/synchronization/AtomicUtil.h>

namespace folly {

static std::string quote(std::string_view const s) {
  return fmt::format("\"{}\"", cEscape<std::string>(s));
}

RegexMatchCacheKey::data_type RegexMatchCacheKey::init(
    std::string_view const regex) noexcept {
  data_type data;
  folly::ssl::OpenSSLHash::sha256(range(data), StringPiece(regex));
  return data;
}

class RegexMatchCache::RegexObject {
 private:
  boost::regex object;

 public:
  explicit RegexObject(std::string_view const regex)
      : object{std::string(regex)} {}

  bool operator()(std::string_view const string) const {
    return boost::regex_match(std::string(string), object);
  }
};

void RegexMatchCache::repair() noexcept {
  stringQueueReverse_.clear();
  stringQueueForward_.clear();
  for (auto& [match, entry] : cacheMatchToRegex_) {
    entry.regexes.reset();
  }
  cacheRegexToMatch_.clear();
  regexVector_.clear();
}

RegexMatchCache::KeyMap::~KeyMap() = default;

void RegexMatchCache::InspectView::print(std::ostream& o) const {
  auto regexToString = [&](auto const& regex) {
    return quote(keys_.lookup(regex));
  };
  o << "cache-regex-to-match[" << ref_.cacheRegexToMatch_.size()
    << "]:" << std::endl;
  for (auto const& [regex, entry] : ref_.cacheRegexToMatch_) {
    o << "  " << regexToString(regex) << ":" << std::endl;
    for (auto const match : entry.matches) {
      o << "    " << quote(*match) << std::endl;
    }
  }
  o << "cache-match-to-regex[" << ref_.cacheMatchToRegex_.size()
    << "]:" << std::endl;
  for (auto const& [match, entry] : ref_.cacheMatchToRegex_) {
    o << "  " << quote(*match) << ":" << std::endl;
    for (auto const regexi : entry.regexes.as_index_set_view()) {
      auto const regex = ref_.regexVector_.value_at_index(regexi);
      o << "    " << regexToString(*regex) << std::endl;
    }
  }
  o << "string-queue-forward[" << ref_.stringQueueForward_.size()
    << "]:" << std::endl;
  for (auto const& [string, entry] : ref_.stringQueueForward_) {
    o << "  " << quote(*string) << ":" << std::endl;
    for (auto const regexi : entry.regexes.as_index_set_view()) {
      auto const regex = ref_.regexVector_.value_at_index(regexi);
      o << "    " << regexToString(*regex) << std::endl;
    }
  }
  o << "string-queue-reverse[" << ref_.stringQueueReverse_.size()
    << "]:" << std::endl;
  for (auto const& [regex, entry] : ref_.stringQueueReverse_) {
    o << "  " << regexToString(*regex) << ":" << std::endl;
    for (auto const string : entry.strings) {
      o << "    " << quote(*string) << std::endl;
    }
  }
}

struct RegexMatchCache::ConsistencyReportMatcher::state {
  std::unordered_map<regex_key, RegexObject> cache;
};

RegexMatchCache::ConsistencyReportMatcher::ConsistencyReportMatcher()
    : state_{std::make_unique<state>()} {}

RegexMatchCache::ConsistencyReportMatcher::~ConsistencyReportMatcher() =
    default;

bool RegexMatchCache::ConsistencyReportMatcher::match(
    KeyMap const& keys, regex_key const regex, string_pointer const string) {
  auto const [iter, inserted] =
      state_->cache.try_emplace(regex, keys.lookup(regex));
  return iter->second(*string);
}

RegexMatchCache::RegexMatchCache() noexcept = default;

RegexMatchCache::~RegexMatchCache() = default;

std::vector<std::string_view> RegexMatchCache::getRegexList(
    KeyMap const& keys) const {
  std::vector<std::string_view> result;
  result.reserve(cacheRegexToMatch_.size());
  for (auto const& [regex, entry] : cacheRegexToMatch_) {
    result.push_back(keys.lookup(regex));
  }
  return result;
}

std::vector<RegexMatchCache::string_pointer> RegexMatchCache::getStringList()
    const {
  std::vector<string_pointer> result;
  result.reserve(cacheMatchToRegex_.size());
  for (auto const& [match, entry] : cacheMatchToRegex_) {
    result.push_back(match);
  }
  return result;
}

void RegexMatchCache::consistency(
    ConsistencyReportMatcher& matcher,
    KeyMap const& keys,
    FunctionRef<void(std::string)> const report) const {
  auto const q = [](std::string_view const s) { return quote(s); };
  auto const h = report;

  auto const r = [&](regex_key const r) { return keys.lookup(r); };

  if (cacheRegexToMatch_.empty() || cacheMatchToRegex_.empty()) {
    if (!stringQueueForward_.empty()) {
      h("string-queue-forward not empty");
    }
    if (!stringQueueReverse_.empty()) {
      h("string-queue-reverse not empty");
    }
  }

  //  check that caches are accurate
  //  check that caches are bidi-consistent
  //  check that missing cache entries are found in string-queues
  for (auto const& [regex, rtmentry] : cacheRegexToMatch_) {
    auto const regexs = r(regex);
    auto const regexi = regexVector_.index_of_value(&regex);
    for (auto const& [match, mtrentry] : cacheMatchToRegex_) {
      auto const rtmcontains = rtmentry.matches.count(match);
      auto const mtrcontains = mtrentry.regexes.get_value(regexi);
      if (rtmcontains && !mtrcontains) {
        h(fmt::format( //
            "cache-regex-to-match[{}] wild {}",
            q(regexs),
            q(*match)));
      }
      if (mtrcontains && !rtmcontains) {
        h(fmt::format( //
            "cache-match-to-regex[{}] wild {}",
            q(*match),
            q(regexs)));
      }
      auto const result = matcher.match(keys, regex, match);
      auto const queues = result && (!rtmcontains || !mtrcontains);
      auto const sqfptr =
          !queues ? nullptr : get_ptr(stringQueueForward_, match);
      auto const sqfhas = sqfptr && sqfptr->regexes.get_value(regexi);
      auto const sqrptr =
          !queues ? nullptr : get_ptr(stringQueueReverse_, &regex);
      auto const sqrhas = sqrptr && sqrptr->strings.contains(match);
      if (rtmcontains && !result) {
        h(fmt::format( //
            "cache-regex-to-match[{}] wild {}",
            q(regexs),
            q(*match)));
      }
      if (result && !rtmcontains) {
        if (!sqfhas || !sqrhas) {
          h(fmt::format( //
              "cache-regex-to-match[{}] missing {}",
              q(regexs),
              q(*match)));
        }
      }
      if (mtrcontains && !result) {
        h(fmt::format( //
            "cache-match-to-regex[{}] wild {}",
            q(*match),
            q(regexs)));
      }
      if (result && !mtrcontains) {
        if (!sqfhas || !sqrhas) {
          h(fmt::format( //
              "cache-match-to-regex[{}] missing {}",
              q(*match),
              q(regexs)));
        }
      }
    }
  }

  //  check that string-queues are bidi-consistent
  //  check that string-queue keys are subsets of caches
  //  check that string-queue entries are not in caches
  for (auto const& [string, entry] : stringQueueForward_) {
    auto const mtrptr = get_ptr(cacheMatchToRegex_, string);
    if (!mtrptr) {
      h(fmt::format( //
          "string-queue-forward has string[{}]",
          q(*string)));
    }
    for (auto const regexi : entry.regexes.as_index_set_view()) {
      auto const regex = regexVector_.value_at_index(regexi);
      auto const sqrptr = get_ptr(stringQueueReverse_, regex);
      if (!sqrptr) {
        h(fmt::format( //
            "string-queue-reverse none regex[{}]",
            q(r(*regex))));
      } else if (!sqrptr->strings.count(string)) {
        h(fmt::format( //
            "string-queue-reverse[{}] none string[{}]",
            q(r(*regex)),
            q(*string)));
      }
      auto const mtrhas = mtrptr && mtrptr->regexes.get_value(regexi);
      auto const rtmptr = get_ptr(cacheRegexToMatch_, *regex);
      auto const rtmhas = rtmptr && rtmptr->matches.count(string);
      if (mtrhas || rtmhas) {
        h(fmt::format( //
            "string-queue-forward[{}] has regex[{}]",
            q(*string),
            q(r(*regex))));
      }
    }
  }
  for (auto const& [regex, entry] : stringQueueReverse_) {
    auto const regexi = regexVector_.index_of_value(regex);
    auto const rtmptr = get_ptr(cacheRegexToMatch_, *regex);
    for (auto const string : entry.strings) {
      auto const sqfptr = get_ptr(stringQueueForward_, string);
      if (!sqfptr) {
        h(fmt::format( //
            "string-queue-forward none string[{}]",
            q(*string)));
      } else if (!sqfptr->regexes.get_value(regexi)) {
        h(fmt::format( //
            "string-queue-forward[{}] none regex[{}]",
            q(*string),
            q(r(*regex))));
      }
      auto const mtrptr = get_ptr(cacheMatchToRegex_, string);
      auto const mtrhas = mtrptr && mtrptr->regexes.get_value(regexi);
      auto const rtmhas = rtmptr && rtmptr->matches.count(string);
      if (mtrhas || rtmhas) {
        h(fmt::format( //
            "string-queue-reverse[{}] has string[{}]",
            q(r(*regex)),
            q(*string)));
      }
    }
  }
}

bool RegexMatchCache::hasRegex(regex_key const& regex) const noexcept {
  return cacheRegexToMatch_.contains(regex);
}

void RegexMatchCache::addRegex(regex_key const& regex) {
  auto const [rtmiter, rtminserted] = cacheRegexToMatch_.try_emplace(regex);
  if (!rtminserted) {
    return;
  }
  auto guard = makeGuard(std::bind(&RegexMatchCache::repair, this));
  auto const regexp = &rtmiter->first;
  auto const regexi = regexVector_.insert_value(regexp).first;
  if (cacheMatchToRegex_.empty()) {
    guard.dismiss();
    return;
  }
  auto const [sqriter, sqrinserted] = stringQueueReverse_.try_emplace(regexp);
  CHECK(sqrinserted) << "string already in string-queue-reverse";
  auto& sqrentry = sqriter->second;
  for (auto const& [string, mtrentry] : cacheMatchToRegex_) {
    stringQueueForward_[string].regexes.set_value(regexi, true);
    sqrentry.strings.insert(string);
  }
  guard.dismiss();
}

void RegexMatchCache::eraseRegex(regex_key const& regex) {
  auto const rtmiter = cacheRegexToMatch_.find(regex);
  if (rtmiter == cacheRegexToMatch_.end()) {
    return;
  }
  auto guard = makeGuard(std::bind(&RegexMatchCache::repair, this));
  auto const regexp = &rtmiter->first;
  auto const regexi = regexVector_.index_of_value(regexp);
  for (auto const match : rtmiter->second.matches) {
    get_ptr(cacheMatchToRegex_, match)->regexes.set_value(regexi, false);
  }
  auto const sqriter = stringQueueReverse_.find(regexp);
  if (sqriter != stringQueueReverse_.end()) {
    for (auto const string : sqriter->second.strings) {
      auto const sqfiter = stringQueueForward_.find(string);
      CHECK(sqfiter != stringQueueForward_.end());
      sqfiter->second.regexes.set_value(regexi, false);
      if (sqfiter->second.regexes.as_index_set_view().empty()) {
        stringQueueForward_.erase(sqfiter);
      }
    }
    stringQueueReverse_.erase(sqriter);
  }
  regexVector_.erase_value(regexp);
  cacheRegexToMatch_.erase(rtmiter);
  guard.dismiss();
}

bool RegexMatchCache::hasString(string_pointer const string) const noexcept {
  return //
      cacheMatchToRegex_.contains(string) ||
      stringQueueForward_.contains(string);
}

void RegexMatchCache::addString(string_pointer const string) {
  //  return-early if already added
  if (!cacheMatchToRegex_.try_emplace(string).second) {
    return;
  }
  if (cacheRegexToMatch_.empty()) {
    return;
  }
  auto guard = makeGuard(std::bind(&RegexMatchCache::repair, this));
  auto const [sqfiter, sqfinserted] = stringQueueForward_.try_emplace(string);
  CHECK(sqfinserted) << "string already in string-queue-forward";

  //  add to string-queue-forward and string-queue-reverse
  auto& sqfentry = sqfiter->second;
  for (auto const& [regex, entry] : cacheRegexToMatch_) {
    auto regexi = regexVector_.index_of_value(&regex);
    sqfentry.regexes.set_value(regexi, true);
    stringQueueReverse_[&regex].strings.insert(string);
  }
  guard.dismiss();
}

void RegexMatchCache::eraseString(string_pointer const string) {
  auto guard = makeGuard(std::bind(&RegexMatchCache::repair, this));

  //  erase from string-queue-forward and string-queue-reverse
  auto const sqfiter = stringQueueForward_.find(string);
  if (sqfiter != stringQueueForward_.end()) {
    for (auto const regexi : sqfiter->second.regexes.as_index_set_view()) {
      auto const regexp = regexVector_.value_at_index(regexi);
      auto const sqriter = stringQueueReverse_.find(regexp);
      sqriter->second.strings.erase(string);
      if (sqriter->second.strings.empty()) {
        stringQueueReverse_.erase(sqriter);
      }
    }
    stringQueueForward_.erase(sqfiter);
  }

  //  erase from cache-regex-to-match and cache-match-to-regex
  auto const mtriter = cacheMatchToRegex_.find(string);
  if (mtriter != cacheMatchToRegex_.end()) {
    for (auto const regexi : mtriter->second.regexes.as_index_set_view()) {
      auto const regex = regexVector_.value_at_index(regexi);
      get_ptr(cacheRegexToMatch_, *regex)->matches.erase(string);
    }
    cacheMatchToRegex_.erase(mtriter);
  }

  guard.dismiss();
}

std::vector<std::string const*> RegexMatchCache::findMatchesUncached(
    std::string_view const regex) const {
  std::vector<std::string const*> result;
  RegexObject robject{regex};
  for (auto const& [string, _] : cacheMatchToRegex_) {
    if (robject(*string)) {
      result.push_back(string);
    }
  }
  return result;
}

bool RegexMatchCache::isReadyToFindMatches(
    regex_key const& regex) const noexcept {
  auto const rtmiter = cacheRegexToMatch_.find(regex);
  return //
      rtmiter != cacheRegexToMatch_.end() &&
      !stringQueueReverse_.contains(&rtmiter->first);
}

void RegexMatchCache::prepareToFindMatches(regex_key_and_view const& regex) {
  auto guard = makeGuard(std::bind(&RegexMatchCache::repair, this));
  auto const [rtmiter, inserted] = cacheRegexToMatch_.try_emplace(regex);
  auto const regexp = &rtmiter->first;
  auto& rtmentry = rtmiter->second;
  auto const [regexi, rvinserted] = regexVector_.insert_value(regexp);
  CHECK_EQ(rvinserted, inserted);

  if (inserted) {
    //  evaluate new regex over matches
    CHECK(!stringQueueReverse_.contains(regexp));
    if (cacheMatchToRegex_.empty()) {
      CHECK(stringQueueForward_.empty());
      CHECK(stringQueueReverse_.empty());
      guard.dismiss();
      return;
    }
    RegexObject robject{regex};
    for (auto& [string, mtrentry] : cacheMatchToRegex_) {
      if (robject(*string)) {
        rtmentry.matches.insert(string);
        mtrentry.regexes.set_value(regexi, true);
      }
    }
  } else {
    //  evaluate old regex over queue
    auto const sqriter = stringQueueReverse_.find(regexp);
    if (sqriter == stringQueueReverse_.end()) {
      //  was actually ready-to-find-matches for regex
      guard.dismiss();
      return;
    }
    auto const strings = std::move(sqriter->second.strings);
    CHECK(!strings.empty());
    stringQueueReverse_.erase(sqriter);
    RegexObject robject{regex};
    for (auto const string : strings) {
      auto const sqfiter = stringQueueForward_.find(string);
      CHECK(sqfiter != stringQueueForward_.end());
      CHECK(sqfiter->second.regexes.get_value(regexi));
      sqfiter->second.regexes.set_value(regexi, false);
      if (sqfiter->second.regexes.as_index_set_view().empty()) {
        stringQueueForward_.erase(sqfiter);
      }

      auto const mtriter = cacheMatchToRegex_.find(string);
      CHECK(mtriter != cacheMatchToRegex_.end());
      auto& mtrentry = mtriter->second;
      if (robject(*string)) {
        rtmentry.matches.insert(string);
        mtrentry.regexes.set_value(regexi, true);
      }
    }
  }
  guard.dismiss();
}

RegexMatchCache::FindMatchesUnsafeResult RegexMatchCache::findMatchesUnsafe(
    regex_key const& regex, time_point const now) const {
  if (kIsDebug && !isReadyToFindMatches(regex)) {
    throw std::logic_error("not ready to find matches");
  }
  auto const& rtmentry = cacheRegexToMatch_.at(regex);
  atomic_fetch_modify(
      rtmentry.accessed_at,
      [now](auto const val) { return std::max(now, val); },
      std::memory_order_relaxed);
  return {rtmentry.matches};
}

std::vector<RegexMatchCache::string_pointer> RegexMatchCache::findMatches(
    regex_key const& regex, time_point const now) const {
  auto const matches = findMatchesUnsafe(regex, now);
  return {matches.begin(), matches.end()};
}

bool RegexMatchCache::hasItemsToPurge(time_point const expiry) const noexcept {
  for (auto const& [regex, entry] : cacheRegexToMatch_) {
    auto const accessed_at = entry.accessed_at.load(std::memory_order_relaxed);
    if (accessed_at <= expiry) {
      return true;
    }
  }
  return false;
}

void RegexMatchCache::clear() {
  std::exchange(stringQueueReverse_, {});
  std::exchange(stringQueueForward_, {});
  std::exchange(cacheMatchToRegex_, {});
  std::exchange(cacheRegexToMatch_, {});
  std::exchange(regexVector_, {});
}

void RegexMatchCache::purge(time_point const expiry) {
  std::vector<regex_key> regexes;
  for (auto const& [regex, entry] : cacheRegexToMatch_) {
    auto const accessed_at = entry.accessed_at.load(std::memory_order_relaxed);
    if (accessed_at <= expiry) {
      regexes.push_back(regex);
    }
  }
  for (auto const& regex : regexes) {
    eraseRegex(regex);
  }
}

} // namespace folly
