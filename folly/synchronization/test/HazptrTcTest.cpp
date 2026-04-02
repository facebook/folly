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

#include <folly/synchronization/Hazptr.h>

#include <folly/lang/Bits.h>
#include <folly/portability/GTest.h>

namespace folly {
template <template <typename> class Atom>
struct hazptr_tc_tester {
  using TC = hazptr_tc<Atom>;
  using Rec = hazptr_rec<Atom>;

  static constexpr size_t kCapacity = TC::kCapacity;
  static constexpr size_t kDecayThreshold = TC::kDecayThreshold;

  static Rec* try_get(TC& tc) { return tc.try_get(); }
  static bool try_put(TC& tc, Rec* hprec) { return tc.try_put(hprec); }
  static void fill(TC& tc, size_t num) { tc.fill(num); }
  static void evict(TC& tc, size_t num) { tc.evict(num); }
  static void evict_all(TC& tc) { tc.evict(); }
};
} // namespace folly

using folly::hazptr_tc;
using TcTester = folly::hazptr_tc_tester<std::atomic>;

static constexpr size_t kCap = TcTester::kCapacity;
static constexpr size_t kDecay = TcTester::kDecayThreshold;

struct HazptrTcTest : public ::testing::Test {};

TEST_F(HazptrTcTest, InitialState) {
  hazptr_tc<> tc;
  EXPECT_EQ(tc.min_capacity(), kCap);
  EXPECT_EQ(tc.count(), 0);
  EXPECT_EQ(tc.allocated_capacity(), 0);
}

TEST_F(HazptrTcTest, Fill) {
  hazptr_tc<> tc;
  TcTester::fill(tc, 5);
  EXPECT_EQ(tc.count(), 5);
}

TEST_F(HazptrTcTest, FillToCapacity) {
  hazptr_tc<> tc;
  TcTester::fill(tc, kCap);
  EXPECT_EQ(tc.count(), kCap);
  EXPECT_EQ(tc.allocated_capacity(), kCap);
}

TEST_F(HazptrTcTest, TryGetFromPrimary) {
  hazptr_tc<> tc;
  TcTester::fill(tc, 5);
  auto* rec = TcTester::try_get(tc);
  ASSERT_NE(rec, nullptr);
  EXPECT_EQ(tc.count(), 4);
  // Put it back so the destructor can release it.
  TcTester::try_put(tc, rec);
}

TEST_F(HazptrTcTest, TryGetMultiple) {
  hazptr_tc<> tc;
  TcTester::fill(tc, 3);
  auto* r0 = TcTester::try_get(tc);
  auto* r1 = TcTester::try_get(tc);
  auto* r2 = TcTester::try_get(tc);
  ASSERT_NE(r0, nullptr);
  ASSERT_NE(r1, nullptr);
  ASSERT_NE(r2, nullptr);
  EXPECT_EQ(tc.count(), 0);
  // Put them all back.
  TcTester::try_put(tc, r0);
  TcTester::try_put(tc, r1);
  TcTester::try_put(tc, r2);
}

TEST_F(HazptrTcTest, TryGetEmpty) {
  hazptr_tc<> tc;
  auto* rec = TcTester::try_get(tc);
  EXPECT_EQ(rec, nullptr);
  EXPECT_EQ(tc.count(), 0);
}

TEST_F(HazptrTcTest, TryPutToPrimary) {
  hazptr_tc<> tc;
  TcTester::fill(tc, 1);
  auto* rec = TcTester::try_get(tc);
  ASSERT_NE(rec, nullptr);
  EXPECT_EQ(tc.count(), 0);
  bool ok = TcTester::try_put(tc, rec);
  EXPECT_TRUE(ok);
  EXPECT_EQ(tc.count(), 1);
}

TEST_F(HazptrTcTest, TryPutOverflow) {
  // When the vector is full, try_put triggers try_put_slow which grows
  // the vector and stores the new record.
  hazptr_tc<> tc;
  TcTester::fill(tc, kCap);
  EXPECT_EQ(tc.count(), kCap);
  EXPECT_EQ(tc.allocated_capacity(), kCap);
  // Get one record to have a spare for putting back.
  auto* rec = TcTester::try_get(tc);
  TcTester::fill(tc, 1);
  // Now count == kCap and capacity == kCap. try_put triggers try_put_slow.
  bool ok = TcTester::try_put(tc, rec);
  EXPECT_TRUE(ok);
  EXPECT_EQ(tc.count(), kCap + 1);
  EXPECT_GT(tc.allocated_capacity(), kCap);
}

