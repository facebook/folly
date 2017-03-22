/*
 * Copyright 2017 Facebook, Inc.
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

#include <folly/io/Compression.h>

#include <random>
#include <set>
#include <thread>
#include <unordered_map>

#include <boost/noncopyable.hpp>
#include <glog/logging.h>

#include <folly/Benchmark.h>
#include <folly/Hash.h>
#include <folly/Random.h>
#include <folly/Varint.h>
#include <folly/io/IOBufQueue.h>
#include <folly/portability/GTest.h>

namespace folly { namespace io { namespace test {

class DataHolder : private boost::noncopyable {
 public:
  uint64_t hash(size_t size) const;
  ByteRange data(size_t size) const;

 protected:
  explicit DataHolder(size_t sizeLog2);
  const size_t size_;
  std::unique_ptr<uint8_t[]> data_;
  mutable std::unordered_map<uint64_t, uint64_t> hashCache_;
};

DataHolder::DataHolder(size_t sizeLog2)
  : size_(size_t(1) << sizeLog2),
    data_(new uint8_t[size_]) {
}

uint64_t DataHolder::hash(size_t size) const {
  CHECK_LE(size, size_);
  auto p = hashCache_.find(size);
  if (p != hashCache_.end()) {
    return p->second;
  }

  uint64_t h = folly::hash::fnv64_buf(data_.get(), size);
  hashCache_[size] = h;
  return h;
}

ByteRange DataHolder::data(size_t size) const {
  CHECK_LE(size, size_);
  return ByteRange(data_.get(), size);
}

uint64_t hashIOBuf(const IOBuf* buf) {
  uint64_t h = folly::hash::FNV_64_HASH_START;
  for (auto& range : *buf) {
    h = folly::hash::fnv64_buf(range.data(), range.size(), h);
  }
  return h;
}

class RandomDataHolder : public DataHolder {
 public:
  explicit RandomDataHolder(size_t sizeLog2);
};

RandomDataHolder::RandomDataHolder(size_t sizeLog2)
  : DataHolder(sizeLog2) {
  constexpr size_t numThreadsLog2 = 3;
  constexpr size_t numThreads = size_t(1) << numThreadsLog2;

  uint32_t seed = randomNumberSeed();

  std::vector<std::thread> threads;
  threads.reserve(numThreads);
  for (size_t t = 0; t < numThreads; ++t) {
    threads.emplace_back(
        [this, seed, t, numThreadsLog2, sizeLog2] () {
          std::mt19937 rng(seed + t);
          size_t countLog2 = sizeLog2 - numThreadsLog2;
          size_t start = size_t(t) << countLog2;
          for (size_t i = 0; i < countLog2; ++i) {
            this->data_[start + i] = rng();
          }
        });
  }

  for (auto& t : threads) {
    t.join();
  }
}

class ConstantDataHolder : public DataHolder {
 public:
  explicit ConstantDataHolder(size_t sizeLog2);
};

ConstantDataHolder::ConstantDataHolder(size_t sizeLog2)
  : DataHolder(sizeLog2) {
  memset(data_.get(), 'a', size_);
}

constexpr size_t dataSizeLog2 = 27;  // 128MiB
RandomDataHolder randomDataHolder(dataSizeLog2);
ConstantDataHolder constantDataHolder(dataSizeLog2);

// The intersection of the provided codecs & those that are compiled in.
static std::vector<CodecType> supportedCodecs(std::vector<CodecType> const& v) {
  std::vector<CodecType> supported;

  std::copy_if(
      std::begin(v),
      std::end(v),
      std::back_inserter(supported),
      hasCodec);

  return supported;
}

// All compiled-in compression codecs.
static std::vector<CodecType> availableCodecs() {
  std::vector<CodecType> codecs;

  for (size_t i = 0; i < static_cast<size_t>(CodecType::NUM_CODEC_TYPES); ++i) {
    auto type = static_cast<CodecType>(i);
    if (hasCodec(type)) {
      codecs.push_back(type);
    }
  }

  return codecs;
}

TEST(CompressionTestNeedsUncompressedLength, Simple) {
  static const struct { CodecType type; bool needsUncompressedLength; }
    expectations[] = {
      { CodecType::NO_COMPRESSION, false },
      { CodecType::LZ4, true },
      { CodecType::SNAPPY, false },
      { CodecType::ZLIB, false },
      { CodecType::LZ4_VARINT_SIZE, false },
      { CodecType::LZMA2, true },
      { CodecType::LZMA2_VARINT_SIZE, false },
      { CodecType::ZSTD, false },
      { CodecType::GZIP, false },
    };

  for (auto const& test : expectations) {
    if (hasCodec(test.type)) {
      EXPECT_EQ(getCodec(test.type)->needsUncompressedLength(),
                test.needsUncompressedLength);
    }
  }
}

class CompressionTest
    : public testing::TestWithParam<std::tr1::tuple<int, int, CodecType>> {
 protected:
  void SetUp() override {
    auto tup = GetParam();
    uncompressedLength_ = uint64_t(1) << std::tr1::get<0>(tup);
    chunks_ = std::tr1::get<1>(tup);
    codec_ = getCodec(std::tr1::get<2>(tup));
  }

  void runSimpleIOBufTest(const DataHolder& dh);

  void runSimpleStringTest(const DataHolder& dh);

 private:
  std::unique_ptr<IOBuf> split(std::unique_ptr<IOBuf> data) const;

  uint64_t uncompressedLength_;
  size_t chunks_;
  std::unique_ptr<Codec> codec_;
};

void CompressionTest::runSimpleIOBufTest(const DataHolder& dh) {
  const auto original = split(IOBuf::wrapBuffer(dh.data(uncompressedLength_)));
  const auto compressed = split(codec_->compress(original.get()));
  if (!codec_->needsUncompressedLength()) {
    auto uncompressed = codec_->uncompress(compressed.get());
    EXPECT_EQ(uncompressedLength_, uncompressed->computeChainDataLength());
    EXPECT_EQ(dh.hash(uncompressedLength_), hashIOBuf(uncompressed.get()));
  }
  {
    auto uncompressed = codec_->uncompress(compressed.get(),
                                           uncompressedLength_);
    EXPECT_EQ(uncompressedLength_, uncompressed->computeChainDataLength());
    EXPECT_EQ(dh.hash(uncompressedLength_), hashIOBuf(uncompressed.get()));
  }
}

void CompressionTest::runSimpleStringTest(const DataHolder& dh) {
  const auto original = std::string(
      reinterpret_cast<const char*>(dh.data(uncompressedLength_).data()),
      uncompressedLength_);
  const auto compressed = codec_->compress(original);
  if (!codec_->needsUncompressedLength()) {
    auto uncompressed = codec_->uncompress(compressed);
    EXPECT_EQ(uncompressedLength_, uncompressed.length());
    EXPECT_EQ(uncompressed, original);
  }
  {
    auto uncompressed = codec_->uncompress(compressed, uncompressedLength_);
    EXPECT_EQ(uncompressedLength_, uncompressed.length());
    EXPECT_EQ(uncompressed, original);
  }
}

// Uniformly split data into (potentially empty) chunks.
std::unique_ptr<IOBuf> CompressionTest::split(
    std::unique_ptr<IOBuf> data) const {
  if (data->isChained()) {
    data->coalesce();
  }

  const size_t size = data->computeChainDataLength();

  std::multiset<size_t> splits;
  for (size_t i = 1; i < chunks_; ++i) {
    splits.insert(Random::rand64(size));
  }

  folly::IOBufQueue result;

  size_t offset = 0;
  for (size_t split : splits) {
    result.append(IOBuf::copyBuffer(data->data() + offset, split - offset));
    offset = split;
  }
  result.append(IOBuf::copyBuffer(data->data() + offset, size - offset));

  return result.move();
}

TEST_P(CompressionTest, RandomData) {
  runSimpleIOBufTest(randomDataHolder);
}

TEST_P(CompressionTest, ConstantData) {
  runSimpleIOBufTest(constantDataHolder);
}

TEST_P(CompressionTest, RandomDataString) {
  runSimpleStringTest(randomDataHolder);
}

TEST_P(CompressionTest, ConstantDataString) {
  runSimpleStringTest(constantDataHolder);
}

INSTANTIATE_TEST_CASE_P(
    CompressionTest,
    CompressionTest,
    testing::Combine(
        testing::Values(0, 1, 12, 22, 25, 27),
        testing::Values(1, 2, 3, 8, 65),
        testing::ValuesIn(availableCodecs())));

class CompressionVarintTest
    : public testing::TestWithParam<std::tr1::tuple<int, CodecType>> {
 protected:
  void SetUp() override {
    auto tup = GetParam();
    uncompressedLength_ = uint64_t(1) << std::tr1::get<0>(tup);
    codec_ = getCodec(std::tr1::get<1>(tup));
  }

  void runSimpleTest(const DataHolder& dh);

  uint64_t uncompressedLength_;
  std::unique_ptr<Codec> codec_;
};

inline uint64_t oneBasedMsbPos(uint64_t number) {
  uint64_t pos = 0;
  for (; number > 0; ++pos, number >>= 1) {
  }
  return pos;
}

void CompressionVarintTest::runSimpleTest(const DataHolder& dh) {
  auto original = IOBuf::wrapBuffer(dh.data(uncompressedLength_));
  auto compressed = codec_->compress(original.get());
  auto breakPoint =
      1UL +
      Random::rand64(
          std::max(uint64_t(9), oneBasedMsbPos(uncompressedLength_)) / 9UL);
  auto tinyBuf = IOBuf::copyBuffer(compressed->data(),
                                   std::min(compressed->length(), breakPoint));
  compressed->trimStart(breakPoint);
  tinyBuf->prependChain(std::move(compressed));
  compressed = std::move(tinyBuf);

  auto uncompressed = codec_->uncompress(compressed.get());

  EXPECT_EQ(uncompressedLength_, uncompressed->computeChainDataLength());
  EXPECT_EQ(dh.hash(uncompressedLength_), hashIOBuf(uncompressed.get()));
}

TEST_P(CompressionVarintTest, RandomData) {
  runSimpleTest(randomDataHolder);
}

TEST_P(CompressionVarintTest, ConstantData) {
  runSimpleTest(constantDataHolder);
}

INSTANTIATE_TEST_CASE_P(
    CompressionVarintTest,
    CompressionVarintTest,
    testing::Combine(
        testing::Values(0, 1, 12, 22, 25, 27),
        testing::ValuesIn(supportedCodecs({
            CodecType::LZ4_VARINT_SIZE,
            CodecType::LZMA2_VARINT_SIZE,
            }))));

class CompressionCorruptionTest : public testing::TestWithParam<CodecType> {
 protected:
  void SetUp() override { codec_ = getCodec(GetParam()); }

  void runSimpleTest(const DataHolder& dh);

  std::unique_ptr<Codec> codec_;
};

void CompressionCorruptionTest::runSimpleTest(const DataHolder& dh) {
  constexpr uint64_t uncompressedLength = 42;
  auto original = IOBuf::wrapBuffer(dh.data(uncompressedLength));
  auto compressed = codec_->compress(original.get());

  if (!codec_->needsUncompressedLength()) {
    auto uncompressed = codec_->uncompress(compressed.get());
    EXPECT_EQ(uncompressedLength, uncompressed->computeChainDataLength());
    EXPECT_EQ(dh.hash(uncompressedLength), hashIOBuf(uncompressed.get()));
  }
  {
    auto uncompressed = codec_->uncompress(compressed.get(),
                                           uncompressedLength);
    EXPECT_EQ(uncompressedLength, uncompressed->computeChainDataLength());
    EXPECT_EQ(dh.hash(uncompressedLength), hashIOBuf(uncompressed.get()));
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

TEST_P(CompressionCorruptionTest, RandomData) {
  runSimpleTest(randomDataHolder);
}

TEST_P(CompressionCorruptionTest, ConstantData) {
  runSimpleTest(constantDataHolder);
}

INSTANTIATE_TEST_CASE_P(
    CompressionCorruptionTest,
    CompressionCorruptionTest,
    testing::ValuesIn(
        // NO_COMPRESSION can't detect corruption
        // LZ4 can't detect corruption reliably (sigh)
        supportedCodecs({
            CodecType::SNAPPY,
            CodecType::ZLIB,
            })));

}}}  // namespaces

int main(int argc, char *argv[]) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  auto ret = RUN_ALL_TESTS();
  if (!ret) {
    folly::runBenchmarksOnFlag();
  }
  return ret;
}
