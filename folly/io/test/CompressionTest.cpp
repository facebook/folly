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
#include <folly/Memory.h>
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
      { CodecType::LZMA2, false },
      { CodecType::LZMA2_VARINT_SIZE, false },
      { CodecType::ZSTD, false },
      { CodecType::GZIP, false },
      { CodecType::LZ4_FRAME, false },
      { CodecType::BZIP2, false },
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
            CodecType::LZMA2,
            CodecType::ZSTD,
            CodecType::LZ4_FRAME,
            CodecType::BZIP2,
        })));

class AutomaticCodecTest : public testing::TestWithParam<CodecType> {
 protected:
  void SetUp() override {
    codec_ = getCodec(GetParam());
    auto_ = getAutoUncompressionCodec();
  }

  void runSimpleTest(const DataHolder& dh);

  std::unique_ptr<Codec> codec_;
  std::unique_ptr<Codec> auto_;
};

void AutomaticCodecTest::runSimpleTest(const DataHolder& dh) {
  constexpr uint64_t uncompressedLength = 1000;
  auto original = IOBuf::wrapBuffer(dh.data(uncompressedLength));
  auto compressed = codec_->compress(original.get());

  if (!codec_->needsUncompressedLength()) {
    auto uncompressed = auto_->uncompress(compressed.get());
    EXPECT_EQ(uncompressedLength, uncompressed->computeChainDataLength());
    EXPECT_EQ(dh.hash(uncompressedLength), hashIOBuf(uncompressed.get()));
  }
  {
    auto uncompressed = auto_->uncompress(compressed.get(), uncompressedLength);
    EXPECT_EQ(uncompressedLength, uncompressed->computeChainDataLength());
    EXPECT_EQ(dh.hash(uncompressedLength), hashIOBuf(uncompressed.get()));
  }
  ASSERT_GE(compressed->computeChainDataLength(), 8);
  for (size_t i = 0; i < 8; ++i) {
    auto split = compressed->clone();
    auto rest = compressed->clone();
    split->trimEnd(split->length() - i);
    rest->trimStart(i);
    split->appendChain(std::move(rest));
    auto uncompressed = auto_->uncompress(split.get(), uncompressedLength);
    EXPECT_EQ(uncompressedLength, uncompressed->computeChainDataLength());
    EXPECT_EQ(dh.hash(uncompressedLength), hashIOBuf(uncompressed.get()));
  }
}

TEST_P(AutomaticCodecTest, RandomData) {
  runSimpleTest(randomDataHolder);
}

TEST_P(AutomaticCodecTest, ConstantData) {
  runSimpleTest(constantDataHolder);
}

TEST_P(AutomaticCodecTest, ValidPrefixes) {
  const auto prefixes = codec_->validPrefixes();
  for (const auto& prefix : prefixes) {
    EXPECT_FALSE(prefix.empty());
    // Ensure that all strings are at least 8 bytes for LZMA2.
    // The bytes after the prefix should be ignored by `canUncompress()`.
    IOBuf data{IOBuf::COPY_BUFFER, prefix, 0, 8};
    data.append(8);
    EXPECT_TRUE(codec_->canUncompress(&data));
    EXPECT_TRUE(auto_->canUncompress(&data));
  }
}

TEST_P(AutomaticCodecTest, NeedsUncompressedLength) {
  if (codec_->needsUncompressedLength()) {
    EXPECT_TRUE(auto_->needsUncompressedLength());
  }
}

TEST_P(AutomaticCodecTest, maxUncompressedLength) {
  EXPECT_LE(codec_->maxUncompressedLength(), auto_->maxUncompressedLength());
}

TEST_P(AutomaticCodecTest, DefaultCodec) {
  const uint64_t length = 42;
  std::vector<std::unique_ptr<Codec>> codecs;
  codecs.push_back(getCodec(CodecType::ZSTD));
  auto automatic = getAutoUncompressionCodec(std::move(codecs));
  auto original = IOBuf::wrapBuffer(constantDataHolder.data(length));
  auto compressed = codec_->compress(original.get());
  auto decompressed = automatic->uncompress(compressed.get());

  EXPECT_EQ(constantDataHolder.hash(length), hashIOBuf(decompressed.get()));
}

