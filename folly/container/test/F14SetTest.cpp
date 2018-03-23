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

#include <folly/container/F14Set.h>
#include <folly/container/test/F14TestUtil.h>
#include <folly/portability/GTest.h>

template <template <typename, typename, typename, typename> class TSet>
void testCustomSwap() {
  using std::swap;

  TSet<
      int,
      folly::f14::DefaultHasher<int>,
      folly::f14::DefaultKeyEqual<int>,
      folly::f14::SwapTrackingAlloc<int>>
      m0, m1;
  folly::f14::resetTracking();
  swap(m0, m1);

  EXPECT_EQ(
      0, folly::f14::Tracked<0>::counts.dist(folly::f14::Counts{0, 0, 0, 0}));
}

TEST(F14Set, customSwap) {
  testCustomSwap<folly::F14ValueSet>();
  testCustomSwap<folly::F14NodeSet>();
  testCustomSwap<folly::F14VectorSet>();
  testCustomSwap<folly::F14FastSet>();
}

///////////////////////////////////
#if FOLLY_F14_VECTOR_INTRINSICS_AVAILABLE
///////////////////////////////////

#include <chrono>
#include <random>
#include <string>
#include <unordered_set>

#include <folly/Range.h>

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

  h.insert(s("abc"));
  EXPECT_TRUE(h.find(s("def")) == h.end());
  EXPECT_FALSE(h.find(s("abc")) == h.end());
  h.insert(s("ghi"));
  EXPECT_EQ(h.size(), 2);
  h.erase(h.find(s("abc")));
  EXPECT_EQ(h.size(), 1);

  T h2(std::move(h));
  EXPECT_EQ(h.size(), 0);
  EXPECT_TRUE(h.begin() == h.end());
  EXPECT_EQ(h2.size(), 1);

  EXPECT_TRUE(h2.find(s("abc")) == h2.end());
  EXPECT_EQ(*h2.begin(), s("ghi"));
  {
    auto i = h2.begin();
    EXPECT_FALSE(i == h2.end());
    ++i;
    EXPECT_TRUE(i == h2.end());
  }

  T h3;
  h3.insert(s("xxx"));
  h3.insert(s("yyy"));
  h3 = std::move(h2);
  EXPECT_EQ(h2.size(), 0);
  EXPECT_EQ(h3.size(), 1);
  EXPECT_TRUE(h3.find(s("xxx")) == h3.end());

  for (uint64_t i = 0; i < 1000; ++i) {
    h.insert(std::move(std::to_string(i * i * i)));
    EXPECT_EQ(h.size(), i + 1);
  }
  {
    using std::swap;
    swap(h, h2);
  }
  for (uint64_t i = 0; i < 1000; ++i) {
    EXPECT_TRUE(h2.find(std::to_string(i * i * i)) != h2.end());
    EXPECT_EQ(*h2.find(std::to_string(i * i * i)), std::to_string(i * i * i));
    EXPECT_TRUE(h2.find(std::to_string(i * i * i + 2)) == h2.end());
  }

  T h4{h2};
  EXPECT_EQ(h2.size(), 1000);
  EXPECT_EQ(h4.size(), 1000);

  T h5{std::move(h2)};
  T h6;
  h6 = h4;
  T h7 = h4;

  T h8({s("abc"), s("def")});
  T h9({s("abd"), s("def")});
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

  F14TableStats::compute(h);
  F14TableStats::compute(h2);
  F14TableStats::compute(h3);
  F14TableStats::compute(h4);
  F14TableStats::compute(h5);
  F14TableStats::compute(h6);
  F14TableStats::compute(h7);
  F14TableStats::compute(h8);
}

template <typename T>
void runRehash() {
  unsigned n = 10000;
  T h;
  for (unsigned i = 0; i < n; ++i) {
    h.insert(std::to_string(i));
  }
  EXPECT_EQ(h.size(), n);
  F14TableStats::compute(h);
}

