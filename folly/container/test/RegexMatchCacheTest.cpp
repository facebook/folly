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
#include <random>
#include <string>
#include <string_view>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <folly/Portability.h>
#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

using namespace std::literals;

using folly::RegexMatchCache;

static_assert(
    !std::is_move_assignable_v<RegexMatchCache::ConsistencyReportMatcher>);

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

struct RegexMatchCacheTest : testing::Test {
  using clock = RegexMatchCache::clock;
  using time_point = RegexMatchCache::time_point;

  class ConsistencyReportCache
      : public RegexMatchCache::ConsistencyReportMatcher {
   private:
    using base = RegexMatchCache::ConsistencyReportMatcher;
    using key = std::tuple<regex_pointer, string_pointer>;
    using map = std::unordered_map<key, bool>;
    map cache_;

   public:
    bool empty() const noexcept { return cache_.empty(); }

    size_t size() const noexcept { return cache_.size(); }

    bool match(regex_pointer const regex, string_pointer const string) final {
      auto const key = std::tuple{regex, string};
      auto const [iter, inserted] = cache_.try_emplace(key, false);
      auto& entry = iter->second;
      return !inserted ? entry : (entry = base::match(regex, string));
    }

    void clear() { cache_.clear(); }

    map const& get_map() const noexcept { return cache_; }

    void eraseRegex(std::string_view const regex) {
      auto const end = cache_.end();
      auto it = cache_.begin();
      while (it != end) {
        auto const match = *std::get<0>(it->first) == regex;
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
    cache.consistency(crcache, [&](auto const line) { //
      throw std::logic_error(line);
    });
  }
  ConsistencyReport getConsistencyReport(RegexMatchCache const& cache) {
    ConsistencyReport out;
    cache.consistency(crcache, [&](auto&& line) { //
      out.lines_.push_back(std::move(line));
    });
    return out;
  }

  auto inspect(RegexMatchCache const& cache) const { return cache.inspect(); }

  auto getRegexList(RegexMatchCache const& cache) const {
    return cache.getRegexList();
  }

  auto lookup(RegexMatchCache& cache, std::string_view regex, time_point now) {
    auto uncached = std::as_const(cache).findMatchesUncached(regex);
    if (!std::as_const(cache).isReadyToFindMatches(regex)) {
      cache.prepareToFindMatches(regex);
    }
    auto matches = std::as_const(cache).findMatches(regex, now);
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
  EXPECT_FALSE(cache.hasRegex(xFooOrBar));
  EXPECT_THAT(cache.getStringList(), testing::ElementsAre());
  EXPECT_THAT(getRegexList(cache), testing::ElementsAre());

  cache.addRegex(xFooOrBar);
  checkConsistency(cache);
  EXPECT_TRUE(cache.hasRegex(xFooOrBar));
  EXPECT_THAT(cache.getStringList(), testing::ElementsAre());
  EXPECT_THAT(getRegexList(cache), testing::ElementsAre(xFooOrBar));

  cache.eraseRegex(xFooOrBar);
  checkConsistency(cache);
  EXPECT_FALSE(cache.hasRegex(xFooOrBar));
  EXPECT_THAT(cache.getStringList(), testing::ElementsAre());
  EXPECT_THAT(getRegexList(cache), testing::ElementsAre());
}

TEST_F(RegexMatchCacheTest, add_regex_add_string) {
  const auto xFoo = "foo"s;
  constexpr auto xFooOrBar = "foo|bar"sv;
  RegexMatchCache cache;
  checkConsistency(cache);
  EXPECT_FALSE(cache.hasRegex(xFooOrBar));
  EXPECT_THAT(cache.getStringList(), testing::ElementsAre());
  EXPECT_THAT(getRegexList(cache), testing::ElementsAre());

  cache.addRegex(xFooOrBar);
  checkConsistency(cache);
  EXPECT_TRUE(cache.hasRegex(xFooOrBar));
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

          ASSERT_TRUE(cache.hasRegex(regex));
          crcache.eraseRegex(regex);
          cache.eraseRegex(regex);
          ASSERT_FALSE(cache.hasRegex(regex));
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
      if (!cache.isReadyToFindMatches(regex)) {
        cache.prepareToFindMatches(regex);
      }
      ASSERT_TRUE(cache.hasRegex(regex));
      {
        auto const report = getConsistencyReport(cache);
        ASSERT_TRUE(report.consistent()) << inspect(cache) << report;
      }
      auto const mlist = cache.findMatchesUnsafe(regex, now);
      for (auto const ref : chosen_strings) {
        ASSERT_EQ(cache.hasString(&ref.get()), contains(mlist, &ref.get()));
      }
      ASSERT_THAT(
          cache.findMatchesUncached(regex),
          testing::UnorderedElementsAreArray(mlist));
    }
  }
}