TEST_F(HazptrTcTest, TryGetEmptyBoth) {
  // When empty, try_get returns nullptr.
  hazptr_tc<> tc;
  EXPECT_EQ(tc.count(), 0);
  auto* rec = TcTester::try_get(tc);
  EXPECT_EQ(rec, nullptr);
  EXPECT_EQ(tc.count(), 0);
}

TEST_F(HazptrTcTest, EvictReleasesToDomain) {
  // evict(num) releases records to domain, reducing count.
  hazptr_tc<> tc;
  TcTester::fill(tc, 10);
  EXPECT_EQ(tc.count(), 10);
  TcTester::evict(tc, 4);
  EXPECT_EQ(tc.count(), 6);
}

TEST_F(HazptrTcTest, EvictZero) {
  hazptr_tc<> tc;
  TcTester::fill(tc, 5);
  TcTester::evict(tc, 0);
  EXPECT_EQ(tc.count(), 5);
}

TEST_F(HazptrTcTest, EvictAll) {
  // evict() (no-arg) releases all records to domain.
  hazptr_tc<> tc;
  TcTester::fill(tc, kCap);
  // Grow beyond kCap.
  auto* rec = TcTester::try_get(tc);
  TcTester::fill(tc, 1);
  TcTester::try_put(tc, rec);
  EXPECT_GT(tc.count(), kCap);
  TcTester::evict_all(tc);
  EXPECT_EQ(tc.count(), 0);
}

TEST_F(HazptrTcTest, EvictAllEmpty) {
  // evict() on an empty tc is a no-op.
  hazptr_tc<> tc;
  TcTester::evict_all(tc);
  EXPECT_EQ(tc.count(), 0);
}

TEST_F(HazptrTcTest, FillFromDomain) {
  // fill() always gets records from domain.
  hazptr_tc<> tc;
  TcTester::fill(tc, 5);
  EXPECT_EQ(tc.count(), 5);
}

TEST_F(HazptrTcTest, TryPutOverflowRepeated) {
  // Repeated puts beyond capacity grow count.
  hazptr_tc<> tc;
  TcTester::fill(tc, kCap);
  // First overflow.
  auto* r0 = TcTester::try_get(tc);
  TcTester::fill(tc, 1);
  TcTester::try_put(tc, r0);
  EXPECT_EQ(tc.count(), kCap + 1);
  // Put more records to grow further.
  hazptr_tc<> helper;
  TcTester::fill(helper, 1);
  auto* r1 = TcTester::try_get(helper);
  TcTester::try_put(tc, r1);
  EXPECT_EQ(tc.count(), kCap + 2);
}

TEST_F(HazptrTcTest, Decay) {
  // After kDecay fast-path try_put calls with count > kCap, decay fires.
  // Since excess was not drawn from during the window, it shrinks.
  hazptr_tc<> tc;
  TcTester::fill(tc, kCap);
  // Grow beyond kCap.
  auto* rec = TcTester::try_get(tc);
  TcTester::fill(tc, 1);
  TcTester::try_put(tc, rec);
  size_t countAfterGrow = tc.count();
  EXPECT_GT(countAfterGrow, kCap);
  size_t allocCap = tc.allocated_capacity();
  // Do kDecay try_get/try_put cycles on the hot path.
  // These cycle the top entry, keeping count > kCap to tick decay.
  for (size_t i = 0; i < kDecay; ++i) {
    auto* r = TcTester::try_get(tc);
    ASSERT_NE(r, nullptr);
    TcTester::try_put(tc, r);
  }
  // Decay should have shrunk excess.
  EXPECT_LE(tc.count(), countAfterGrow);
  EXPECT_LE(tc.allocated_capacity(), allocCap);
}

TEST_F(HazptrTcTest, DecayRepeated) {
  // Repeated decay shrinks count and allocation progressively.
  hazptr_tc<> tc;
  TcTester::fill(tc, kCap);
  auto* rec = TcTester::try_get(tc);
  TcTester::fill(tc, 1);
  TcTester::try_put(tc, rec);
  size_t prevCount = tc.count();
  size_t prevCap = tc.allocated_capacity();
  // First decay.
  for (size_t i = 0; i < kDecay; ++i) {
    auto* r = TcTester::try_get(tc);
    TcTester::try_put(tc, r);
  }
  EXPECT_LE(tc.count(), prevCount);
  EXPECT_LE(tc.allocated_capacity(), prevCap);
  prevCount = tc.count();
  prevCap = tc.allocated_capacity();
  // Second decay.
  for (size_t i = 0; i < kDecay; ++i) {
    auto* r = TcTester::try_get(tc);
    TcTester::try_put(tc, r);
  }
  EXPECT_LE(tc.count(), prevCount);
  EXPECT_LE(tc.allocated_capacity(), prevCap);
}