// T should be a set of uint64_t
template <typename T>
void runRandom() {
  using R = std::unordered_set<uint64_t>;

  std::mt19937_64 gen(0);
  std::uniform_int_distribution<> pctDist(0, 100);
  std::uniform_int_distribution<uint64_t> bitsBitsDist(1, 6);
  T t0;
  T t1;
  R r0;
  R r1;

  for (std::size_t reps = 0; reps < 100000; ++reps) {
    // discardBits will be from 0 to 62
    auto discardBits = (uint64_t{1} << bitsBitsDist(gen)) - 2;
    auto k = gen() >> discardBits;
    auto pct = pctDist(gen);

    EXPECT_EQ(t0.size(), r0.size());
    if (pct < 15) {
      // insert
      auto t = t0.insert(k);
      auto r = r0.insert(k);
      EXPECT_EQ(t.second, r.second);
      EXPECT_EQ(*t.first, *r.first);
    } else if (pct < 25) {
      // emplace
      auto t = t0.emplace(k);
      auto r = r0.emplace(k);
      EXPECT_EQ(t.second, r.second);
      EXPECT_EQ(*t.first, *r.first);
    } else if (pct < 30) {
      // bulk insert
      t0.insert(t1.begin(), t1.end());
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
        k = *r;
        auto t = t0.find(k);
        t = t0.erase(t);
        if (t != t0.end()) {
          EXPECT_NE(*t, k);
        }
        r = r0.erase(r);
        if (r != r0.end()) {
          EXPECT_NE(*r, k);
        }
      }
    } else if (pct < 50) {
      // bulk erase
      if (t0.size() > 0) {
        auto r = r0.find(k);
        if (r == r0.end()) {
          r = r0.begin();
        }
        k = *r;
        auto t = t0.find(k);
        auto firstt = t;
        auto lastt = ++t;
        t = t0.erase(firstt, lastt);
        if (t != t0.end()) {
          EXPECT_NE(*t, k);
        }
        auto firstr = r;
        auto lastr = ++r;
        r = r0.erase(firstr, lastr);
        if (r != r0.end()) {
          EXPECT_NE(*r, k);
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
        t += e + 1000;
      }
      uint64_t r = 0;
      for (auto& e : r0) {
        r += e + 1000;
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
      t0.~T();
      new (&t0) T({k, k, k2});
      r0.~R();
      new (&r0) R({k, k, k2});
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
      t0.computeStats();
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

TEST(F14ValueSet, simple) {
  runSimple<F14ValueSet<std::string>>();
}

TEST(F14NodeSet, simple) {
  runSimple<F14NodeSet<std::string>>();
}

TEST(F14VectorSet, simple) {
  runSimple<F14VectorSet<std::string>>();
}

TEST(F14FastSet, simple) {
  // F14FastSet inherits from a conditional typedef. Verify it compiles.
  runRandom<F14FastSet<uint64_t>>();
  runSimple<F14FastSet<std::string>>();
}

TEST(F14ValueSet, rehash) {
  runRehash<F14ValueSet<std::string>>();
}

TEST(F14NodeSet, rehash) {
  runRehash<F14NodeSet<std::string>>();
}

TEST(F14VectorSet, rehash) {
  runRehash<F14VectorSet<std::string>>();
}

TEST(F14ValueSet, random) {
  runRandom<F14ValueSet<uint64_t>>();
}

TEST(F14NodeSet, random) {
  runRandom<F14NodeSet<uint64_t>>();
}

TEST(F14VectorSet, random) {
  runRandom<F14VectorSet<uint64_t>>();
}

TEST(F14ValueSet, grow_stats) {
  F14ValueSet<uint64_t> h;
  for (unsigned i = 1; i <= 3072; ++i) {
    h.insert(i);
  }
  // F14ValueSet just before rehash
  F14TableStats::compute(h);
  h.insert(0);
  // F14ValueSet just after rehash
  F14TableStats::compute(h);
}

TEST(F14ValueSet, steady_state_stats) {
  // 10k keys, 14% probability of insert, 90% chance of erase, so the
  // table should converge to 1400 size without triggering the rehash
  // that would occur at 1536.
  F14ValueSet<uint64_t> h;
  std::mt19937 gen(0);
  std::uniform_int_distribution<> dist(0, 10000);
  for (std::size_t i = 0; i < 100000; ++i) {
    auto key = dist(gen);
    if (dist(gen) < 1400) {
      h.insert(key);
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
  // F14ValueSet at steady state
  F14TableStats::compute(h);
}

TEST(F14ValueSet, vectorMaxSize) {
  F14ValueSet<int> s;
  EXPECT_EQ(s.max_size(), std::numeric_limits<uint64_t>::max() / sizeof(int));
}

TEST(F14NodeSet, vectorMaxSize) {
  F14NodeSet<int> s;
  EXPECT_EQ(s.max_size(), std::numeric_limits<uint64_t>::max() / sizeof(int));
}

TEST(F14VectorSet, vectorMaxSize) {
  F14VectorSet<int> s;
  EXPECT_EQ(s.max_size(), std::numeric_limits<uint32_t>::max());
}

template <typename S>
void runMoveOnlyTest() {
  S t0;
  t0.emplace(10);
  t0.insert(20);
  S t1{std::move(t0)};
  EXPECT_TRUE(t0.empty());
  S t2;
  EXPECT_TRUE(t2.empty());
  t2 = std::move(t1);
  EXPECT_EQ(t2.size(), 2);
}

TEST(F14ValueSet, moveOnly) {
  runMoveOnlyTest<F14ValueSet<f14::MoveOnlyTestInt>>();
}

TEST(F14NodeSet, moveOnly) {
  runMoveOnlyTest<F14NodeSet<f14::MoveOnlyTestInt>>();
}

TEST(F14VectorSet, moveOnly) {
  runMoveOnlyTest<F14VectorSet<f14::MoveOnlyTestInt>>();
}

TEST(F14FastSet, moveOnly) {
  runMoveOnlyTest<F14FastSet<f14::MoveOnlyTestInt>>();
}

TEST(F14ValueSet, heterogeneous) {
  // note: std::string is implicitly convertible to but not from StringPiece
  using Hasher = folly::transparent<folly::hasher<folly::StringPiece>>;
  using KeyEqual = folly::transparent<std::equal_to<folly::StringPiece>>;

  constexpr auto hello = "hello"_sp;
  constexpr auto buddy = "buddy"_sp;
  constexpr auto world = "world"_sp;

  F14ValueSet<std::string, Hasher, KeyEqual> set;
  set.emplace(hello.str());
  set.emplace(world.str());

  auto checks = [hello, buddy](auto& ref) {
    // count
    EXPECT_EQ(0, ref.count(buddy));
    EXPECT_EQ(1, ref.count(hello));

    // find
    EXPECT_TRUE(ref.end() == ref.find(buddy));
    EXPECT_EQ(hello, *ref.find(hello));

    // prehash + find
    EXPECT_TRUE(ref.end() == ref.find(ref.prehash(buddy), buddy));
    EXPECT_EQ(hello, *ref.find(ref.prehash(hello), hello));

    // equal_range
    EXPECT_TRUE(std::make_pair(ref.end(), ref.end()) == ref.equal_range(buddy));
    EXPECT_TRUE(
        std::make_pair(ref.find(hello), ++ref.find(hello)) ==
        ref.equal_range(hello));
  };

  checks(set);
  checks(folly::as_const(set));
}

///////////////////////////////////
#endif // FOLLY_F14_VECTOR_INTRINSICS_AVAILABLE
///////////////////////////////////
