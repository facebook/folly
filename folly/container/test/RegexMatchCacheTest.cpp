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

#include <chrono>
#include <numeric>
#include <random>
#include <span>
#include <string>
#include <string_view>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <folly/Portability.h>
#include <folly/Utility.h>
#include <folly/container/F14Map.h>
#include <folly/container/F14Set.h>
#include <folly/container/sorted_vector_types.h>
#include <folly/container/span.h>
#include <folly/lang/Keep.h>
#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

using namespace std::literals;

using folly::RegexMatchCache;
using folly::RegexMatchCacheDynamicBitset;
using folly::RegexMatchCacheKey;
using folly::RegexMatchCacheKeyAndView;

static_assert(
    !std::is_move_assignable_v<RegexMatchCache::ConsistencyReportMatcher>);

extern "C" FOLLY_KEEP size_t check_folly_regex_match_cache_dynamic_bitset_count(
    RegexMatchCacheDynamicBitset const& bitset) {
  auto const view = bitset.as_index_set_view();
  return std::accumulate(view.begin(), view.end(), size_t(0));
}

/// test_ref
///
/// A formattable variation of std::reference_wrapper.
///
/// TODO: Since fmt-v12, remove this and just use std::reference_wrapper.
template <typename T>
struct test_ref : std::reference_wrapper<T> {
  using base = std::reference_wrapper<T>;
  using base::base;
};
template <typename T>
test_ref(T&) -> test_ref<T>;

/// TODO: Since fmt-v9, remove this and just define an overload of format_as.
namespace fmt {
template <typename T>
struct formatter<test_ref<T>> : private formatter<std::remove_const_t<T>> {
  using base = formatter<std::remove_const_t<T>>;
  using base::parse;

#if FMT_VERSION >= 80000
  template <typename Context>
  auto format(test_ref<T> const ref, Context& ctx) const {
    return base::format(ref.get(), ctx);
  }
#else
  template <typename Context>
  auto format(test_ref<T> const ref, Context& ctx) {
    return base::format(ref.get(), ctx);
  }
#endif
};
} // namespace fmt

#if FOLLY_F14_VECTOR_INTRINSICS_AVAILABLE
template <typename T>
using unordered_vector_set_base = folly::F14VectorSet<T>;
#else
template <typename T>
using unordered_vector_set_base = folly::sorted_vector_set<T>;
#endif

template <typename T>
struct unordered_vector_set : private unordered_vector_set_base<T> {
 private:
  using base = unordered_vector_set_base<T>;

  static std::span<T const> to_span(
      folly::F14VectorSet<T> const& set) noexcept {
    auto const size = set.size();
    auto const data = size ? &*set.begin() + 1 - size : nullptr;
    return {data, size};
  }
  static std::span<T const> to_span(
      folly::sorted_vector_set<T> const& set) noexcept {
    return {&*set.begin(), &*set.end()};
  }
  std::span<T const> to_span() const noexcept { return to_span(*this); }

 public:
  using base::contains;
  using base::empty;
  using base::size;

  auto begin() const noexcept { return to_span().begin(); }
  auto end() const noexcept { return to_span().end(); }

  using base::clear;

  void insert(T const& value) { base::insert(value); }
  void erase(T const& value) { base::erase(value); }
};

struct RegexMatchCacheDynamicBitsetTest : testing::Test {};

TEST_F(RegexMatchCacheDynamicBitsetTest, example) {
  folly::RegexMatchCacheDynamicBitset bitset;
  EXPECT_FALSE(bitset.get_value(3));
  EXPECT_FALSE(bitset.get_value(14));
  EXPECT_TRUE(bitset.as_index_set_view().empty());
  EXPECT_THAT(bitset.as_index_set_view(), testing::ElementsAre());

  bitset.set_value(3, false);
  EXPECT_FALSE(bitset.get_value(3));
  EXPECT_FALSE(bitset.get_value(2));
  EXPECT_FALSE(bitset.get_value(14));
  EXPECT_FALSE(bitset.get_value(99));
  EXPECT_TRUE(bitset.as_index_set_view().empty());
  EXPECT_THAT(bitset.as_index_set_view(), testing::ElementsAre());

  bitset.set_value(3, true);
  EXPECT_TRUE(bitset.get_value(3));
  EXPECT_FALSE(bitset.get_value(2));
  EXPECT_FALSE(bitset.get_value(14));
  EXPECT_FALSE(bitset.get_value(99));
  EXPECT_FALSE(bitset.as_index_set_view().empty());
  EXPECT_THAT(bitset.as_index_set_view(), testing::ElementsAre(3));

  bitset.set_value(99, true);
  EXPECT_TRUE(bitset.get_value(3));
  EXPECT_FALSE(bitset.get_value(2));
  EXPECT_FALSE(bitset.get_value(14));
  EXPECT_TRUE(bitset.get_value(99));
  EXPECT_FALSE(bitset.as_index_set_view().empty());
  EXPECT_THAT(bitset.as_index_set_view(), testing::ElementsAre(3, 99));
}

