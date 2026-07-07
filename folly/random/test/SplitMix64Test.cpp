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

#include <folly/random/splitmix64.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <numeric>
#include <random>
#include <set>
#include <sstream>
#include <type_traits>
#include <vector>

#include <folly/portability/GTest.h>

namespace folly {
namespace {

constexpr uint64_t kTestSeed = 0x1234567890ABCDEF;

TEST(SplitMix64Test, SatisfiesUniformRandomBitGeneratorConcept) {
  static_assert(std::is_same_v<splitmix64_engine::result_type, uint64_t>);
  static_assert(std::uniform_random_bit_generator<splitmix64_engine>);

  EXPECT_EQ(splitmix64_engine::min(), std::numeric_limits<uint64_t>::min());
  EXPECT_EQ(splitmix64_engine::max(), std::numeric_limits<uint64_t>::max());
}

TEST(SplitMix64Test, DifferentSeedsProduceDifferentSequences) {
  splitmix64_engine rng1(kTestSeed);
  splitmix64_engine rng2(~kTestSeed);
  bool foundDifference = false;
  for (int i = 0; i < 100 && !foundDifference; ++i) {
    foundDifference = rng1() != rng2();
  }
  EXPECT_TRUE(foundDifference);
}

TEST(SplitMix64Test, Reseeding) {
  splitmix64_engine rng(kTestSeed);
  std::array<uint64_t, 10> initial;
  std::generate(initial.begin(), initial.end(), std::ref(rng));

  rng.seed(~kTestSeed);
  std::array<uint64_t, 10> reseeded;
  std::generate(reseeded.begin(), reseeded.end(), std::ref(rng));
  EXPECT_NE(initial, reseeded);

  rng.seed(kTestSeed);
  std::array<uint64_t, 10> reproduced;
  std::generate(reproduced.begin(), reproduced.end(), std::ref(rng));
  EXPECT_EQ(initial, reproduced);
}

TEST(SplitMix64Test, SeedSeqInitialization) {
  std::array<uint32_t, 10> seedData = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  std::seed_seq seedSeq(seedData.begin(), seedData.end());

  splitmix64_engine rng1(seedSeq);
  splitmix64_engine rng2(seedSeq);
  // A seed_seq must seed deterministically.
  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(rng1(), rng2());
  }
}

// discard(z) must be equivalent to invoking the engine z times.
TEST(SplitMix64Test, DiscardEquivalentToDraws) {
  constexpr unsigned long long kSkip = 137;
  splitmix64_engine discarded(kTestSeed);
  splitmix64_engine drawn(kTestSeed);

  discarded.discard(kSkip);
  for (unsigned long long i = 0; i < kSkip; ++i) {
    drawn();
  }
  EXPECT_EQ(discarded, drawn);
  EXPECT_EQ(discarded(), drawn());
}

// operator<< / operator>> must round-trip the full engine state.
TEST(SplitMix64Test, StreamRoundTrip) {
  splitmix64_engine rng(kTestSeed);
  for (int i = 0; i < 5; ++i) {
    rng();
  }

  std::stringstream ss;
  ss << rng;

  splitmix64_engine restored;
  ss >> restored;
  EXPECT_EQ(rng, restored);
  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(rng(), restored());
  }
}

TEST(SplitMix64Test, StdLibCompatibility) {
  splitmix64_engine rng(kTestSeed);
  std::uniform_int_distribution<int> dist(1, 100);
  for (int i = 0; i < 1000; ++i) {
    int value = dist(rng);
    EXPECT_GE(value, 1);
    EXPECT_LE(value, 100);
  }

  std::vector<int> values(100);
  std::iota(values.begin(), values.end(), 1);
  std::shuffle(values.begin(), values.end(), rng);
  std::set<int> uniqueValues(values.begin(), values.end());
  EXPECT_EQ(uniqueValues.size(), 100);
}

} // namespace
} // namespace folly
