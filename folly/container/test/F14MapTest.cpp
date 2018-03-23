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

#include <folly/container/F14Map.h>
#include <folly/container/test/F14TestUtil.h>
#include <folly/portability/GTest.h>

template <template <typename, typename, typename, typename, typename>
          class TMap>
void testCustomSwap() {
  using std::swap;

  TMap<
      int,
      int,
      folly::f14::DefaultHasher<int>,
      folly::f14::DefaultKeyEqual<int>,
      folly::f14::SwapTrackingAlloc<std::pair<int const, int>>>
      m0, m1;
  folly::f14::resetTracking();
  swap(m0, m1);

  EXPECT_EQ(
      0, folly::f14::Tracked<0>::counts.dist(folly::f14::Counts{0, 0, 0, 0}));
}

TEST(F14Map, customSwap) {
  testCustomSwap<folly::F14ValueMap>();
  testCustomSwap<folly::F14NodeMap>();
  testCustomSwap<folly::F14VectorMap>();
  testCustomSwap<folly::F14FastMap>();
}

///////////////////////////////////
#if FOLLY_F14_VECTOR_INTRINSICS_AVAILABLE
///////////////////////////////////

#include <chrono>
#include <random>
#include <string>
#include <typeinfo>
#include <unordered_map>

#include <folly/Range.h>
#include <folly/hash/Hash.h>

using namespace folly;
using namespace folly::f14;
using namespace folly::string_piece_literals;

namespace {
std::string s(char const* p) {
  return p;
}
} // namespace

template <typename T>
void runSimple() {
  T h;

  EXPECT_EQ(h.size(), 0);

  h.insert(std::make_pair(s("abc"), s("ABC")));
  EXPECT_TRUE(h.find(s("def")) == h.end());
  EXPECT_FALSE(h.find(s("abc")) == h.end());
  EXPECT_EQ(h[s("abc")], s("ABC"));
  h[s("ghi")] = s("GHI");
  EXPECT_EQ(h.size(), 2);
  h.erase(h.find(s("abc")));
  EXPECT_EQ(h.size(), 1);

  T h2(std::move(h));
  EXPECT_EQ(h.size(), 0);
  EXPECT_TRUE(h.begin() == h.end());
  EXPECT_EQ(h2.size(), 1);

  EXPECT_TRUE(h2.find(s("abc")) == h2.end());
  EXPECT_EQ(h2.begin()->first, s("ghi"));
  {
    auto i = h2.begin();
    EXPECT_FALSE(i == h2.end());
    ++i;
    EXPECT_TRUE(i == h2.end());
  }

  T h3;
  h3.try_emplace(s("xxx"));
  h3.insert_or_assign(s("yyy"), s("YYY"));
  h3 = std::move(h2);
  EXPECT_EQ(h2.size(), 0);
  EXPECT_EQ(h3.size(), 1);
  EXPECT_TRUE(h3.find(s("xxx")) == h3.end());

  for (uint64_t i = 0; i < 1000; ++i) {
    h[std::to_string(i * i * i)] = s("x");
    EXPECT_EQ(h.size(), i + 1);
  }
  {
    using std::swap;
    swap(h, h2);
  }
  for (uint64_t i = 0; i < 1000; ++i) {
    EXPECT_TRUE(h2.find(std::to_string(i * i * i)) != h2.end());
    EXPECT_EQ(
        h2.find(std::to_string(i * i * i))->first, std::to_string(i * i * i));
    EXPECT_TRUE(h2.find(std::to_string(i * i * i + 2)) == h2.end());
  }

  T h4{h2};
  EXPECT_EQ(h2.size(), 1000);
  EXPECT_EQ(h4.size(), 1000);

  T h5{std::move(h2)};
  T h6;
  h6 = h4;
  T h7 = h4;
  T h8({{s("abc"), s("ABC")}, {s("def"), s("DEF")}});
  T h9({{s("abc"), s("ABD")}, {s("def"), s("DEF")}});
  EXPECT_EQ(h8.size(), 2);
  EXPECT_EQ(h8.count(s("abc")), 1);
  EXPECT_EQ(h8.count(s("xyz")), 0);

  EXPECT_TRUE(h7 != h8);
  EXPECT_TRUE(h8 != h9);

  h8 = std::move(h7);
  // h2 and h7 are moved from, h4, h5, h6, and h8 should be identical

  EXPECT_TRUE(h4 == h8);

  EXPECT_TRUE(h2.empty());
  EXPECT_TRUE(h7.empty());
  for (uint64_t i = 0; i < 1000; ++i) {
    auto k = std::to_string(i * i * i);
    EXPECT_EQ(h4.count(k), 1);
    EXPECT_EQ(h5.count(k), 1);
    EXPECT_EQ(h6.count(k), 1);
    EXPECT_EQ(h8.count(k), 1);
  }

  EXPECT_TRUE(h2 == h7);
  EXPECT_TRUE(h4 != h7);

  EXPECT_EQ(h3.at(s("ghi")), s("GHI"));
  EXPECT_THROW(h3.at(s("abc")), std::out_of_range);

  F14TableStats::compute(h);
  F14TableStats::compute(h2);
  F14TableStats::compute(h3);
  F14TableStats::compute(h4);
  F14TableStats::compute(h5);
  F14TableStats::compute(h6);
  F14TableStats::compute(h7);
  F14TableStats::compute(h8);
  F14TableStats::compute(h9);
}

