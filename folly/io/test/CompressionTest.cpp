/*
 * Copyright 2013 Facebook, Inc.
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

#include "folly/io/Compression.h"

// Yes, tr1, as that's what gtest requires
#include <random>
#include <thread>
#include <tr1/tuple>
#include <unordered_map>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "folly/Benchmark.h"
#include "folly/Hash.h"
#include "folly/Random.h"
#include "folly/io/IOBufQueue.h"

namespace folly { namespace io { namespace test {

constexpr size_t randomDataSizeLog2 = 27;  // 128MiB
constexpr size_t randomDataSize = size_t(1) << randomDataSizeLog2;

std::unique_ptr<uint8_t[]> randomData;
std::unordered_map<uint64_t, uint64_t> hashes;

uint64_t hashIOBuf(const IOBuf* buf) {
  uint64_t h = folly::hash::FNV_64_HASH_START;
  for (auto& range : *buf) {
    h = folly::hash::fnv64_buf(range.data(), range.size(), h);
  }
  return h;
}

uint64_t getRandomDataHash(uint64_t size) {
  auto p = hashes.find(size);
  if (p != hashes.end()) {
    return p->second;
  }

  uint64_t h = folly::hash::fnv64_buf(randomData.get(), size);
  hashes[size] = h;
  return h;
}

void generateRandomData() {
  randomData.reset(new uint8_t[size_t(1) << randomDataSizeLog2]);

  constexpr size_t numThreadsLog2 = 3;
  constexpr size_t numThreads = size_t(1) << numThreadsLog2;

  uint32_t seed = randomNumberSeed();

  std::vector<std::thread> threads;
  threads.reserve(numThreads);
  for (size_t t = 0; t < numThreads; ++t) {
    threads.emplace_back(
        [seed, t, numThreadsLog2] () {
          std::mt19937 rng(seed + t);
          size_t countLog2 = size_t(1) << (randomDataSizeLog2 - numThreadsLog2);
          size_t start = size_t(t) << countLog2;
          for (size_t i = 0; i < countLog2; ++i) {
            randomData[start + i] = rng();
          }
        });
  }

  for (auto& t : threads) {
    t.join();
  }
}

TEST(CompressionTestNeedsUncompressedLength, Simple) {
  EXPECT_FALSE(getCodec(CodecType::NO_COMPRESSION)->needsUncompressedLength());
  EXPECT_TRUE(getCodec(CodecType::LZ4)->needsUncompressedLength());
  EXPECT_FALSE(getCodec(CodecType::SNAPPY)->needsUncompressedLength());
  EXPECT_FALSE(getCodec(CodecType::ZLIB)->needsUncompressedLength());
  EXPECT_FALSE(getCodec(CodecType::LZ4_VARINT_SIZE)->needsUncompressedLength());
}

class CompressionTest : public testing::TestWithParam<
    std::tr1::tuple<int, CodecType>> {
  protected:
   void SetUp() {
     auto tup = GetParam();
     uncompressedLength_ = uint64_t(1) << std::tr1::get<0>(tup);
     codec_ = getCodec(std::tr1::get<1>(tup));
   }

   uint64_t uncompressedLength_;
   std::unique_ptr<Codec> codec_;
};

TEST_P(CompressionTest, Simple) {
  auto original = IOBuf::wrapBuffer(randomData.get(), uncompressedLength_);
  auto compressed = codec_->compress(original.get());
  if (!codec_->needsUncompressedLength()) {
    auto uncompressed = codec_->uncompress(compressed.get());
    EXPECT_EQ(uncompressedLength_, uncompressed->computeChainDataLength());
    EXPECT_EQ(getRandomDataHash(uncompressedLength_),
              hashIOBuf(uncompressed.get()));
  }
  {
    auto uncompressed = codec_->uncompress(compressed.get(),
                                           uncompressedLength_);
    EXPECT_EQ(uncompressedLength_, uncompressed->computeChainDataLength());
    EXPECT_EQ(getRandomDataHash(uncompressedLength_),
              hashIOBuf(uncompressed.get()));
  }
}

INSTANTIATE_TEST_CASE_P(
    CompressionTest,
    CompressionTest,
    testing::Combine(
        testing::Values(0, 1, 12, 22, int(randomDataSizeLog2)),
        testing::Values(CodecType::NO_COMPRESSION,
                        CodecType::LZ4,
                        CodecType::SNAPPY,
                        CodecType::ZLIB,
                        CodecType::LZ4_VARINT_SIZE)));

class CompressionCorruptionTest : public testing::TestWithParam<CodecType> {
 protected:
  void SetUp() {
    codec_ = getCodec(GetParam());
  }

  std::unique_ptr<Codec> codec_;
};

TEST_P(CompressionCorruptionTest, Simple) {
  constexpr uint64_t uncompressedLength = 42;
  auto original = IOBuf::wrapBuffer(randomData.get(), uncompressedLength);
  auto compressed = codec_->compress(original.get());

  if (!codec_->needsUncompressedLength()) {
    auto uncompressed = codec_->uncompress(compressed.get());
    EXPECT_EQ(uncompressedLength, uncompressed->computeChainDataLength());
    EXPECT_EQ(getRandomDataHash(uncompressedLength),
              hashIOBuf(uncompressed.get()));
  }
  {
    auto uncompressed = codec_->uncompress(compressed.get(),
                                           uncompressedLength);
    EXPECT_EQ(uncompressedLength, uncompressed->computeChainDataLength());
    EXPECT_EQ(getRandomDataHash(uncompressedLength),
              hashIOBuf(uncompressed.get()));
  }

  EXPECT_THROW(codec_->uncompress(compressed.get(), uncompressedLength + 1),
               std::runtime_error);

  // Corrupt the first character
  ++(compressed->writableData()[0]);

  if (!codec_->needsUncompressedLength()) {
    EXPECT_THROW(codec_->uncompress(compressed.get()),
                 std::runtime_error);
  }

  EXPECT_THROW(codec_->uncompress(compressed.get(), uncompressedLength),
               std::runtime_error);
}

INSTANTIATE_TEST_CASE_P(
    CompressionCorruptionTest,
    CompressionCorruptionTest,
    testing::Values(
        // NO_COMPRESSION can't detect corruption
        // LZ4 can't detect corruption reliably (sigh)
        CodecType::SNAPPY,
        CodecType::ZLIB));

}}}  // namespaces

int main(int argc, char *argv[]) {
  testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);

  folly::io::test::generateRandomData();  // 4GB

  auto ret = RUN_ALL_TESTS();
  if (!ret) {
    folly::runBenchmarksOnFlag();
  }
  return ret;
}

