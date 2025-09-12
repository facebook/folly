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

#include <stdexcept>
#include <string_view>
#include <folly/base64.h>
#include <folly/portability/GTest.h>

// NOTE: Most of the testing is done through individual components
// we just check that everything works together
//
// NOTE: there is also fuzzing.

namespace {

template <std::size_t N>
constexpr auto base64EncodeArray(const std::array<char, N>& in) {
  std::array<char, folly::base64EncodedSize(N) + 1> res{};
  folly::base64Encode(in.data(), in.data() + in.size(), res.data());
  res.back() = 0;
  return res;
}

template <std::size_t N>
constexpr auto base64URLEncodeArray(const std::array<char, N>& in) {
  std::array<char, folly::base64URLEncodedSize(N) + 1> res{};
  folly::base64URLEncode(in.data(), in.data() + in.size(), res.data());
  res.back() = 0;
  return res;
}

template <std::size_t ResSize>
constexpr auto base64DecodeToArray(std::string_view s) {
  std::array<char, ResSize> res = {};
  if (!folly::base64Decode(s, res.data()).is_success) {
    throw std::runtime_error("Couldn't decode");
  }
  return res;
}

template <std::size_t ResSize>
constexpr auto base64URLDecodeToArray(std::string_view s) {
  std::array<char, ResSize> res = {};
  if (!folly::base64URLDecode(s, res.data()).is_success) {
    throw std::runtime_error("Couldn't decode");
  }
  return res;
}

template <std::size_t ResSize>
struct PHPRes {
  std::array<char, ResSize> res;
  long result_size;
};

template <std::size_t ResSize>
constexpr PHPRes<ResSize> base64PHPStrictDecodeToArray(std::string_view s) {
  std::array<char, ResSize> res = {};
  auto decoded = folly::base64PHPStrictDecode(s, res.data());
  if (!decoded.is_success) {
    throw std::runtime_error("Couldn't decode");
  }
  return {res, decoded.o - res.data()};
}

// deal with insufficient library constexpr
template <typename Rng>
constexpr bool rng_equal(const Rng& x, const Rng& y) {
  auto f = x.begin();
  auto l = x.end();
  auto f1 = y.begin();
  if (x.size() != y.size()) {
    return false;
  }

  while (f != l) {
    if (*f++ != *f1++) {
      return false;
    }
  }
  return true;
}

struct ConstexprTest {
  static constexpr std::array<char, 2> toEncode{{'a', 'b'}};
  static constexpr std::array<char, 5> encoded = base64EncodeArray(toEncode);

  static constexpr std::string_view expected = "YWI=";
  static constexpr std::size_t decodedSize = folly::base64DecodedSize(expected);
  static constexpr auto decoded = base64DecodeToArray<decodedSize>(expected);

  // C++17 constexpr bug
  static constexpr std::string_view encoded_sv =
      std::string_view(encoded.data(), encoded.size() - 1);

  static_assert(decodedSize == 2);
  static_assert(rng_equal(expected, encoded_sv));
  static_assert(rng_equal(toEncode, decoded));
};

struct ConstexprURLTest {
  static constexpr std::array<char, 2> toEncode{{'a', 'b'}};
  static constexpr std::array<char, 4> encoded = base64URLEncodeArray(toEncode);

  static constexpr std::string_view expected = "YWI";
  static constexpr std::size_t decodedSize =
      folly::base64URLDecodedSize(expected);
  static constexpr auto decoded = base64URLDecodeToArray<decodedSize>(expected);

  // C++17 constexpr bug
  static constexpr std::string_view encoded_sv =
      std::string_view(encoded.data(), encoded.size() - 1);

  static_assert(decodedSize == 2);
  static_assert(rng_equal(expected, encoded_sv));
  static_assert(rng_equal(toEncode, decoded));
};

struct ConstexprPHPTest {
  static constexpr std::array<char, 2> toEncode{{'a', 'b'}};
  static constexpr std::array<char, 5> encoded = base64EncodeArray(toEncode);

  static constexpr std::string_view expected = "YWI=";
  static constexpr std::size_t requiredSize =
      folly::base64PHPStrictDecodeRequiredOutputSize(expected);
  static constexpr std::array<char, requiredSize> output{{'a', 'b'}};
  static constexpr auto decoded =
      base64PHPStrictDecodeToArray<requiredSize>(expected);