template <typename T>
void runRehash() {
  unsigned n = 10000;
  T h;
  auto b = h.bucket_count();
  for (unsigned i = 0; i < n; ++i) {
    h.insert(std::make_pair(std::to_string(i), s("")));
    if (b != h.bucket_count()) {
      F14TableStats::compute(h);
      b = h.bucket_count();
    }
  }
  EXPECT_EQ(h.size(), n);
  F14TableStats::compute(h);
}

// T should be a map from uint64_t to uint64_t
template <typename T>
void runRandom() {
  using R = std::unordered_map<uint64_t, uint64_t>;

  std::mt19937_64 gen(0);
  std::uniform_int_distribution<> pctDist(0, 100);
  std::uniform_int_distribution<uint64_t> bitsBitsDist(1, 6);
  T t0;
  T t1;
  R r0;
  R r1;

  for (std::size_t reps = 0; reps < 10000; ++reps) {
    // discardBits will be from 0 to 62
    auto discardBits = (uint64_t{1} << bitsBitsDist(gen)) - 2;
    auto k = gen() >> discardBits;
    auto v = gen();
    auto pct = pctDist(gen);

    EXPECT_EQ(t0.empty(), r0.empty());
    EXPECT_EQ(t0.size(), r0.size());
    if (pct < 15) {
      // insert
      auto t = t0.insert(std::make_pair(k, v));
      auto r = r0.insert(std::make_pair(k, v));
      EXPECT_EQ(*t.first, *r.first);
      EXPECT_EQ(t.second, r.second);
    } else if (pct < 25) {
      // emplace
      auto t = t0.emplace(k, v);
      auto r = r0.emplace(k, v);
      EXPECT_EQ(*t.first, *r.first);
      EXPECT_EQ(t.second, r.second);
    } else if (pct < 30) {
      // bulk insert
      t0.insert(r1.begin(), r1.end());
      r0.insert(r1.begin(), r1.end());
    } else if (pct < 40) {
      // erase by key
      auto t = t0.erase(k);
      auto r = r0.erase(k);
      EXPECT_EQ(t, r);
    } else if (pct < 47) {
      // erase by iterator
      if (t0.size() > 0) {
        auto r = r0.find(k);
        if (r == r0.end()) {
          r = r0.begin();
        }
        k = r->first;
        auto t = t0.find(k);
        t = t0.erase(t);
        if (t != t0.end()) {
          EXPECT_NE(t->first, k);
        }
        r = r0.erase(r);
        if (r != r0.end()) {
          EXPECT_NE(r->first, k);
        }
      }
    } else if (pct < 50) {
      // bulk erase
      if (t0.size() > 0) {
        auto r = r0.find(k);
        if (r == r0.end()) {
          r = r0.begin();
        }
        k = r->first;
        auto t = t0.find(k);
        auto firstt = t;
        auto lastt = ++t;
        t = t0.erase(firstt, lastt);
        if (t != t0.end()) {
          EXPECT_NE(t->first, k);
        }
        auto firstr = r;
        auto lastr = ++r;
        r = r0.erase(firstr, lastr);
        if (r != r0.end()) {
          EXPECT_NE(r->first, k);
        }
      }
    } else if (pct < 58) {
      // find
      auto t = t0.find(k);
      auto r = r0.find(k);
      EXPECT_EQ((t == t0.end()), (r == r0.end()));
      if (t != t0.end() && r != r0.end()) {
        EXPECT_EQ(*t, *r);
      }
      EXPECT_EQ(t0.count(k), r0.count(k));
    } else if (pct < 60) {
      // equal_range
      auto t = t0.equal_range(k);
      auto r = r0.equal_range(k);
      EXPECT_EQ((t.first == t.second), (r.first == r.second));
      if (t.first != t.second && r.first != r.second) {
        EXPECT_EQ(*t.first, *r.first);
        t.first++;
        r.first++;
        EXPECT_TRUE(t.first == t.second);
        EXPECT_TRUE(r.first == r.second);
      }
    } else if (pct < 65) {
      // iterate
      uint64_t t = 0;
      for (auto& e : t0) {
        t += e.first * 37 + e.second + 1000;
      }
      uint64_t r = 0;
      for (auto& e : r0) {
        r += e.first * 37 + e.second + 1000;
      }
      EXPECT_EQ(t, r);
    } else if (pct < 69) {
      // swap
      using std::swap;
      swap(t0, t1);
      swap(r0, r1);
    } else if (pct < 70) {
      // swap
      t0.swap(t1);
      r0.swap(r1);
    } else if (pct < 72) {
      // default construct
      t0.~T();
      new (&t0) T();
      r0.~R();
      new (&r0) R();
    } else if (pct < 74) {
      // default construct with capacity
      std::size_t capacity = k & 0xffff;
      t0.~T();
      new (&t0) T(capacity);
      r0.~R();
      new (&r0) R(capacity);
    } else if (pct < 80) {
      // bulk iterator construct
      t0.~T();
      new (&t0) T(r1.begin(), r1.end());
      r0.~R();
      new (&r0) R(r1.begin(), r1.end());
    } else if (pct < 82) {
      // initializer list construct
      auto k2 = gen() >> discardBits;
      auto v2 = gen();
      t0.~T();
      new (&t0) T({{k, v}, {k2, v}, {k2, v2}});
      r0.~R();
      new (&r0) R({{k, v}, {k2, v}, {k2, v2}});
    } else if (pct < 88) {
      // copy construct
      t0.~T();
      new (&t0) T(t1);
      r0.~R();
      new (&r0) R(r1);
    } else if (pct < 90) {
      // move construct
      t0.~T();
      new (&t0) T(std::move(t1));
      r0.~R();
      new (&r0) R(std::move(r1));
    } else if (pct < 94) {
      // copy assign
      t0 = t1;
      r0 = r1;
    } else if (pct < 96) {
      // move assign
      t0 = std::move(t1);
      r0 = std::move(r1);
    } else if (pct < 98) {
      // operator==
      EXPECT_EQ((t0 == t1), (r0 == r1));
    } else if (pct < 99) {
      // clear
      F14TableStats::compute(t0);
      t0.clear();
      r0.clear();
    } else if (pct < 100) {
      // reserve
      auto scale = std::uniform_int_distribution<>(0, 8)(gen);
      auto delta = std::uniform_int_distribution<>(-2, 2)(gen);
      std::ptrdiff_t target = (t0.size() * scale) / 4 + delta;
      if (target >= 0) {
        t0.reserve(static_cast<std::size_t>(target));
        r0.reserve(static_cast<std::size_t>(target));
      }
    }
  }
}

