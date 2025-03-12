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

#include <gtest/gtest.h>

#include <limits>
#include <string>

#include <folly/compression/Compression.h>

namespace folly::compression::test {
namespace {
constexpr uint64_t kGB = 1024 * 1024 * 1024;
std::vector<CodecType> availableCodecs() {
  std::vector<CodecType> codecs;

  for (size_t i = 0; i < static_cast<size_t>(CodecType::NUM_CODEC_TYPES); ++i) {
    auto type = static_cast<CodecType>(i);
    if (hasCodec(type)) {
      codecs.push_back(type);
    }
  }

  return codecs;
}
} // namespace

class LargeTest : public testing::TestWithParam<CodecType> {};

TEST_P(LargeTest, SingleLargeBuffer) {
  auto codec = getCodec(GetParam(), COMPRESSION_LEVEL_FASTEST);
  uint64_t size = std::min<uint64_t>(5 * kGB, codec->maxUncompressedLength());
  size = std::min<uint64_t>(size, std::numeric_limits<size_t>::max());
  std::string data(size, 'a');
  auto compressed = codec->compress(data);
  auto decompressed = codec->uncompress(
      compressed,
      codec->needsUncompressedLength()
          ? folly::Optional<uint64_t>(size)
          : folly::none);
  EXPECT_EQ(data.size(), decompressed.size());
  EXPECT_TRUE(data == decompressed);
}

INSTANTIATE_TEST_SUITE_P(
    LargeTest,
    LargeTest,
    testing::ValuesIn(availableCodecs()),
    [](const testing::TestParamInfo<CodecType>& info) {
      return std::to_string(int(info.param));
    });
} // namespace folly::compression::test
