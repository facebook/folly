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

  static folly::span<T const> to_span(
      folly::F14VectorSet<T> const& set) noexcept {
    auto const size = set.size();
    auto const data = size ? &*set.begin() + 1 - size : nullptr;
    return {data, size};
  }
  static folly::span<T const> to_span(
      folly::sorted_vector_set<T> const& set) noexcept {
    return {set.begin(), set.end()};
  }
  folly::span<T const> to_span() const noexcept { return to_span(*this); }

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
          value = folly::span<size_t const>{set.begin(), set.end()}[dist(rng)];
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