template <typename T>
void runPrehash() {
  T h;

  EXPECT_EQ(h.size(), 0);

  h.insert(std::make_pair(s("abc"), s("ABC")));
  EXPECT_TRUE(h.find(s("def")) == h.end());
  EXPECT_FALSE(h.find(s("abc")) == h.end());

  auto t1 = h.prehash(s("def"));
  auto t2 = h.prehash(s("abc"));
  EXPECT_TRUE(h.find(t1, s("def")) == h.end());
  EXPECT_FALSE(h.find(t2, s("abc")) == h.end());
}

TEST(F14ValueMap, simple) {
  runSimple<F14ValueMap<std::string, std::string>>();
}

TEST(F14NodeMap, simple) {
  runSimple<F14NodeMap<std::string, std::string>>();
}

TEST(F14VectorMap, simple) {
  runSimple<F14VectorMap<std::string, std::string>>();
}

TEST(F14FastMap, simple) {
  // F14FastMap inherits from a conditional typedef. Verify it compiles.
  runRandom<F14FastMap<uint64_t, uint64_t>>();
  runSimple<F14FastMap<std::string, std::string>>();
}

TEST(F14ValueMap, rehash) {
  runRehash<F14ValueMap<std::string, std::string>>();
}

TEST(F14NodeMap, rehash) {
  runRehash<F14NodeMap<std::string, std::string>>();
}