TEST_F(RegexMatchCacheDynamicBitsetTest, combinatorics) {
  constexpr size_t opt = folly::kIsOptimize;
  constexpr size_t san = folly::kIsSanitize;
  constexpr size_t rounds = 1u << (1 + size_t(opt) + size_t(!san));
  constexpr size_t ops = 1u << (8 + size_t(opt) + size_t(!san));
  std::mt19937 rng;
  unordered_vector_set<size_t> set;
  folly::RegexMatchCacheDynamicBitset dyn;
  for (size_t r = 0; r < rounds; ++r) {
    set.clear();
    dyn.reset();
    for (size_t op = 0; op < ops; ++op) {
      auto const what = rng() % 128;
      SCOPED_TRACE(fmt::format("round[{}] op[{}] what[{}]", r, op, what));
      size_t value = 0;
      if (what < 16) {
        if (!set.empty()) {
          auto dist = std::uniform_int_distribution<size_t>{0, set.size() - 1};
          value = std::span<size_t const>{set.begin(), set.end()}[dist(rng)];
          ASSERT_TRUE(dyn.get_value(value));
          set.erase(value);
          dyn.set_value(value, false);
          ASSERT_FALSE(dyn.get_value(value));
        }
      } else if (what < 48) {
        auto dist = std::uniform_int_distribution<size_t>{0, op};
        value = dist(rng);
        ASSERT_EQ(set.contains(value), dyn.get_value(value));
        set.erase(value);
        ASSERT_FALSE(set.contains(value));
        dyn.set_value(value, false);
        ASSERT_FALSE(dyn.get_value(value));
      } else {
        auto dist = std::uniform_int_distribution<size_t>{0, op};
        value = dist(rng);
        ASSERT_EQ(set.contains(value), dyn.get_value(value));
        set.insert(value);
        ASSERT_TRUE(set.contains(value));
        dyn.set_value(value, true);
        ASSERT_TRUE(dyn.get_value(value));
      }
      ASSERT_EQ(set.size(), set.end() - set.begin());
      std::vector<size_t> els{set.begin(), set.end()};
      std::sort(els.begin(), els.end());
      ASSERT_THAT(dyn.as_index_set_view(), testing::ElementsAreArray(els));
    }
  }
}

struct RegexMatchCacheIndexedVectorTest : testing::Test {};

TEST_F(RegexMatchCacheIndexedVectorTest, example) {
  folly::RegexMatchCacheIndexedVector<std::string> vec;
  EXPECT_EQ(0, vec.size());

  EXPECT_THAT(
      vec.as_forward_view(),
      testing::UnorderedElementsAreArray(
          std::initializer_list<std::pair<std::string, size_t>>{
              //
          }));

  EXPECT_EQ(std::pair(size_t(0), true), vec.insert_value("hello"));
  EXPECT_THAT(
      vec.as_forward_view(),
      testing::UnorderedElementsAreArray(
          std::initializer_list<std::pair<std::string, size_t>>{
              {"hello", 0},
          }));

  EXPECT_EQ(std::pair(size_t(1), true), vec.insert_value("world"));
  EXPECT_THAT(
      vec.as_forward_view(),
      testing::UnorderedElementsAreArray(
          std::initializer_list<std::pair<std::string, size_t>>{
              {"hello", 0},
              {"world", 1},
          }));

  //  erase an element, leaving a hole in the index space to be filled
  EXPECT_TRUE(vec.erase_value("hello"));
  EXPECT_THAT(
      vec.as_forward_view(),
      testing::UnorderedElementsAreArray(
          std::initializer_list<std::pair<std::string, size_t>>{
              {"world", 1},
          }));

  //  fill the hole in the index space
  EXPECT_EQ(std::pair(size_t(0), true), vec.insert_value("water"));
  EXPECT_THAT(
      vec.as_forward_view(),
      testing::UnorderedElementsAreArray(
          std::initializer_list<std::pair<std::string, size_t>>{
              {"water", 0},
              {"world", 1},
          }));
}

struct RegexMatchCacheTest : testing::Test {
  using clock = RegexMatchCache::clock;
  using time_point = RegexMatchCache::time_point;

  class KeyMap final : public RegexMatchCache::KeyMap {
   private:
    folly::F14FastMap<regex_key, std::string> store_;