  // C++17 constexpr bug
  static constexpr std::string_view encoded_sv =
      std::string_view(encoded.data(), encoded.size() - 1);

  static_assert(decoded.result_size == 2);
  static_assert(rng_equal(expected, encoded_sv));
  static_assert(output[0] == decoded.res[0]);
  static_assert(output[1] == decoded.res[1]);
};

TEST(Base64Test, NormalTest) {
  std::vector<std::uint8_t> bytes{'a', 'b'};

  ASSERT_EQ("YWI=", folly::base64Encode("ab"));
  ASSERT_EQ("ab", folly::base64Decode("YWI="));

  ASSERT_EQ("YWI", folly::base64URLEncode("ab"));
  ASSERT_EQ("ab", folly::base64URLDecode("YWI="));
  ASSERT_EQ("ab", folly::base64URLDecode("YWI"));

  ASSERT_EQ("ab", folly::base64PHPStrictDecode("YWI="));

  // From fuzzing
  ASSERT_EQ("", folly::base64Decode(""));
  ASSERT_THROW(folly::base64Decode("="), folly::base64_decode_error);
  ASSERT_THROW(folly::base64Decode("=="), folly::base64_decode_error);

  ASSERT_EQ("", folly::base64URLDecode(""));
  ASSERT_THROW(folly::base64URLDecode("="), folly::base64_decode_error);
  ASSERT_THROW(folly::base64URLDecode("=="), folly::base64_decode_error);

  ASSERT_EQ("", folly::base64PHPStrictDecode(""));
  ASSERT_THROW(folly::base64PHPStrictDecode("="), folly::base64_decode_error);
  ASSERT_THROW(folly::base64PHPStrictDecode("=="), folly::base64_decode_error);

  {
    std::string_view s =
        "bbbbbbbbb"
        "ddddddddddddd"
        "dddddddaaaaaa"
        "adddddddb=";

    ASSERT_THROW(folly::base64Decode(s), folly::base64_decode_error);
    ASSERT_THROW(folly::base64URLDecode(s), folly::base64_decode_error);
    ASSERT_THROW(folly::base64PHPStrictDecode(s), folly::base64_decode_error);
  }
}

constexpr std::string_view kBase64PHPStrictDecodeEmptyStrings[] = {
    "",
    "        ",
    "\t\r\n \t\r\n ",
    "FB\n\n\n\n",
    "RmFjZWJvb2s=FB=\n",
    "RmFjZWJvb2s==FB\n",
};

constexpr std::string_view kBase64PHPStrictDecodeTestCases[] = {
    "RmFjZWJvb2s=",
    "RmFjZW\n\nJvb2s\n\n  =  \n\n",
    "Rm\r\nFj\t\tZWJv\t\r  b2s =\t\r\n\r\t  \n\n",
};

TEST(Base64Test, PHPDecode) {
  std::string_view decoded = "Facebook";

  for (std::string_view empty : kBase64PHPStrictDecodeEmptyStrings) {
    std::size_t requiredSize =
        folly::base64PHPStrictDecodeRequiredOutputSize(empty);
    std::string buf(requiredSize, '\0');
    auto result = folly::base64PHPStrictDecode(empty, buf.data());

    ASSERT_EQ(0, result.o - buf.data()) << "Decoded empty string: " << empty;
  }

  for (std::string_view test : kBase64PHPStrictDecodeTestCases) {
    std::size_t requiredSize =
        folly::base64PHPStrictDecodeRequiredOutputSize(test);
    std::string buf(requiredSize, '\0');
    auto result = folly::base64PHPStrictDecode(test, buf.data());
    buf.resize(result.o - buf.data());

    ASSERT_TRUE(result.is_success) << "Failed to decode: " << test;
    ASSERT_EQ(decoded.size(), result.o - buf.data());
    ASSERT_EQ(decoded, std::string_view(buf.data(), result.o - buf.data()))
        << "Decoded content doesn't match expected for: " << test;
  }

  for (std::string_view test : kBase64PHPStrictDecodeTestCases) {
    auto result = folly::base64PHPStrictDecode(test);
    ASSERT_EQ(decoded, result)
        << "Decoded content doesn't match expected for: " << test;
  }
}

} // namespace