TEST(F14VectorMap, rehash) {
  runRehash<F14VectorMap<std::string, std::string>>();
}

TEST(F14ValueMap, prehash) {
  runPrehash<F14ValueMap<std::string, std::string>>();
}

TEST(F14NodeMap, prehash) {
  runPrehash<F14NodeMap<std::string, std::string>>();
}

TEST(F14ValueMap, random) {
  runRandom<F14ValueMap<uint64_t, uint64_t>>();
}

TEST(F14NodeMap, random) {
  runRandom<F14NodeMap<uint64_t, uint64_t>>();
}

TEST(F14VectorMap, random) {
  runRandom<F14VectorMap<uint64_t, uint64_t>>();
}

TEST(F14ValueMap, grow_stats) {
  F14ValueMap<uint64_t, uint64_t> h;
  for (unsigned i = 1; i <= 3072; ++i) {
    h[i]++;
  }
  // F14ValueMap just before rehash
  F14TableStats::compute(h);
  h[0]++;
  // F14ValueMap just after rehash
  F14TableStats::compute(h);
}

TEST(F14ValueMap, steady_state_stats) {
  // 10k keys, 14% probability of insert, 90% chance of erase, so the
  // table should converge to 1400 size without triggering the rehash
  // that would occur at 1536.
  F14ValueMap<uint64_t, uint64_t> h;
  std::mt19937_64 gen(0);
  std::uniform_int_distribution<> dist(0, 10000);
  for (std::size_t i = 0; i < 100000; ++i) {
    auto key = dist(gen);
    if (dist(gen) < 1400) {
      h.insert_or_assign(key, i);
    } else {
      h.erase(key);
    }
    if (((i + 1) % 10000) == 0) {
      auto stats = F14TableStats::compute(h);
      // Verify that average miss probe length is bounded despite continued
      // erase + reuse.  p99 of the average across 10M random steps is 4.69,
      // average is 2.96.
      EXPECT_LT(f14::expectedProbe(stats.missProbeLengthHisto), 10.0);
    }
  }
  // F14ValueMap at steady state
  F14TableStats::compute(h);
}

TEST(Tracked, baseline) {
  Tracked<0> a0;

  {
    resetTracking();
    Tracked<0> b0{a0};
    EXPECT_EQ(a0.val_, b0.val_);
    EXPECT_EQ(sumCounts, (Counts{1, 0, 0, 0}));
    EXPECT_EQ(Tracked<0>::counts, (Counts{1, 0, 0, 0}));
  }
  {
    resetTracking();
    Tracked<0> b0{std::move(a0)};
    EXPECT_EQ(a0.val_, b0.val_);
    EXPECT_EQ(sumCounts, (Counts{0, 1, 0, 0}));
    EXPECT_EQ(Tracked<0>::counts, (Counts{0, 1, 0, 0}));
  }
  {
    resetTracking();
    Tracked<1> b1{a0};
    EXPECT_EQ(a0.val_, b1.val_);
    EXPECT_EQ(sumCounts, (Counts{0, 0, 1, 0}));
    EXPECT_EQ(Tracked<1>::counts, (Counts{0, 0, 1, 0}));
  }
  {
    resetTracking();
    Tracked<1> b1{std::move(a0)};
    EXPECT_EQ(a0.val_, b1.val_);
    EXPECT_EQ(sumCounts, (Counts{0, 0, 0, 1}));
    EXPECT_EQ(Tracked<1>::counts, (Counts{0, 0, 0, 1}));
  }
  {
    Tracked<0> b0;
    resetTracking();
    b0 = a0;
    EXPECT_EQ(a0.val_, b0.val_);
    EXPECT_EQ(sumCounts, (Counts{0, 0, 0, 0, 1, 0}));
    EXPECT_EQ(Tracked<0>::counts, (Counts{0, 0, 0, 0, 1, 0}));
  }
  {
    Tracked<0> b0;
    resetTracking();
    b0 = std::move(a0);
    EXPECT_EQ(a0.val_, b0.val_);
    EXPECT_EQ(sumCounts, (Counts{0, 0, 0, 0, 0, 1}));
    EXPECT_EQ(Tracked<0>::counts, (Counts{0, 0, 0, 0, 0, 1}));
  }
  {
    Tracked<1> b1;
    resetTracking();
    b1 = a0;
    EXPECT_EQ(a0.val_, b1.val_);
    EXPECT_EQ(sumCounts, (Counts{0, 0, 1, 0, 0, 1}));
    EXPECT_EQ(Tracked<1>::counts, (Counts{0, 0, 1, 0, 0, 1}));
  }
  {
    Tracked<1> b1;
    resetTracking();
    b1 = std::move(a0);
    EXPECT_EQ(a0.val_, b1.val_);
    EXPECT_EQ(sumCounts, (Counts{0, 0, 0, 1, 0, 1}));
    EXPECT_EQ(Tracked<1>::counts, (Counts{0, 0, 0, 1, 0, 1}));
  }
}