   public:
    void add(regex_key_and_view const& regex) {
      auto const to_value = [&] { return std::string{regex}; };
      store_.try_emplace(regex, folly::invocable_to(to_value));
    }

    std::string_view lookup(regex_key const& regex) const override {
      return store_.at(regex);
    }
  };

  KeyMap keys;

  class ConsistencyReportCache
      : public RegexMatchCache::ConsistencyReportMatcher {
   private:
    using base = RegexMatchCache::ConsistencyReportMatcher;
    using key = std::tuple<regex_key, string_pointer>;
    using map = std::unordered_map<key, bool>;
    map cache_;

   public:
    bool empty() const noexcept { return cache_.empty(); }

    size_t size() const noexcept { return cache_.size(); }

    bool match(
        RegexMatchCache::KeyMap const& keymap,
        regex_key const regex,
        string_pointer const string) final {
      auto const key = std::tuple{regex, string};
      auto const [iter, inserted] = cache_.try_emplace(key, false);
      auto& entry = iter->second;
      return !inserted ? entry : (entry = base::match(keymap, regex, string));
    }

    void clear() { cache_.clear(); }

    map const& get_map() const noexcept { return cache_; }

    void eraseRegex(regex_key const regex) {
      auto const end = cache_.end();
      auto it = cache_.begin();
      while (it != end) {
        auto const match = std::get<0>(it->first) == regex;
        it = match ? cache_.erase(it) : std::next(it);
      }
    }
  };

  ConsistencyReportCache crcache;

  class ConsistencyReport {
    friend RegexMatchCacheTest;

   private:
    std::vector<std::string> lines_;

    ConsistencyReport() = default;
    explicit ConsistencyReport(std::vector<std::string>&& lines) noexcept
        : lines_{std::move(lines)} {}

    void print(std::ostream& o) const {
      if (lines_.empty()) {
        o << "consistent" << std::endl;
      } else {
        o << "inconsistencies:" << std::endl;
        for (auto const& line : lines_) {
          o << "  " << line << std::endl;
        }
      }
    }

   public:
    bool consistent() const noexcept { return lines_.empty(); }

    friend std::ostream& operator<<(
        std::ostream& o, ConsistencyReport const& report) {
      return (report.print(o), o);
    }
  };

  void checkConsistency(RegexMatchCache const& cache) {
    cache.consistency(crcache, keys, [&](auto const line) { //
      throw std::logic_error(line);
    });
  }
  ConsistencyReport getConsistencyReport(RegexMatchCache const& cache) {
    ConsistencyReport out;
    cache.consistency(crcache, keys, [&](auto&& line) { //
      out.lines_.push_back(std::move(line));
    });
    return out;
  }

  auto inspect(RegexMatchCache const& cache) const {
    return cache.inspect(keys);
  }

  auto getRegexList(RegexMatchCache const& cache) const {
    std::vector<std::string> ret;
    for (auto const item : cache.getRegexList(keys)) {
      ret.emplace_back(item);
    }
    return ret;
  }

  auto lookup(RegexMatchCache& cache, std::string_view regex, time_point now) {
    auto key = RegexMatchCacheKeyAndView(regex);
    keys.add(key);
    auto uncached = std::as_const(cache).findMatchesUncached(regex);
    if (!std::as_const(cache).isReadyToFindMatches(key)) {
      cache.prepareToFindMatches(key);
    }
    auto matches = std::as_const(cache).findMatches(key, now);
    EXPECT_THAT(matches, testing::UnorderedElementsAreArray(uncached));
    return matches;
  }

  RegexMatchCache::PrepareHandle initBuild(
      RegexMatchCache& cache,
      RegexMatchCacheKeyAndView const& key,
      time_point const now) {
    keys.add(key);
    auto matcher = RegexMatchCache::compile(key);
    return cache.initPrepareToFindMatches(key, std::move(matcher), now);
  }

  RegexMatchCache::PrepareHandle initBuild(
      RegexMatchCache& cache, RegexMatchCacheKeyAndView const& key) {
    return initBuild(cache, key, clock::now());
  }

  void finishBuild(RegexMatchCache& cache, RegexMatchCache::PrepareHandle h) {
    cache.finiPrepareToFindMatches(std::move(h).evaluate());
  }

  std::vector<std::string const*> lookupOffLock(
      RegexMatchCache& cache, std::string_view regex, time_point now) {
    auto const key = RegexMatchCacheKeyAndView(regex);
    auto const uncached = std::as_const(cache).findMatchesUncached(regex);
    if (std::as_const(cache).isReadyToFindMatches(key)) {
      keys.add(key);
    } else {
      finishBuild(cache, initBuild(cache, key));
    }
    auto const matches = std::as_const(cache).findMatches(key, now);
    EXPECT_THAT(matches, testing::UnorderedElementsAreArray(uncached));
    return matches;
  }
};