namespace {
class CustomCodec : public Codec {
 public:
  static std::unique_ptr<Codec> create(std::string prefix, CodecType type) {
    return make_unique<CustomCodec>(std::move(prefix), type);
  }
  explicit CustomCodec(std::string prefix, CodecType type)
      : Codec(CodecType::USER_DEFINED),
        prefix_(std::move(prefix)),
        codec_(getCodec(type)) {}

 private:
  std::vector<std::string> validPrefixes() const override {
    return {prefix_};
  }

  bool canUncompress(const IOBuf* data, uint64_t) const override {
    auto clone = data->cloneCoalescedAsValue();
    if (clone.length() < prefix_.size()) {
      return false;
    }
    return memcmp(clone.data(), prefix_.data(), prefix_.size()) == 0;
  }

  std::unique_ptr<IOBuf> doCompress(const IOBuf* data) override {
    auto result = IOBuf::copyBuffer(prefix_);
    result->appendChain(codec_->compress(data));
    EXPECT_TRUE(canUncompress(result.get(), data->computeChainDataLength()));
    return result;
  }

  std::unique_ptr<IOBuf> doUncompress(
      const IOBuf* data,
      uint64_t uncompressedLength) override {
    EXPECT_TRUE(canUncompress(data, uncompressedLength));
    auto clone = data->cloneCoalescedAsValue();
    clone.trimStart(prefix_.size());
    return codec_->uncompress(&clone, uncompressedLength);
  }

  std::string prefix_;
  std::unique_ptr<Codec> codec_;
};
}

TEST_P(AutomaticCodecTest, CustomCodec) {
  const uint64_t length = 42;
  auto ab = CustomCodec::create("ab", CodecType::ZSTD);
  std::vector<std::unique_ptr<Codec>> codecs;
  codecs.push_back(CustomCodec::create("ab", CodecType::ZSTD));
  auto automatic = getAutoUncompressionCodec(std::move(codecs));
  auto original = IOBuf::wrapBuffer(constantDataHolder.data(length));

  auto abCompressed = ab->compress(original.get());
  auto abDecompressed = automatic->uncompress(abCompressed.get());
  EXPECT_TRUE(automatic->canUncompress(abCompressed.get()));
  EXPECT_FALSE(auto_->canUncompress(abCompressed.get()));
  EXPECT_EQ(constantDataHolder.hash(length), hashIOBuf(abDecompressed.get()));

  auto compressed = codec_->compress(original.get());
  auto decompressed = automatic->uncompress(compressed.get());
  EXPECT_EQ(constantDataHolder.hash(length), hashIOBuf(decompressed.get()));
}

TEST_P(AutomaticCodecTest, CustomDefaultCodec) {
  const uint64_t length = 42;
  auto none = CustomCodec::create("none", CodecType::NO_COMPRESSION);
  std::vector<std::unique_ptr<Codec>> codecs;
  codecs.push_back(CustomCodec::create("none", CodecType::NO_COMPRESSION));
  codecs.push_back(getCodec(CodecType::LZ4_FRAME));
  auto automatic = getAutoUncompressionCodec(std::move(codecs));
  auto original = IOBuf::wrapBuffer(constantDataHolder.data(length));

  auto noneCompressed = none->compress(original.get());
  auto noneDecompressed = automatic->uncompress(noneCompressed.get());
  EXPECT_TRUE(automatic->canUncompress(noneCompressed.get()));
  EXPECT_FALSE(auto_->canUncompress(noneCompressed.get()));
  EXPECT_EQ(constantDataHolder.hash(length), hashIOBuf(noneDecompressed.get()));

  auto compressed = codec_->compress(original.get());
  auto decompressed = automatic->uncompress(compressed.get());
  EXPECT_EQ(constantDataHolder.hash(length), hashIOBuf(decompressed.get()));
}

