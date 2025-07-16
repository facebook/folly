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

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <vector>
#include <folly/portability/GTest.h>

#include <folly/detail/base64_detail/Base64_SSE4_2_Platform.h>

namespace folly::detail::base64_detail {
namespace {
#if FOLLY_SSE_PREREQ(4, 2)

std::array<std::uint8_t, 16> expectedEncodeToIndexes(
    std::array<std::uint8_t, 16> in) {
  std::array<std::uint8_t, 16> res{};

  std::uint8_t const* f = in.data();
  std::uint8_t* o = res.data();
  std::uint8_t* const oEnd = res.data() + res.size();

  while (o != oEnd) {
    std::uint8_t aaab = f[0];
    std::uint8_t bbcc = f[1];
    std::uint8_t cddd = f[2];

    std::uint8_t aaa = aaab >> 2;
    std::uint8_t bbb = ((aaab << 4) | (bbcc >> 4)) & 0x3f;
    std::uint8_t ccc = ((bbcc << 2) | (cddd >> 6)) & 0x3f;
    std::uint8_t ddd = cddd & 0x3f;

    o[0] = aaa;
    o[1] = bbb;
    o[2] = ccc;
    o[3] = ddd;

    f += 3;
    o += 4;
  }

  return res;
}

std::array<std::uint8_t, 16> expectedPackIndexesToBytes(
    std::array<std::uint8_t, 16> in) {
  std::array<std::uint8_t, 16> res{};
  res.fill(0);

  std::uint8_t const* f = in.data();
  std::uint8_t const* const inEnd = in.data() + in.size();
  std::uint8_t* o = res.data();

  while (f != inEnd) {
    std::uint8_t aaa = f[0];
    std::uint8_t bbb = f[1];
    std::uint8_t ccc = f[2];
    std::uint8_t ddd = f[3];

    std::uint8_t aaab = (aaa << 2) | (bbb >> 4);
    std::uint8_t bbcc = (bbb << 4) | (ccc >> 2);
    std::uint8_t cddd = (ccc << 6) | ddd;

    o[0] = aaab;
    o[1] = bbcc;
    o[2] = cddd;

    f += 4;
    o += 3;
  }

  return res;
}

constexpr char kBase64EncodeTable[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
    "="; // \0 is also at the end.

std::array<std::uint8_t, 16> expectedLookupByIndex(
    std::array<std::uint8_t, 16> in, const char* sampleTable) {
  std::array<std::uint8_t, 16> res{};

  for (std::size_t i = 0; i != in.size(); ++i) {
    res[i] = static_cast<std::uint8_t>(sampleTable[in[i]]);
  }

  return res;
}

std::array<std::uint8_t, 16> expectedSuccessfullDecodeToIndex(
    std::array<std::uint8_t, 16> in) {
  std::array<std::uint8_t, 16> r = {};
  for (std::size_t i = 0; i != in.size(); ++i) {
    if ('A' <= in[i] && in[i] <= 'Z') {
      r[i] = in[i] - 'A';
    } else if ('a' <= in[i] && in[i] <= 'z') {
      r[i] = in[i] - 'a' + 26;
    } else if ('0' <= in[i] && in[i] <= '9') {
      r[i] = in[i] - '0' + 26 * 2;
    } else if ('+' == in[i]) {
      r[i] = 62;
    } else if ('/' == in[i]) {
      r[i] = 63;
    } else {
      return {};
    }
  }
  return r;
}

template <typename Platform>
struct Base64PlatformTest : ::testing::Test {
  using platform = Platform;
  using RegBytesArray = std::array<std::uint8_t, platform::kRegisterSize>;

  static RegBytesArray actualEncodeToIndexes(RegBytesArray from) {
    RegBytesArray res;
    auto reg = platform::encodeToIndexes(platform::loadu(from.data()));
    platform::storeu(res.data(), reg);
    return res;
  }

  static RegBytesArray actualLookupByIndex(RegBytesArray from) {
    RegBytesArray res;
    auto reg = platform::lookupByIndex(
        platform::loadu(from.data()), constants::kEncodeTable.data());
    platform::storeu(res.data(), reg);
    return res;
  }

  static RegBytesArray actualSuccesfullDecodeToIndex(RegBytesArray from) {
    RegBytesArray res;
    auto err = platform::initError();
    auto reg = platform::decodeToIndex(platform::loadu(from.data()), err);
    EXPECT_FALSE(platform::hasErrors(err));
    platform::storeu(res.data(), reg);
    return res;
  }

  static RegBytesArray actualPackIndexesToBytes(RegBytesArray from) {
    auto reg = platform::packIndexesToBytes(platform::loadu(from.data()));
    RegBytesArray res;
    platform::storeu(res.data(), reg);
    return res;
  }
};

TYPED_TEST_SUITE(Base64PlatformTest, ::testing::Types<Base64_SSE4_2_Platform>);

TYPED_TEST(Base64PlatformTest, EncodeToIndexes) {
  using RegBytes = typename TestFixture::RegBytesArray;

  for (std::uint16_t v = 0; v != 256; v += 8) {
    RegBytes in;
    std::iota(in.data(), in.data() + in.size(), static_cast<std::uint8_t>(v));

    RegBytes expected = expectedEncodeToIndexes(in);
    RegBytes actual = TestFixture::actualEncodeToIndexes(in);

    EXPECT_EQ(expected, actual);
  }
}

TYPED_TEST(Base64PlatformTest, IndexLookup) {
  using RegBytes = typename TestFixture::RegBytesArray;

  std::uint8_t max_index =
      std::strlen(kBase64EncodeTable) + 1; // to include '\0'

  for (std::uint8_t i = 0; i != max_index + 1 - RegBytes{}.size(); i += 1) {
    RegBytes in;
    std::iota(in.data(), in.data() + in.size(), i);
    RegBytes expected = expectedLookupByIndex(in, kBase64EncodeTable);
    RegBytes actual = TestFixture::actualLookupByIndex(in);
    ASSERT_EQ(expected, actual);
  }
}

TYPED_TEST(Base64PlatformTest, errorDetection) {
  using RegBytes = typename TestFixture::RegBytesArray;

  auto anyErrors = [](const RegBytes& arr) {
    using pl = typename TestFixture::platform;

    auto errorAccum = pl::initError();
    pl::decodeToIndex(pl::loadu(arr.data()), errorAccum);
    return pl::hasErrors(errorAccum);
  };

  constexpr char kValidChar = 'A';

  RegBytes in;
  in.fill(kValidChar);
  ASSERT_FALSE(anyErrors(in));

  for (std::size_t sym = 0; //
       sym != std::numeric_limits<std::uint8_t>::max() + 1;
       ++sym) {
    bool isValid = //
        (sym == '+') || //
        (sym == '/') || //
        ('0' <= sym && sym <= '9') || //
        ('A' <= sym && sym <= 'Z') || //
        ('a' <= sym && sym <= 'z');
    for (auto& inByte : in) {
      inByte = static_cast<std::uint8_t>(sym);

      ASSERT_EQ(anyErrors(in), !isValid) << std::hex << sym << std::dec;
      inByte = kValidChar;
    }
  }
}

TYPED_TEST(Base64PlatformTest, decodeToIndexSuccess) {
  using RegBytes = typename TestFixture::RegBytesArray;

  // Some cases
  for (std::uint16_t v = 0; v < 256; v += 1) {
    RegBytes in;
    std::iota(in.data(), in.data() + in.size(), static_cast<std::uint8_t>(v));

    for (auto& x : in) {
      x = x % 64;
    }

    RegBytes encoded = expectedLookupByIndex(in, kBase64EncodeTable);
    RegBytes expected = expectedSuccessfullDecodeToIndex(encoded);
    RegBytes actual = TestFixture::actualSuccesfullDecodeToIndex(encoded);

    ASSERT_EQ(expected, actual) << v;
  }
}

TYPED_TEST(Base64PlatformTest, packIndexesToBytes) {
  using RegBytes = typename TestFixture::RegBytesArray;

  for (std::uint16_t v = 0; v < 256; v += 1) {
    RegBytes in;
    in.fill(0);
    std::iota(
        in.data(), in.data() + in.size() / 4 * 3, static_cast<std::uint8_t>(v));

    for (auto& x : in) {
      x = x % 64;
    }

    RegBytes expected = expectedPackIndexesToBytes(in);
    ASSERT_EQ(in, expectedEncodeToIndexes(expected)) << "sanity check";

    RegBytes actual = TestFixture::actualPackIndexesToBytes(in);
    ASSERT_EQ(expected, actual);
  }
}
#endif // FOLLY_SSE_PREREQ(4, 2)

} // namespace
} // namespace folly::detail::base64_detail