TEST_F(HazptrTcTest, DecayNotTriggeredWithinCapacity) {
  // When count <= kCap, decay is never triggered.
  hazptr_tc<> tc;
  TcTester::fill(tc, 5);
  for (size_t i = 0; i < 2 * kDecay; ++i) {
    auto* r = TcTester::try_get(tc);
    ASSERT_NE(r, nullptr);
    TcTester::try_put(tc, r);
  }
  EXPECT_EQ(tc.count(), 5);
}

TEST_F(HazptrTcTest, DecayResetByOverflowUse) {
  // try_put_slow resets put_tick_ to 0, so active use delays decay.
  hazptr_tc<> tc;
  TcTester::fill(tc, kCap);
  auto* rec = TcTester::try_get(tc);
  TcTester::fill(tc, 1);
  TcTester::try_put(tc, rec);
  size_t countAfterFirstGrow = tc.count();
  EXPECT_GT(countAfterFirstGrow, kCap);
  // Do kDecay / 2 cycles (well short of the threshold).
  for (size_t i = 0; i < kDecay / 2; ++i) {
    auto* r = TcTester::try_get(tc);
    TcTester::try_put(tc, r);
  }
  EXPECT_EQ(tc.count(), countAfterFirstGrow); // no decay yet
  // Grow again via try_put_slow — resets put_tick_ to 0.
  // Fill tc to capacity first so next try_put triggers try_put_slow.
  hazptr_tc<> helper;
  size_t need = tc.allocated_capacity() - tc.count();
  for (size_t i = 0; i < need; ++i) {
    TcTester::fill(helper, 1);
    TcTester::try_put(tc, TcTester::try_get(helper));
  }
  TcTester::fill(helper, 1);
  TcTester::try_put(tc, TcTester::try_get(helper)); // triggers try_put_slow
  size_t countAfterSecondGrow = tc.count();
  EXPECT_GT(countAfterSecondGrow, countAfterFirstGrow);
  // Another kDecay / 2 cycles — no decay since put_tick_ was reset.
  for (size_t i = 0; i < kDecay / 2; ++i) {
    auto* r = TcTester::try_get(tc);
    TcTester::try_put(tc, r);
  }
  EXPECT_EQ(tc.count(), countAfterSecondGrow); // still no decay
}

TEST_F(HazptrTcTest, Destructor) {
  // Destructor releases all records to domain.
  {
    hazptr_tc<> tc;
    TcTester::fill(tc, kCap);
    auto* rec = TcTester::try_get(tc);
    TcTester::fill(tc, 1);
    TcTester::try_put(tc, rec);
  }
}

TEST_F(HazptrTcTest, DestructorPrimaryOnly) {
  // Destructor with records only within kCap.
  {
    hazptr_tc<> tc;
    TcTester::fill(tc, 10);
  }
}

TEST_F(HazptrTcTest, DestructorEmpty) {
  // Destructor with no records at all.
  {
    hazptr_tc<> tc;
  }
}

TEST_F(HazptrTcTest, RoundTrip) {
  // Records put back via try_put are the same ones returned by try_get.
  hazptr_tc<> tc;
  TcTester::fill(tc, 3);
  auto* r0 = TcTester::try_get(tc);
  auto* r1 = TcTester::try_get(tc);
  auto* r2 = TcTester::try_get(tc);
  // Put in reverse order.
  TcTester::try_put(tc, r2);
  TcTester::try_put(tc, r1);
  TcTester::try_put(tc, r0);
  // Get in LIFO order — should match reverse of put order.
  EXPECT_EQ(TcTester::try_get(tc), r0);
  EXPECT_EQ(TcTester::try_get(tc), r1);
  EXPECT_EQ(TcTester::try_get(tc), r2);
  // Put them back for cleanup.
  TcTester::try_put(tc, r0);
  TcTester::try_put(tc, r1);
  TcTester::try_put(tc, r2);
}