TEST_F(RegexMatchCacheTest, clean) {
  RegexMatchCache cache;
  checkConsistency(cache);
}

TEST_F(RegexMatchCacheTest, clean_lookup) {
  RegexMatchCache cache;

  EXPECT_THAT(
      lookup(cache, "foo|bar", time_point() + 5s), //
      testing::UnorderedElementsAre())
      << inspect(cache);
  checkConsistency(cache);
}

TEST_F(RegexMatchCacheTest, add_strings_erase_add) {
  auto const foo = "foo"s;
  auto const bar = "bar"s;
  RegexMatchCache cache;

  cache.addString(&foo);
  checkConsistency(cache);
  cache.addString(&bar);
  checkConsistency(cache);

  cache.eraseString(&bar);
  checkConsistency(cache);
  EXPECT_THAT(cache.getStringList(), testing::UnorderedElementsAre(&foo));

  cache.addString(&bar);
  checkConsistency(cache);
  EXPECT_THAT(cache.getStringList(), testing::UnorderedElementsAre(&foo, &bar));
}

TEST_F(RegexMatchCacheTest, add_strings_lookup) {
  auto const foo = "foo"s;
  auto const bar = "bar"s;
  RegexMatchCache cache;

  cache.addString(&foo);
  checkConsistency(cache);
  cache.addString(&bar);
  checkConsistency(cache);

  EXPECT_THAT(
      lookup(cache, "foo|qux", time_point() + 5s), //
      testing::UnorderedElementsAre(&foo))
      << inspect(cache);
  checkConsistency(cache);
}

TEST_F(RegexMatchCacheTest, add_strings_lookup_erase_string) {
  auto const foo = "foo"s;
  auto const bar = "bar"s;
  RegexMatchCache cache;

  cache.addString(&foo);
  cache.addString(&bar);
  EXPECT_THAT(
      lookup(cache, "foo|qux", time_point() + 5s), //
      testing::UnorderedElementsAre(&foo))
      << inspect(cache);

  cache.eraseString(&foo);
  checkConsistency(cache);

  EXPECT_THAT(
      lookup(cache, "foo|qux", time_point() + 5s), //
      testing::UnorderedElementsAre())
      << inspect(cache);
  checkConsistency(cache);
}

TEST_F(RegexMatchCacheTest, add_strings_lookup_repeat) {
  auto const foo = "foo"s;
  auto const bar = "bar"s;
  auto const cat = "cat"s;
  auto const dog = "dog"s;
  RegexMatchCache cache;

  cache.addString(&foo);
  cache.addString(&bar);
  EXPECT_THAT(
      lookup(cache, "foo|qux", time_point() + 5s), //
      testing::UnorderedElementsAre(&foo))
      << inspect(cache);

  cache.addString(&cat);
  checkConsistency(cache);
  cache.addString(&dog);
  checkConsistency(cache);
  EXPECT_THAT(
      lookup(cache, "foo|qux", time_point() + 5s), //
      testing::UnorderedElementsAre(&foo))
      << inspect(cache);
  checkConsistency(cache);
  EXPECT_THAT(
      lookup(cache, "foo|qux|dog", time_point() + 5s), //
      testing::UnorderedElementsAre(&dog, &foo))
      << inspect(cache);
  checkConsistency(cache);
}

TEST_F(RegexMatchCacheTest, add_string) {
  auto const foo = "foo"s;
  RegexMatchCache cache;
  checkConsistency(cache);
  EXPECT_FALSE(cache.hasString(&foo));
  EXPECT_THAT(cache.getStringList(), testing::ElementsAre());
  EXPECT_THAT(getRegexList(cache), testing::ElementsAre());

  cache.addString(&foo);
  checkConsistency(cache);
  EXPECT_TRUE(cache.hasString(&foo));
  EXPECT_THAT(cache.getStringList(), testing::ElementsAre(&foo));
  EXPECT_THAT(getRegexList(cache), testing::ElementsAre());

  cache.eraseString(&foo);
  checkConsistency(cache);
  EXPECT_FALSE(cache.hasString(&foo));
  EXPECT_THAT(cache.getStringList(), testing::ElementsAre());
  EXPECT_THAT(getRegexList(cache), testing::ElementsAre());
}

