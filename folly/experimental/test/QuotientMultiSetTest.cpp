/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <folly/experimental/QuotientMultiSet.h>

#include <random>

#include <folly/Format.h>
#include <folly/Random.h>
#include <folly/container/Enumerate.h>
#include <folly/io/IOBufQueue.h>
#include <folly/portability/GTest.h>

#if FOLLY_QUOTIENT_MULTI_SET_SUPPORTED

namespace {

class QuotientMultiSetTest : public ::testing::Test {
 protected:
  static constexpr uint64_t kBlockSize = folly::QuotientMultiSet<>::kBlockSize;

  void SetUp() override {
    rng.seed(folly::randomNumberSeed());
  }

  void buildAndValidate(
      std::vector<uint64_t>& keys,
      uint64_t keyBits,
      double loadFactor) {
    // Elements must be added in ascending order.
    std::sort(keys.begin(), keys.end());
    folly::QuotientMultiSetBuilder builder(keyBits, keys.size(), loadFactor);
    folly::IOBufQueue buff;
    for (const auto& iter : folly::enumerate(keys)) {
      if (builder.insert(*iter)) {
        // Set payload to relative position of the first key in block.
        builder.setBlockPayload(iter.index);
      }
      if (builder.numReadyBlocks() >= 1) {
        builder.flush(buff);
      }
    }
    builder.close(buff);
    auto spBuf = buff.move();

    folly::StringPiece data(spBuf->coalesce());
    folly::QuotientMultiSet<> reader(data);

    size_t index = 0;
    folly::QuotientMultiSet<>::Iterator iter(&reader);
    while (index < keys.size()) {
      uint64_t start = index;
      uint64_t& key = keys[start];
      auto debugInfo = [&] {
        return folly::sformat("Index: {} Key: {}", index, key);
      };
      while (index < keys.size() && keys[index] == key) {
        index++;
      }
      auto range = reader.equalRange(key);
      size_t pos = reader.getBlockPayload(range.begin / kBlockSize);
      EXPECT_EQ(start, pos + range.begin % kBlockSize) << debugInfo();
      pos = reader.getBlockPayload((range.end - 1) / kBlockSize);
      EXPECT_EQ(index - 1, pos + (range.end - 1) % kBlockSize) << debugInfo();

      iter.skipTo(key);
      EXPECT_EQ(key, iter.key()) << debugInfo();
      EXPECT_EQ(range.begin, iter.pos()) << debugInfo();
      EXPECT_EQ(
          start,
          reader.getBlockPayload(iter.pos() / kBlockSize) +
              iter.pos() % kBlockSize)
          << debugInfo();
      if (start < keys.size() - 1) {
        EXPECT_TRUE(iter.next());
        EXPECT_EQ(keys[start + 1], iter.key());
      }

      // Verify getting keys not in keys returns false.
      uint64_t prevKey = (start == 0 ? 0 : keys[start - 1]);
      if (prevKey + 1 < key) {
        uint64_t missedKey = folly::Random::rand64(prevKey + 1, key, rng);
        EXPECT_FALSE(reader.equalRange(missedKey)) << key;
        iter.skipTo(missedKey);
        EXPECT_EQ(key, iter.key()) << debugInfo();
        EXPECT_EQ(range.begin, iter.pos()) << debugInfo();
        EXPECT_EQ(
            start,
            reader.getBlockPayload(iter.pos() / kBlockSize) +
                iter.pos() % kBlockSize)
            << debugInfo();
        if (start < keys.size() - 1) {
          EXPECT_TRUE(iter.next());
          EXPECT_EQ(keys[start + 1], iter.key());
        }
      }
    }

    const auto maxKey = folly::qms_detail::maxValue(keyBits);
    if (keys.back() < maxKey) {
      uint64_t key = folly::Random::rand64(keys.back(), maxKey, rng) + 1;
      EXPECT_FALSE(reader.equalRange(key)) << keys.back() << " " << key;
      EXPECT_FALSE(iter.skipTo(key));
      EXPECT_TRUE(iter.done());
    }

    folly::QuotientMultiSet<>::Iterator nextIter(&reader);
    for (const auto key : keys) {
      EXPECT_TRUE(nextIter.next());
      EXPECT_EQ(key, nextIter.key());
    }
    EXPECT_FALSE(nextIter.next());
    EXPECT_TRUE(nextIter.done());
  }

  std::mt19937 rng;
};

} // namespace

TEST_F(QuotientMultiSetTest, Simple) {
  std::vector<uint64_t> keys = {
      100, 1000, 1 << 14, 10 << 14, 0, 10, 100 << 14, 1000 << 14};
  buildAndValidate(keys, 32, 0.95);
}

TEST_F(QuotientMultiSetTest, Empty) {
  folly::QuotientMultiSetBuilder builder(32, 1024);
  folly::IOBufQueue buff;
  builder.close(buff);
  auto spBuf = buff.move();
  folly::StringPiece data(spBuf->coalesce());
  folly::QuotientMultiSet<> reader(data);

  for (size_t idx = 0; idx < 1024; idx++) {
    uint64_t key = folly::Random::rand32(rng);
    EXPECT_FALSE(reader.equalRange(key));
  }
}

TEST_F(QuotientMultiSetTest, ZeroKeyBits) {
  std::vector<uint64_t> keys(67, 0);
  buildAndValidate(keys, 0, 0.95);
}