TEST_F(HazptrTcTest, DecayShrinksSizeAndCapacity) {
  // After one decay window with no excess usage, count and allocation shrink.
  hazptr_tc<> tc;
  TcTester::fill(tc, kCap);
  // Grow well beyond kCap by adding multiple records.
  hazptr_tc<> helper;
  for (size_t i = 0; i < 3 * kCap; ++i) {
    TcTester::fill(helper, 1);
    TcTester::try_put(tc, TcTester::try_get(helper));
  }
  size_t countBefore = tc.count();
  size_t capBefore = tc.allocated_capacity();
  EXPECT_GT(countBefore, kCap);
  // Run a decay window with no excess usage.
  for (size_t i = 0; i < kDecay; ++i) {
    auto* r = TcTester::try_get(tc);
    TcTester::try_put(tc, r);
  }
  EXPECT_LT(tc.count(), countBefore);
  EXPECT_LE(tc.allocated_capacity(), capBefore);
}

TEST_F(HazptrTcTest, DecayRepeatedShrink) {
  // Multiple decay windows shrink progressively. Capacity bottoms out at kCap.
  hazptr_tc<> tc;
  TcTester::fill(tc, kCap);
  hazptr_tc<> helper;
  for (size_t i = 0; i < 6 * kCap; ++i) {
    TcTester::fill(helper, 1);
    TcTester::try_put(tc, TcTester::try_get(helper));
  }
  size_t prevCount = tc.count();
  size_t prevCap = tc.allocated_capacity();
  // Decay windows until stable.
  size_t stableCount;
  do {
    for (size_t i = 0; i < kDecay; ++i) {
      auto* r = TcTester::try_get(tc);
      TcTester::try_put(tc, r);
    }
    stableCount = tc.count();
    EXPECT_LE(stableCount, prevCount);
    EXPECT_LE(tc.allocated_capacity(), prevCap);
    EXPECT_GE(tc.allocated_capacity(), kCap);
    prevCount = stableCount;
    prevCap = tc.allocated_capacity();
  } while (stableCount > kCap);

  // Verify the stable state persists.
  for (size_t i = 0; i < kDecay; ++i) {
    auto* r = TcTester::try_get(tc);
    TcTester::try_put(tc, r);
  }
  EXPECT_EQ(tc.count(), stableCount);
}

TEST_F(HazptrTcTest, AllocatedCapacityPowerOfTwo) {
  // Allocated capacity is always a power of 2 (or 0 if never used).
  hazptr_tc<> tc;
  EXPECT_EQ(tc.allocated_capacity(), 0);
  TcTester::fill(tc, kCap);
  size_t cap = tc.allocated_capacity();
  EXPECT_GT(cap, 0);
  EXPECT_TRUE(folly::isPowTwo(cap));
  // Grow and verify capacity stays a power of 2.
  hazptr_tc<> helper;
  for (int round = 0; round < 5; ++round) {
    TcTester::fill(helper, 1);
    TcTester::try_put(tc, TcTester::try_get(helper));
    cap = tc.allocated_capacity();
    EXPECT_TRUE(folly::isPowTwo(cap));
  }
  // Decay and verify capacity remains a power of 2.
  for (int round = 0; round < 5; ++round) {
    for (size_t i = 0; i < kDecay; ++i) {
      auto* r = TcTester::try_get(tc);
      if (r) {
        TcTester::try_put(tc, r);
      }
    }
    cap = tc.allocated_capacity();
    EXPECT_TRUE(folly::isPowTwo(cap));
  }
}

TEST_F(HazptrTcTest, AllocatedCapacityGrowth) {
  // Pushing beyond capacity correctly grows allocation as a power of 2
  // that is at least kCapacity.
  hazptr_tc<> tc;
  hazptr_tc<> helper;
  TcTester::fill(tc, kCap);
  size_t expectedCap = std::max(kCap, folly::nextPowTwo(kCap));
  EXPECT_EQ(tc.allocated_capacity(), expectedCap);
  // Grow by one: triggers try_put_slow.
  TcTester::fill(helper, 1);
  TcTester::try_put(tc, TcTester::try_get(helper));
  EXPECT_EQ(tc.count(), kCap + 1);
  expectedCap = std::max(kCap, folly::nextPowTwo(kCap + 1u));
  EXPECT_EQ(tc.allocated_capacity(), expectedCap);
}