TEST_F(RegexMatchCacheTest, add_regex) {
  constexpr auto xFooOrBar = "foo|bar"sv;
  RegexMatchCache cache;
  checkConsistency(cache);
  EXPECT_FALSE(cache.hasRegex(RegexMatchCacheKey(xFooOrBar)));
  EXPECT_THAT(cache.getStringList(), testing::ElementsAre());
  EXPECT_THAT(getRegexList(cache), testing::ElementsAre());

  keys.add(RegexMatchCacheKeyAndView(xFooOrBar));
  cache.addRegex(RegexMatchCacheKey(xFooOrBar));
  checkConsistency(cache);
  EXPECT_TRUE(cache.hasRegex(RegexMatchCacheKey(xFooOrBar)));
  EXPECT_THAT(cache.getStringList(), testing::ElementsAre());
  EXPECT_THAT(getRegexList(cache), testing::ElementsAre(xFooOrBar));

  cache.eraseRegex(RegexMatchCacheKey(xFooOrBar));
  checkConsistency(cache);
  EXPECT_FALSE(cache.hasRegex(RegexMatchCacheKey(xFooOrBar)));
  EXPECT_THAT(cache.getStringList(), testing::ElementsAre());
  EXPECT_THAT(getRegexList(cache), testing::ElementsAre());
}

TEST_F(RegexMatchCacheTest, add_regex_add_string) {
  const auto xFoo = "foo"s;
  constexpr auto xFooOrBar = "foo|bar"sv;
  RegexMatchCache cache;
  checkConsistency(cache);
  EXPECT_FALSE(cache.hasRegex(RegexMatchCacheKey(xFooOrBar)));
  EXPECT_THAT(cache.getStringList(), testing::ElementsAre());
  EXPECT_THAT(getRegexList(cache), testing::ElementsAre());

  keys.add(RegexMatchCacheKeyAndView(xFooOrBar));
  cache.addRegex(RegexMatchCacheKey(xFooOrBar));
  checkConsistency(cache);
  EXPECT_TRUE(cache.hasRegex(RegexMatchCacheKey(xFooOrBar)));
  EXPECT_THAT(cache.getStringList(), testing::ElementsAre());
  EXPECT_THAT(getRegexList(cache), testing::ElementsAre(xFooOrBar));

  cache.addString(&xFoo);
  checkConsistency(cache);
}

TEST_F(RegexMatchCacheTest, combinatorics) {
  constexpr size_t opt = folly::kIsOptimize;
  constexpr size_t san = folly::kIsSanitize;
  constexpr size_t rounds = 1u << (1 + size_t(opt) + size_t(!san));
  constexpr size_t ops = 1u << (8 + size_t(opt) + size_t(!san));
  std::mt19937 rng;
  std::vector const source{"foo"s, "bar"s, "cat"s, "dog"s, "qux"s, "nit"s};
  RegexMatchCache::time_point now{};
  RegexMatchCache cache;

  auto const contains = [](auto const& c, auto const& k) {
    return std::count(c.begin(), c.end(), k) > 0;
  };
  auto const rand_size = [&] {
    auto const r = rng() % 256;
    return size_t(1) + (r >= 128) + (r >= 192) + (r >= 224);
  };

  for (size_t r = 0; r < rounds; ++r) {
    std::vector const strings = source;
    auto const nstrings = strings.size();
    SCOPE_EXIT {
      crcache.clear();
      cache.clear();
    };
    auto strings_dist = std::uniform_int_distribution<size_t>{0, nstrings - 1};
    auto const rand_string = [&] { return &strings.at(strings_dist(rng)); };
    auto const rand_strings = [&]() {
      std::vector<test_ref<std::string const>> ret;
      auto const out = std::back_inserter(ret);
      std::sample(strings.begin(), strings.end(), out, rand_size(), rng);
      return ret;
    };
    for (size_t i = 0; i < ops; ++i) {
      auto const what = rng() % (1u << 5);
      SCOPED_TRACE(fmt::format("round[{}] iter[{}] what[{}]", r, i, what));
      if (what < 1) {
        crcache.clear();
        cache.purge(now);
        now += 1s;
      } else if (what < 2) {
        if (auto const regexes = getRegexList(cache); !regexes.empty()) {
          auto const nregexes = regexes.size();
          auto dist = std::uniform_int_distribution<size_t>{0, nregexes - 1};
          auto const regex = std::string{regexes.at(dist(rng))};

          auto const key = RegexMatchCacheKey{regex};
          ASSERT_TRUE(cache.hasRegex(key));
          crcache.eraseRegex(key);
          cache.eraseRegex(key);
          ASSERT_FALSE(cache.hasRegex(key));
        }
      } else if (what < 8) {
        auto const str = rand_string();
        cache.addString(str);
      } else if (what < 10) {
        auto const str = rand_string();
        cache.eraseString(str);
      }
      if (what < 10) {
        auto const report = getConsistencyReport(cache);
        ASSERT_TRUE(report.consistent()) << inspect(cache) << report;
      }
      auto const chosen_strings = rand_strings();
      auto const regex = fmt::format("{}", fmt::join(chosen_strings, "|"));
      auto const key = RegexMatchCacheKeyAndView(regex);
      keys.add(key);
      if (!cache.isReadyToFindMatches(key)) {
        cache.prepareToFindMatches(key);
      }
      ASSERT_TRUE(cache.hasRegex(key));
      {
        auto const report = getConsistencyReport(cache);
        ASSERT_TRUE(report.consistent()) << inspect(cache) << report;
      }
      auto const mlist = cache.findMatchesUnsafe(key, now);
      for (auto const ref : chosen_strings) {
        ASSERT_EQ(cache.hasString(&ref.get()), contains(mlist, &ref.get()));
      }
      ASSERT_THAT(
          cache.findMatchesUncached(regex),
          testing::UnorderedElementsAreArray(mlist));
    }
  }
}