TEST_P(AutomaticCodecTest, canUncompressOneBytes) {
  // No default codec can uncompress 1 bytes.
  IOBuf buf{IOBuf::CREATE, 1};
  buf.append(1);
  EXPECT_FALSE(codec_->canUncompress(&buf, 1));
  EXPECT_FALSE(codec_->canUncompress(&buf, Codec::UNKNOWN_UNCOMPRESSED_LENGTH));
  EXPECT_FALSE(auto_->canUncompress(&buf, 1));
  EXPECT_FALSE(auto_->canUncompress(&buf, Codec::UNKNOWN_UNCOMPRESSED_LENGTH));
}

INSTANTIATE_TEST_CASE_P(
    AutomaticCodecTest,
    AutomaticCodecTest,
    testing::Values(
        CodecType::LZ4_FRAME,
        CodecType::ZSTD,
        CodecType::ZLIB,
        CodecType::GZIP,
        CodecType::LZMA2,
        CodecType::BZIP2));

TEST(ValidPrefixesTest, CustomCodec) {
  std::vector<std::unique_ptr<Codec>> codecs;
  codecs.push_back(CustomCodec::create("none", CodecType::NO_COMPRESSION));
  const auto none = getAutoUncompressionCodec(std::move(codecs));
  const auto prefixes = none->validPrefixes();
  const auto it = std::find(prefixes.begin(), prefixes.end(), "none");
  EXPECT_TRUE(it != prefixes.end());
}

#define EXPECT_THROW_IF_DEBUG(statement, expected_exception) \
  do {                                                       \
    if (kIsDebug) {                                          \
      EXPECT_THROW((statement), expected_exception);         \
    } else {                                                 \
      EXPECT_NO_THROW((statement));                          \
    }                                                        \
  } while (false)

TEST(CheckCompatibleTest, SimplePrefixSecond) {
  std::vector<std::unique_ptr<Codec>> codecs;
  codecs.push_back(CustomCodec::create("abc", CodecType::NO_COMPRESSION));
  codecs.push_back(CustomCodec::create("ab", CodecType::NO_COMPRESSION));
  EXPECT_THROW_IF_DEBUG(
      getAutoUncompressionCodec(std::move(codecs)), std::invalid_argument);
}

TEST(CheckCompatibleTest, SimplePrefixFirst) {
  std::vector<std::unique_ptr<Codec>> codecs;
  codecs.push_back(CustomCodec::create("ab", CodecType::NO_COMPRESSION));
  codecs.push_back(CustomCodec::create("abc", CodecType::NO_COMPRESSION));
  EXPECT_THROW_IF_DEBUG(
      getAutoUncompressionCodec(std::move(codecs)), std::invalid_argument);
}

TEST(CheckCompatibleTest, Empty) {
  std::vector<std::unique_ptr<Codec>> codecs;
  codecs.push_back(CustomCodec::create("", CodecType::NO_COMPRESSION));
  EXPECT_THROW_IF_DEBUG(
      getAutoUncompressionCodec(std::move(codecs)), std::invalid_argument);
}

TEST(CheckCompatibleTest, ZstdPrefix) {
  std::vector<std::unique_ptr<Codec>> codecs;
  codecs.push_back(CustomCodec::create("\x28\xB5\x2F", CodecType::ZSTD));
  EXPECT_THROW_IF_DEBUG(
      getAutoUncompressionCodec(std::move(codecs)), std::invalid_argument);
}

TEST(CheckCompatibleTest, ZstdDuplicate) {
  std::vector<std::unique_ptr<Codec>> codecs;
  codecs.push_back(CustomCodec::create("\x28\xB5\x2F\xFD", CodecType::ZSTD));
  EXPECT_THROW_IF_DEBUG(
      getAutoUncompressionCodec(std::move(codecs)), std::invalid_argument);
}

TEST(CheckCompatibleTest, ZlibIsPrefix) {
  std::vector<std::unique_ptr<Codec>> codecs;
  codecs.push_back(CustomCodec::create("\x18\x76zzasdf", CodecType::ZSTD));
  EXPECT_THROW_IF_DEBUG(
      getAutoUncompressionCodec(std::move(codecs)), std::invalid_argument);
}
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