// M should be a map from Tracked<0> to Tracked<1>.  F should take a map
// and a pair const& or pair&& and cause it to be inserted
template <typename M, typename F>
void runInsertCases(
    std::string const& /* name */,
    F const& insertFunc,
    uint64_t expectedDist = 0) {
  static_assert(std::is_same<typename M::key_type, Tracked<0>>::value, "");
  static_assert(std::is_same<typename M::mapped_type, Tracked<1>>::value, "");
  {
    typename M::value_type p{0, 0};
    M m;
    resetTracking();
    insertFunc(m, p);
    // fresh key, value_type const& ->
    // copy is expected
    EXPECT_EQ(
        Tracked<0>::counts.dist(Counts{1, 0, 0, 0}) +
            Tracked<1>::counts.dist(Counts{1, 0, 0, 0}),
        expectedDist);
  }
  {
    typename M::value_type p{0, 0};
    M m;
    resetTracking();
    insertFunc(m, std::move(p));
    // fresh key, value_type&& ->
    // key copy is unfortunate but required
    EXPECT_EQ(
        Tracked<0>::counts.dist(Counts{1, 0, 0, 0}) +
            Tracked<1>::counts.dist(Counts{0, 1, 0, 0}),
        expectedDist);
  }
  {
    std::pair<Tracked<0>, Tracked<1>> p{0, 0};
    M m;
    resetTracking();
    insertFunc(m, p);
    // fresh key, pair<key_type,mapped_type> const& ->
    // 1 copy is required
    EXPECT_EQ(
        Tracked<0>::counts.dist(Counts{1, 0, 0, 0}) +
            Tracked<1>::counts.dist(Counts{1, 0, 0, 0}),
        expectedDist);
  }
  {
    std::pair<Tracked<0>, Tracked<1>> p{0, 0};
    M m;
    resetTracking();
    insertFunc(m, std::move(p));
    // fresh key, pair<key_type,mapped_type>&& ->
    // this is the happy path for insert(make_pair(.., ..))
    EXPECT_EQ(
        Tracked<0>::counts.dist(Counts{0, 1, 0, 0}) +
            Tracked<1>::counts.dist(Counts{0, 1, 0, 0}),
        expectedDist);
  }
  {
    std::pair<Tracked<2>, Tracked<3>> p{0, 0};
    M m;
    resetTracking();
    insertFunc(m, p);
    // fresh key, convertible const& ->
    //   key_type ops: Tracked<0>::counts
    //   mapped_type ops: Tracked<1>::counts
    //   key_src ops: Tracked<2>::counts
    //   mapped_src ops: Tracked<3>::counts;

    // There are three strategies that could be optimal for particular
    // ratios of cost:
    //
    // - convert key and value in place to final position, destroy if
    //   insert fails. This is the strategy used by std::unordered_map
    //   and FBHashMap
    //
    // - convert key and default value in place to final position,
    //   convert value only if insert succeeds.  Nobody uses this strategy
    //
    // - convert key to a temporary, move key and convert value if
    //   insert succeeds.  This is the strategy used by F14 and what is
    //   EXPECT_EQ here.

    // The expectedDist * 3 is just a hack for the emplace-pieces-by-value
    // test, whose test harness copies the original pair and then uses
    // move conversion instead of copy conversion.
    EXPECT_EQ(
        Tracked<0>::counts.dist(Counts{0, 1, 1, 0}) +
            Tracked<1>::counts.dist(Counts{0, 0, 1, 0}) +
            Tracked<2>::counts.dist(Counts{0, 0, 0, 0}) +
            Tracked<3>::counts.dist(Counts{0, 0, 0, 0}),
        expectedDist * 3);
  }
  {
    std::pair<Tracked<2>, Tracked<3>> p{0, 0};
    M m;
    resetTracking();
    insertFunc(m, std::move(p));
    // fresh key, convertible&& ->
    //   key_type ops: Tracked<0>::counts
    //   mapped_type ops: Tracked<1>::counts
    //   key_src ops: Tracked<2>::counts
    //   mapped_src ops: Tracked<3>::counts;
    EXPECT_EQ(
        Tracked<0>::counts.dist(Counts{0, 1, 0, 1}) +
            Tracked<1>::counts.dist(Counts{0, 0, 0, 1}) +
            Tracked<2>::counts.dist(Counts{0, 0, 0, 0}) +
            Tracked<3>::counts.dist(Counts{0, 0, 0, 0}),
        expectedDist);
  }
  {
    typename M::value_type p{0, 0};
    M m;
    m[0] = 0;
    resetTracking();
    insertFunc(m, p);
    // duplicate key, value_type const&
    EXPECT_EQ(
        Tracked<0>::counts.dist(Counts{0, 0, 0, 0}) +
            Tracked<1>::counts.dist(Counts{0, 0, 0, 0}),
        expectedDist);
  }
  {
    typename M::value_type p{0, 0};
    M m;
    m[0] = 0;
    resetTracking();
    insertFunc(m, std::move(p));
    // duplicate key, value_type&&
    EXPECT_EQ(
        Tracked<0>::counts.dist(Counts{0, 0, 0, 0}) +
            Tracked<1>::counts.dist(Counts{0, 0, 0, 0}),
        expectedDist);
  }
  {
    std::pair<Tracked<0>, Tracked<1>> p{0, 0};
    M m;
    m[0] = 0;
    resetTracking();
    insertFunc(m, p);
    // duplicate key, pair<key_type,mapped_type> const&
    EXPECT_EQ(
        Tracked<0>::counts.dist(Counts{0, 0, 0, 0}) +
            Tracked<1>::counts.dist(Counts{0, 0, 0, 0}),
        expectedDist);
  }
  {
    std::pair<Tracked<0>, Tracked<1>> p{0, 0};
    M m;
    m[0] = 0;
    resetTracking();
    insertFunc(m, std::move(p));
    // duplicate key, pair<key_type,mapped_type>&&
    EXPECT_EQ(
        Tracked<0>::counts.dist(Counts{0, 0, 0, 0}) +
            Tracked<1>::counts.dist(Counts{0, 0, 0, 0}),
        expectedDist);
  }
  {
    std::pair<Tracked<2>, Tracked<3>> p{0, 0};
    M m;
    m[0] = 0;
    resetTracking();
    insertFunc(m, p);
    // duplicate key, convertible const& ->
    //   key_type ops: Tracked<0>::counts
    //   mapped_type ops: Tracked<1>::counts
    //   key_src ops: Tracked<2>::counts
    //   mapped_src ops: Tracked<3>::counts;
    EXPECT_EQ(
        Tracked<0>::counts.dist(Counts{0, 0, 1, 0}) +
            Tracked<1>::counts.dist(Counts{0, 0, 0, 0}) +
            Tracked<2>::counts.dist(Counts{0, 0, 0, 0}) +
            Tracked<3>::counts.dist(Counts{0, 0, 0, 0}),
        expectedDist * 2);
  }
  {
    std::pair<Tracked<2>, Tracked<3>> p{0, 0};
    M m;
    m[0] = 0;
    resetTracking();
    insertFunc(m, std::move(p));
    // duplicate key, convertible&& ->
    //   key_type ops: Tracked<0>::counts
    //   mapped_type ops: Tracked<1>::counts
    //   key_src ops: Tracked<2>::counts
    //   mapped_src ops: Tracked<3>::counts;
    EXPECT_EQ(
        Tracked<0>::counts.dist(Counts{0, 0, 0, 1}) +
            Tracked<1>::counts.dist(Counts{0, 0, 0, 0}) +
            Tracked<2>::counts.dist(Counts{0, 0, 0, 0}) +
            Tracked<3>::counts.dist(Counts{0, 0, 0, 0}),
        expectedDist);
  }
}