TEST_F(RegexMatchCacheTest, matcher) {
  auto matcher = RegexMatchCache::compile("foo|bar");
  EXPECT_TRUE(matcher("foo"));
  EXPECT_TRUE(matcher("bar"));
  EXPECT_FALSE(matcher("baz"));

  //  Matcher is move-only and stays usable after being moved-from-into
  auto const moved = std::move(matcher);
  EXPECT_TRUE(moved("foo"));
  EXPECT_FALSE(moved("qux"));
}

TEST_F(RegexMatchCacheTest, init_rejects_matcher_from_another_pattern) {
  constexpr auto xFoo = "foo"sv;
  constexpr auto xBar = "bar"sv;
  RegexMatchCache cache;

  auto const key = RegexMatchCacheKeyAndView(xFoo);
  EXPECT_DEATH(
      cache.initPrepareToFindMatches(
          key, RegexMatchCache::compile(xBar), clock::now()),
      "matcher compiled from another pattern");
}

TEST_F(RegexMatchCacheTest, off_lock_build) {
  auto const foo = "foo"s;
  auto const bar = "bar"s;
  RegexMatchCache cache;

  cache.addString(&foo);
  cache.addString(&bar);
  checkConsistency(cache);

  EXPECT_THAT(
      lookupOffLock(cache, "foo|qux", time_point() + 5s), //
      testing::UnorderedElementsAre(&foo))
      << inspect(cache);
  checkConsistency(cache);
}

TEST_F(RegexMatchCacheTest, off_lock_build_drains_queued_strings) {
  auto const foo = "foo"s;
  auto const bar = "bar"s;
  auto const dog = "dog"s;
  RegexMatchCache cache;

  cache.addString(&foo);
  cache.addString(&bar);
  EXPECT_THAT(
      lookupOffLock(cache, "foo|dog", time_point() + 5s), //
      testing::UnorderedElementsAre(&foo))
      << inspect(cache);

  //  a string added after build is queued; the next build drains it
  cache.addString(&dog);
  checkConsistency(cache);
  EXPECT_FALSE(cache.isReadyToFindMatches(RegexMatchCacheKey("foo|dog")));

  EXPECT_THAT(
      lookupOffLock(cache, "foo|dog", time_point() + 5s), //
      testing::UnorderedElementsAre(&foo, &dog))
      << inspect(cache);
  checkConsistency(cache);
}

TEST_F(RegexMatchCacheTest, num_queued_strings_for) {
  auto const foo = "foo"s;
  auto const bar = "bar"s;
  constexpr auto xFooOrBar = "foo|bar"sv;
  RegexMatchCache cache;

  cache.addString(&foo);
  cache.addString(&bar);

  EXPECT_EQ(0, cache.numQueuedStringsFor(RegexMatchCacheKey(xFooOrBar)));

  keys.add(RegexMatchCacheKeyAndView(xFooOrBar));
  cache.addRegex(RegexMatchCacheKey(xFooOrBar));
  EXPECT_EQ(2, cache.numQueuedStringsFor(RegexMatchCacheKey(xFooOrBar)));

  auto const key = RegexMatchCacheKeyAndView(xFooOrBar);
  auto handle = initBuild(cache, key);
  EXPECT_EQ(2, handle.queueSize());
  finishBuild(cache, std::move(handle));
  EXPECT_TRUE(cache.isReadyToFindMatches(RegexMatchCacheKey(xFooOrBar)));
  EXPECT_EQ(0, cache.numQueuedStringsFor(RegexMatchCacheKey(xFooOrBar)));
  checkConsistency(cache);
}