TEST_F(QuotientMultiSetTest, Uniform) {
  constexpr auto kLoadFactor =
      folly::QuotientMultiSetBuilder::kDefaultMaxLoadFactor;
  constexpr uint64_t kAvgSize = 1 << 16;
  auto randSize = [&](uint64_t avgSize) {
    return folly::Random::rand64(avgSize / 2, avgSize * 3 / 2, rng);
  };
  std::vector<std::tuple<int, uint64_t, double>> testCases = {
      {1, randSize(1 << 9), kLoadFactor},
      {8, randSize(1 << 10), kLoadFactor},
      {9, randSize(1 << 11), kLoadFactor},
      {12, randSize(kAvgSize), kLoadFactor},
      {32, randSize(kAvgSize), kLoadFactor},
      {48, randSize(kAvgSize), kLoadFactor},
      {64, randSize(kAvgSize), kLoadFactor},
      {32, randSize(kAvgSize), 1}, // Full
      {12, 3800, kLoadFactor}, // Almost full
      {64, randSize(16), kLoadFactor}, // Sparse, long keys.
  };

  for (const auto& testCase : testCases) {
    const auto& [keyBits, size, loadFactor] = testCase;
    SCOPED_TRACE(folly::sformat(
        "Key bits: {} Size: {} Load factor: {}", keyBits, size, loadFactor));
    std::vector<uint64_t> keys;
    for (uint64_t idx = 0; idx < size; idx++) {
      keys.emplace_back(
          folly::Random::rand64(rng) & folly::qms_detail::maxValue(keyBits));
    }
    buildAndValidate(keys, keyBits, loadFactor);
  }
}

TEST_F(QuotientMultiSetTest, UniformDistributionFullLoadFactor) {
  const uint64_t numElements = 1 << 16;
  std::vector<uint64_t> keys;
  for (uint64_t idx = 0; idx < numElements; idx++) {
    uint64_t key = folly::Random::rand32(idx << 16, (idx + 1) << 16, rng);
    keys.emplace_back(key);
  }
  buildAndValidate(keys, 32, 1.0);
}

TEST_F(QuotientMultiSetTest, Overflow) {
  const uint64_t numElements = 1 << 12;
  std::vector<uint64_t> keys;
  for (uint64_t idx = 0; idx < numElements; idx++) {
    keys.emplace_back(idx);
    keys.emplace_back(idx);
    keys.emplace_back(idx);
  }
  buildAndValidate(keys, 12, 0.95);
}

TEST_F(QuotientMultiSetTest, RandomLengthRuns) {
  const uint64_t numElements = 1 << 16;
  std::vector<uint64_t> keys;
  for (uint64_t idx = 0; idx < (numElements >> 4); idx++) {
    uint64_t key = folly::Random::rand32(rng);
    uint64_t length = folly::Random::rand32(0, 10, rng);
    for (uint64_t k = 0; k < length; k++) {
      keys.emplace_back(key + k);
    }
  }
  buildAndValidate(keys, 32, 0.95);
}

TEST_F(QuotientMultiSetTest, RunAcrossBlocks) {
  const uint64_t numElements = 1 << 10;
  std::vector<uint64_t> keys;
  // Add keys with cluster size 137.
  for (uint64_t idx = 0; idx < (numElements >> 4); idx++) {
    uint64_t key = folly::Random::rand32(rng);
    for (uint64_t k = 0; k < 136; k++) {
      key += k;
      keys.emplace_back(key);
    }
  }
  buildAndValidate(keys, 32, 0.95);
}

TEST_F(QuotientMultiSetTest, PackAtHeadSlots) {
  const uint64_t numElements = 1 << 12;
  std::vector<uint64_t> keys;
  for (uint64_t idx = 0; idx < numElements; idx++) {
    uint64_t key = folly::Random::rand32(idx << 8, (idx + 1) << 8, rng);
    keys.emplace_back(key);
  }
  buildAndValidate(keys, 32, 0.95);
}

TEST_F(QuotientMultiSetTest, PackAtTailSlots) {
  const uint64_t numElements = 1 << 12;
  std::vector<uint64_t> keys;
  uint64_t key = (1 << 30);
  for (uint64_t idx = 0; idx < numElements; idx++) {
    keys.emplace_back(key + idx);
  }
  buildAndValidate(keys, 32, 0.95);
}

TEST_F(QuotientMultiSetTest, KeysOnlyInHeadAndTail) {
  const uint64_t numElements = 1 << 11;
  std::vector<uint64_t> keys;
  for (uint64_t idx = 0; idx < numElements; idx++) {
    keys.emplace_back(idx);
  }
  uint64_t key = (1 << 30);
  for (uint64_t idx = 0; idx < numElements; idx++) {
    keys.emplace_back(key + idx);
  }
  buildAndValidate(keys, 32, 0.95);
}

TEST_F(QuotientMultiSetTest, RunendRightBeforeFirstOccupiedRunend) {
  std::vector<uint64_t> keys;
  // 60 ranges [0, 67] with occupied slot 20.
  for (size_t idx = 0; idx < 68; idx++) {
    keys.push_back(60);
  }
  // 60 ranges [68, 68] with occupied slot 66.
  for (size_t idx = 0; idx < 1; idx++) {
    keys.push_back(200);
  }
  // 60 ranges [69, 88] with occupied slot 83.
  for (size_t idx = 0; idx < 20; idx++) {
    keys.push_back(250);
  }
  buildAndValidate(keys, 8, 0.95);
}

#endif // FOLLY_QUOTIENT_MULTI_SET_SUPPORTED