struct DoInsert {
  template <typename M, typename P>
  void operator()(M& m, P&& p) const {
    m.insert(std::forward<P>(p));
  }
};

struct DoEmplace1 {
  template <typename M, typename P>
  void operator()(M& m, P&& p) const {
    m.emplace(std::forward<P>(p));
  }
};

struct DoEmplace2 {
  template <typename M, typename U1, typename U2>
  void operator()(M& m, std::pair<U1, U2> const& p) const {
    m.emplace(p.first, p.second);
  }

  template <typename M, typename U1, typename U2>
  void operator()(M& m, std::pair<U1, U2>&& p) const {
    m.emplace(std::move(p.first), std::move(p.second));
  }
};

struct DoEmplace3 {
  template <typename M, typename U1, typename U2>
  void operator()(M& m, std::pair<U1, U2> const& p) const {
    m.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(p.first),
        std::forward_as_tuple(p.second));
  }

  template <typename M, typename U1, typename U2>
  void operator()(M& m, std::pair<U1, U2>&& p) const {
    m.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(std::move(p.first)),
        std::forward_as_tuple(std::move(p.second)));
  }
};

// Simulates use of piecewise_construct without proper use of
// forward_as_tuple.  This code doesn't yield the normal pattern, but
// it should have exactly 1 additional move or copy of the key and 1
// additional move or copy of the mapped value.
struct DoEmplace3Value {
  template <typename M, typename U1, typename U2>
  void operator()(M& m, std::pair<U1, U2> const& p) const {
    m.emplace(
        std::piecewise_construct,
        std::tuple<U1>{p.first},
        std::tuple<U2>{p.second});
  }