TEST_F(RegexMatchCacheTest, off_lock_build_erase_string_during) {
  auto const foo = "foo"s;
  auto const bar = "bar"s;
  constexpr auto xFooOrBar = "foo|bar"sv;
  RegexMatchCache cache;

  cache.addString(&foo);
  cache.addString(&bar);

  auto const key = RegexMatchCacheKeyAndView(xFooOrBar);
  auto handle = initBuild(cache, key);
  ASSERT_EQ(2, handle.queueSize());

  //  erase a snapshotted string before publish; caller holds it alive, skip it
  cache.eraseString(&foo);
  checkConsistency(cache);

  cache.finiPrepareToFindMatches(std::move(handle).evaluate());
  checkConsistency(cache);

  EXPECT_THAT(
      std::as_const(cache).findMatches(key, time_point() + 5s),
      testing::UnorderedElementsAre(&bar))
      << inspect(cache);
  EXPECT_THAT(
      std::as_const(cache).findMatchesUncached(xFooOrBar),
      testing::UnorderedElementsAre(&bar));
}

TEST_F(RegexMatchCacheTest, off_lock_build_add_string_during) {
  auto const foo = "foo"s;
  auto const bar = "bar"s;
  constexpr auto xFooOrBar = "foo|bar"sv;
  RegexMatchCache cache;

  cache.addString(&foo);

  auto const key = RegexMatchCacheKeyAndView(xFooOrBar);
  auto handle = initBuild(cache, key);
  ASSERT_EQ(1, handle.queueSize());

  //  a string added after the snapshot must stay queued for a later build
  cache.addString(&bar);
  checkConsistency(cache);

  cache.finiPrepareToFindMatches(std::move(handle).evaluate());
  checkConsistency(cache);
  EXPECT_FALSE(cache.isReadyToFindMatches(key));
  EXPECT_EQ(1, cache.numQueuedStringsFor(key));

  //  a subsequent build drains the remaining queued string
  finishBuild(cache, initBuild(cache, key));
  checkConsistency(cache);
  EXPECT_TRUE(cache.isReadyToFindMatches(key));
  EXPECT_THAT(
      std::as_const(cache).findMatches(key, time_point() + 5s),
      testing::UnorderedElementsAre(&foo, &bar))
      << inspect(cache);
}

TEST_F(RegexMatchCacheTest, off_lock_build_fini_is_idempotent) {
  auto const foo = "foo"s;
  auto const bar = "bar"s;
  constexpr auto xFooOrBar = "foo|bar"sv;
  RegexMatchCache cache;

  cache.addString(&foo);
  cache.addString(&bar);

  auto const key = RegexMatchCacheKeyAndView(xFooOrBar);

  //  publishing from two concurrent builds must not double-count
  auto h1 = initBuild(cache, key);
  auto h2 = initBuild(cache, key);
  cache.finiPrepareToFindMatches(std::move(h1).evaluate());
  checkConsistency(cache);
  cache.finiPrepareToFindMatches(std::move(h2).evaluate());
  checkConsistency(cache);

  EXPECT_TRUE(cache.isReadyToFindMatches(key));
  EXPECT_THAT(
      std::as_const(cache).findMatches(key, time_point() + 5s),
      testing::UnorderedElementsAre(&foo, &bar))
      << inspect(cache);
}

TEST_F(RegexMatchCacheTest, off_lock_build_erase_regex_during) {
  auto const foo = "foo"s;
  constexpr auto xFooOrBar = "foo|bar"sv;
  RegexMatchCache cache;

  cache.addString(&foo);

  auto const key = RegexMatchCacheKeyAndView(xFooOrBar);
  auto handle = initBuild(cache, key);
  ASSERT_EQ(1, handle.queueSize());

  //  the regex is erased before results are published; fini must be a no-op
  crcache.eraseRegex(RegexMatchCacheKey(xFooOrBar));
  cache.eraseRegex(RegexMatchCacheKey(xFooOrBar));
  checkConsistency(cache);

  cache.finiPrepareToFindMatches(std::move(handle).evaluate());
  checkConsistency(cache);

  EXPECT_FALSE(cache.hasRegex(RegexMatchCacheKey(xFooOrBar)));
}

