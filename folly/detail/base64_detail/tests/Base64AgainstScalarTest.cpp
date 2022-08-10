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

#include <array>
#include <exception>
#include <limits>
#include <optional>
#include <random>
#include <string_view>
#include <folly/detail/base64_detail/Base64Common.h>
#include <folly/detail/base64_detail/Base64SWAR.h>
#include <folly/detail/base64_detail/Base64Scalar.h>
#include <folly/detail/base64_detail/Base64_SSE4_2.h>
#include <folly/portability/GTest.h>

namespace folly::detail::base64_detail {
namespace {

constexpr std::size_t kMaxInputSize = 1000;

struct Base64RandomTest : ::testing::Test {
  std::string generateBytes() {
    std::string res(size_dis(engine), 0);
    for (auto& byte : res) {
      byte = static_cast<char>(byte_dis(engine));
    }
    return res;
  }

  // We don't seed the engine so that tests give consistent results
  std::mt19937 engine;
  std::uniform_int_distribution<unsigned int> size_dis{0, kMaxInputSize};
  std::uniform_int_distribution<unsigned int> byte_dis{
      0, std::numeric_limits<std::uint8_t>::max()};
};

using Encode = char* (*)(const char*, const char*, char*);
using Decode = Base64DecodeResult (*)(const char*, const char*, char*);

std::string callEncode(std::string_view bytes, Encode encode) {
  std::string buf(kMaxInputSize * 2, 0);
  char* end = encode(bytes.data(), bytes.data() + bytes.size(), buf.data());
  buf.resize(end - buf.data());
  return buf;
}

auto callDecode(std::string_view encoded, Decode decode)
    -> std::optional<std::string> {
  std::string buf(kMaxInputSize * 2, 0);
  Base64DecodeResult r =
      decode(encoded.data(), encoded.data() + encoded.size(), buf.data());
  if (!r.isSuccess) {
    return {};
  }
  buf.resize(r.o - buf.data());
  return buf;
}

constexpr Encode kEncodes[] = {
    base64EncodeScalar,
#if FOLLY_SSE_PREREQ(4, 2)
    base64Encode_SSE4_2,
#endif
};

constexpr Encode kEncodesURL[] = {
    base64URLEncodeScalar,
#if FOLLY_SSE_PREREQ(4, 2)
    base64URLEncode_SSE4_2,
#endif
};

constexpr Decode kDecodes[] = {
    base64DecodeScalar,
    base64DecodeSWAR,
#if FOLLY_SSE_PREREQ(4, 2)
    base64Decode_SSE4_2,
#endif
};

constexpr Decode kDecodesURL[] = {
    base64URLDecodeScalar,
    base64URLDecodeSWAR,
};

TEST_F(Base64RandomTest, RandomTest) {
  for (int i = 0; i != 10'000; ++i) {
    auto bytes = generateBytes();
    auto base64 = callEncode(bytes, base64EncodeScalar);
    auto base64URL = callEncode(bytes, base64URLEncodeScalar);

    for (Encode encode : kEncodes) {
      ASSERT_EQ(base64, callEncode(bytes, encode));
    }

    for (Encode encode : kEncodesURL) {
      ASSERT_EQ(base64URL, callEncode(bytes, encode));
    }

    for (Decode decode : kDecodes) {
      ASSERT_EQ(bytes, callDecode(base64, decode));
    }

    for (Decode decode : kDecodesURL) {
      ASSERT_EQ(bytes, callDecode(base64, decode));
      ASSERT_EQ(bytes, callDecode(base64URL, decode));
    }
  }
}

TEST_F(Base64RandomTest, RandomDecodeTest) {
  for (int i = 0; i != 10'000; ++i) {
    auto bytes = generateBytes();

    auto scalar = callDecode(bytes, base64DecodeScalar);
    auto scalarURL = callDecode(bytes, base64URLDecodeScalar);

    for (Decode decode : kDecodes) {
      ASSERT_EQ(scalar, callDecode(bytes, decode));
    }
    for (Decode decode : kDecodesURL) {
      ASSERT_EQ(scalarURL, callDecode(bytes, decode));
    }
  }
}

} // namespace
} // namespace folly::detail::base64_detail