  template <typename M, typename U1, typename U2>
  void operator()(M& m, std::pair<U1, U2>&& p) const {
    m.emplace(
        std::piecewise_construct,
        std::tuple<U1>{std::move(p.first)},
        std::tuple<U2>{std::move(p.second)});
  }
};

template <typename M>
void runInsertAndEmplace(std::string const& name) {
  runInsertCases<M>(name + " insert", DoInsert{});
  runInsertCases<M>(name + " emplace pair", DoEmplace1{});
  runInsertCases<M>(name + " emplace k,v", DoEmplace2{});
  runInsertCases<M>(name + " emplace pieces", DoEmplace3{});
  runInsertCases<M>(name + " emplace pieces by value", DoEmplace3Value{}, 2);

  // Calling the default pair constructor via emplace is valid, but not
  // very useful in real life.  Verify that it works.
  M m;
  typename M::key_type k;
  EXPECT_EQ(m.count(k), 0);
  m.emplace();
  EXPECT_EQ(m.count(k), 1);
}

TEST(F14ValueMap, destructuring) {
  runInsertAndEmplace<F14ValueMap<Tracked<0>, Tracked<1>>>("f14value");
}

TEST(F14NodeMap, destructuring) {
  runInsertAndEmplace<F14NodeMap<Tracked<0>, Tracked<1>>>("f14node");
}

TEST(F14VectorMap, destructuring) {
  runInsertAndEmplace<F14VectorMap<Tracked<0>, Tracked<1>>>("f14vector");
}

TEST(F14VectorMap, destructuringErase) {
  using M = F14VectorMap<Tracked<0>, Tracked<1>>;
  typename M::value_type p1{0, 0};
  typename M::value_type p2{2, 2};
  M m;
  m.insert(p1);
  m.insert(p2);

  resetTracking();
  m.erase(p1.first);
  LOG(INFO) << "erase -> "
            << "key_type ops " << Tracked<0>::counts << ", mapped_type ops "
            << Tracked<1>::counts;
  // deleting p1 will cause p2 to be moved to the front of the values array
  EXPECT_EQ(
      Tracked<0>::counts.dist(Counts{0, 1, 0, 0}) +
          Tracked<1>::counts.dist(Counts{0, 1, 0, 0}),
      0);
}