TEST_F(RegexMatchCacheTest, init_stamps_accessed_protects_from_purge) {
  auto const foo = "foo"s;
  constexpr auto xFoo = "foo"sv;
  RegexMatchCache cache;

  cache.addString(&foo);

  //  a never-served regex sits at epoch accessed-at, so a purge treats it as
  //  stale -- this is the state an in-flight build starts from, and what a
  //  concurrent purge would evict
  auto const key = RegexMatchCacheKeyAndView(xFoo);
  keys.add(key);
  cache.addRegex(key);
  EXPECT_TRUE(cache.hasItemsToPurge(time_point() + 5s));

  //  initPrepareToFindMatches stamps accessed_at with the caller's time point,
  //  so only a purge at or past that point still treats the build as stale
  auto handle = initBuild(cache, key, time_point() + 10s);
  EXPECT_FALSE(cache.hasItemsToPurge(time_point() + 5s));
  EXPECT_TRUE(cache.hasItemsToPurge(time_point() + 10s));
  cache.purge(time_point() + 5s);
  EXPECT_TRUE(cache.hasRegex(RegexMatchCacheKey(xFoo)));
  checkConsistency(cache);

  finishBuild(cache, std::move(handle));
  checkConsistency(cache);
}

TEST_F(RegexMatchCacheTest, compile_bad_pattern_leaves_cache_intact) {
  auto const foo = "foo"s;
  constexpr auto xFoo = "foo"sv;
  constexpr auto xBad = "foo["sv;
  RegexMatchCache cache;

  cache.addString(&foo);
  auto const key = RegexMatchCacheKeyAndView(xFoo);
  finishBuild(cache, initBuild(cache, key));
  ASSERT_TRUE(cache.isReadyToFindMatches(key));

  //  compile returns a standalone matcher, so an invalid pattern throws before
  //  any cache state changes
  EXPECT_THROW(RegexMatchCache::compile(xBad), std::runtime_error);
  checkConsistency(cache);
  EXPECT_TRUE(cache.isReadyToFindMatches(key));
  EXPECT_THAT(
      std::as_const(cache).findMatches(key, time_point() + 5s),
      testing::UnorderedElementsAre(&foo))
      << inspect(cache);

  //  by contrast prepareToFindMatches purges every cached regex on failure,
  //  leaving the strings
  auto const bad = RegexMatchCacheKeyAndView(xBad);
  keys.add(bad);
  EXPECT_THROW(cache.prepareToFindMatches(bad), std::runtime_error);
  crcache.eraseRegex(RegexMatchCacheKey(xFoo));
  checkConsistency(cache);
  EXPECT_FALSE(cache.hasRegex(RegexMatchCacheKey(xFoo)));
  EXPECT_THAT(cache.getStringList(), testing::UnorderedElementsAre(&foo));
}

TEST_F(RegexMatchCacheTest, off_lock_build_erase_add_regex_during) {
  auto const foo = "foo"s;
  auto const bar = "bar"s;
  constexpr auto xFooOrBar = "foo|bar"sv;
  RegexMatchCache cache;

  cache.addString(&foo);
  cache.addString(&bar);

  auto const key = RegexMatchCacheKeyAndView(xFooOrBar);
  auto handle = initBuild(cache, key);
  ASSERT_EQ(2, handle.queueSize());

  //  the regex is erased and re-added under the same key while the scan runs.
  //  the key is derived from the pattern, so the re-added regex is the same
  //  pattern and publishing the in-flight result must not corrupt the cache
  crcache.eraseRegex(RegexMatchCacheKey(xFooOrBar));
  cache.eraseRegex(RegexMatchCacheKey(xFooOrBar));
  cache.addRegex(key);
  checkConsistency(cache);

  cache.finiPrepareToFindMatches(std::move(handle).evaluate());
  checkConsistency(cache);

  //  the stale publish must land against the re-added regex; asserting through
  //  a rebuilding lookup would pass even if fini had published nothing
  ASSERT_TRUE(cache.isReadyToFindMatches(key));
  EXPECT_THAT(
      std::as_const(cache).findMatches(key, time_point() + 5s),
      testing::UnorderedElementsAre(&foo, &bar))
      << inspect(cache);
}

TEST_F(RegexMatchCacheTest, off_lock_build_handle_queue_size_after_evaluate) {
  auto const foo = "foo"s;
  constexpr auto xFoo = "foo"sv;
  RegexMatchCache cache;

  cache.addString(&foo);

  auto const key = RegexMatchCacheKeyAndView(xFoo);
  auto handle = initBuild(cache, key);
  EXPECT_EQ(1, handle.queueSize());

  //  evaluate() moves the queue out, so the moved-from handle reports zero
  auto result = std::move(handle).evaluate();
  EXPECT_EQ(0, handle.queueSize()); // NOLINT(bugprone-use-after-move)

  cache.finiPrepareToFindMatches(std::move(result));
  checkConsistency(cache);
}