TEST(F14ValueMap, vectorMaxSize) {
  F14ValueMap<int, int> m;
  EXPECT_EQ(
      m.max_size(),
      std::numeric_limits<uint64_t>::max() / sizeof(std::pair<int, int>));
}

TEST(F14NodeMap, vectorMaxSize) {
  F14NodeMap<int, int> m;
  EXPECT_EQ(
      m.max_size(),
      std::numeric_limits<uint64_t>::max() / sizeof(std::pair<int, int>));
}

TEST(F14VectorMap, vectorMaxSize) {
  F14VectorMap<int, int> m;
  EXPECT_EQ(m.max_size(), std::numeric_limits<uint32_t>::max());
}

template <typename M>
void runMoveOnlyTest() {
  M t0;
  t0[10] = 20;
  t0.emplace(30, 40);
  t0.insert(std::make_pair(50, 60));
  M t1{std::move(t0)};
  EXPECT_TRUE(t0.empty());
  M t2;
  EXPECT_TRUE(t2.empty());
  t2 = std::move(t1);
  EXPECT_EQ(t2.size(), 3);
}

TEST(F14ValueMap, moveOnly) {
  runMoveOnlyTest<F14ValueMap<f14::MoveOnlyTestInt, int>>();
  runMoveOnlyTest<F14ValueMap<int, f14::MoveOnlyTestInt>>();
  runMoveOnlyTest<F14ValueMap<f14::MoveOnlyTestInt, f14::MoveOnlyTestInt>>();
}

TEST(F14NodeMap, moveOnly) {
  runMoveOnlyTest<F14NodeMap<f14::MoveOnlyTestInt, int>>();
  runMoveOnlyTest<F14NodeMap<int, f14::MoveOnlyTestInt>>();
  runMoveOnlyTest<F14NodeMap<f14::MoveOnlyTestInt, f14::MoveOnlyTestInt>>();
}

TEST(F14VectorMap, moveOnly) {
  runMoveOnlyTest<F14VectorMap<f14::MoveOnlyTestInt, int>>();
  runMoveOnlyTest<F14VectorMap<int, f14::MoveOnlyTestInt>>();
  runMoveOnlyTest<F14VectorMap<f14::MoveOnlyTestInt, f14::MoveOnlyTestInt>>();
}

TEST(F14FastMap, moveOnly) {
  runMoveOnlyTest<F14FastMap<f14::MoveOnlyTestInt, int>>();
  runMoveOnlyTest<F14FastMap<int, f14::MoveOnlyTestInt>>();
  runMoveOnlyTest<F14FastMap<f14::MoveOnlyTestInt, f14::MoveOnlyTestInt>>();
}

TEST(F14ValueMap, heterogeneous) {
  // note: std::string is implicitly convertible to but not from StringPiece
  using Hasher = folly::transparent<folly::hasher<folly::StringPiece>>;
  using KeyEqual = folly::transparent<std::equal_to<folly::StringPiece>>;

  constexpr auto hello = "hello"_sp;
  constexpr auto buddy = "buddy"_sp;
  constexpr auto world = "world"_sp;

  F14ValueMap<std::string, bool, Hasher, KeyEqual> map;
  map.emplace(hello.str(), true);
  map.emplace(world.str(), false);

  auto checks = [hello, buddy](auto& ref) {
    // count
    EXPECT_EQ(0, ref.count(buddy));
    EXPECT_EQ(1, ref.count(hello));

    // find
    EXPECT_TRUE(ref.end() == ref.find(buddy));
    EXPECT_EQ(hello, ref.find(hello)->first);

    // prehash + find
    EXPECT_TRUE(ref.end() == ref.find(ref.prehash(buddy), buddy));
    EXPECT_EQ(hello, ref.find(ref.prehash(hello), hello)->first);

    // equal_range
    EXPECT_TRUE(std::make_pair(ref.end(), ref.end()) == ref.equal_range(buddy));
    EXPECT_TRUE(
        std::make_pair(ref.find(hello), ++ref.find(hello)) ==
        ref.equal_range(hello));
  };

  checks(map);
  checks(folly::as_const(map));
}

///////////////////////////////////
#endif // FOLLY_F14_VECTOR_INTRINSICS_AVAILABLE
///////////////////////////////////
