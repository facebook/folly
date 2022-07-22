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

#include <cstdint>
#include <initializer_list>
#include <sstream>
#include <string_view>
#include <type_traits>
#include <folly/detail/base64_detail/Base64Scalar.h>
#include <folly/detail/base64_detail/Base64Simd.h>
#include <folly/detail/base64_detail/Base64_SSE4_2.h>
#include <folly/portability/GTest.h>

namespace folly::detail::base64_detail {
namespace {

struct TestCase {
  std::string_view data;
  std::string_view encodedStd;
  std::string_view encodedURL;
};

// The types are weird because we need to do constexpr tests
struct TestCaseOnStack : TestCase {
  constexpr TestCaseOnStack(
      std::initializer_list<char> dataInit, // initializer list makes it easier
                                            // to put non printable characters
                                            // compared to strings
      std::string_view encodedStd,
      std::string_view encodedURL)
      : TestCase{{}, encodedStd, encodedURL}, dataBuf{} {
    // std::copy not constexpr
    auto f = dataInit.begin(), l = dataInit.end();
    auto o = dataBuf.begin();

    while (f != l) {
      *o++ = *f++;
    }
    data = {dataBuf.begin(), o};
  }

  std::array<char, 200> dataBuf;
} constexpr staticTestCases[] = {
    // clang-format off
    {
      std::initializer_list<char>{}, "", ""
    },
    {
        { 0, 0, 0 }, "AAAA", "AAAA"
    },
    {
        {1}, "AQ==", "AQ"
    },
    {
        {1, 0}, "AQA=", "AQA",
    },
    {
        {1, 0, 0}, "AQAA", "AQAA"
    },
    {
        {'a','b','c','d'},
        "YWJjZA==",
        "YWJjZA"
    },
    {
        {'a', 'b', 'c'},
        "YWJj",
        "YWJj"
    },
    {
      {'l','e','s','s',' ',
       'i','s',' ',
       'm','o','r','e',' ',
       't','h','a','n',' ',
       'm','o','r','e'},
      "bGVzcyBpcyBtb3JlIHRoYW4gbW9yZQ==",
      "bGVzcyBpcyBtb3JlIHRoYW4gbW9yZQ",
    },
    {
      {'<','>', '?','s','u'},
      "PD4/c3U=",
      "PD4_c3U"
    },
    // clang-format on
};

std::string byteRangeToString(std::string_view data) {
  std::stringstream res;
  res << '{';
  auto f = data.begin();
  auto l = data.end();

  if (f != l) {
    res << static_cast<std::int32_t>(*f);
    while (++f != l) {
      res << ", " << static_cast<std::int32_t>(*f);
    }
  }
  res << '}';
  return std::move(res).str();
}

template <typename TestRunner>
constexpr bool staticTests(TestRunner testRunner) {
  for (const auto& test : staticTestCases) {
    if (!testRunner(TestCase{test})) {
      return false;
    }
  }
  return true;
}

template <typename I, typename N, typename V>
constexpr I fill_n(I f, N n, V v) {
  while (n--) {
    *f++ = v;
  }
  return f;
}

template <typename TestRunner>
constexpr bool manyZeroesTests(TestRunner testRunner) {
  for (std::size_t inSize = 0; inSize != 129; ++inSize) {
    TestCase test;

    // Populate input
    std::array<char, 256> buf = {}; // fill in 0s
    buf[inSize + 1] = 15; // messing with the input
    test.data = {buf.begin(), buf.begin() + inSize};

    // Populate expected
    std::array<char, 256> expectedBuf = {};
    char* expectedL = fill_n(expectedBuf.begin(), inSize / 3 * 4, 'A');
    char* expectedURLL = expectedL;

    if (inSize % 3 == 2) {
      *expectedL++ = 'A';
      *expectedL++ = 'A';
      *expectedL++ = 'A';
      expectedURLL = expectedL;
      *expectedL++ = '=';
    } else if (inSize % 3 == 1) {
      *expectedL++ = 'A';
      *expectedL++ = 'A';
      expectedURLL = expectedL;
      *expectedL++ = '=';
      *expectedL++ = '=';
    }

    test.encodedStd =
        std::string_view(expectedBuf.data(), expectedL - expectedBuf.data());
    test.encodedURL =
        std::string_view(expectedBuf.data(), expectedURLL - expectedBuf.data());

    // Run test
    if (!testRunner(test))
      return false;
  }

  return true;
}

template <typename TestRunner>
constexpr bool runEncodeTests(TestRunner testRunner) {
  return staticTests(testRunner) && manyZeroesTests(testRunner);
}

struct ConstexprTester {
  constexpr bool encodeTest(TestCase test) const {
    std::array<char, 1000> buf = {};
    std::string_view actual(
        buf.data(),
        base64EncodeScalar(test.data.begin(), test.data.end(), buf.data()));

    if (test.encodedStd == actual) {
      return true;
    }

    EXPECT_EQ(test.encodedStd, actual)
        << "Regular encoding mismatch. Input data:\n"
        << byteRangeToString(test.data);
    return false;
  }

  constexpr bool decodeTest(TestCase test) const {
    std::array<char, 1000> buf = {};
    auto res = base64DecodeScalar(
        test.encodedStd.data(),
        test.encodedStd.data() + test.encodedStd.size(),
        buf.begin());

    std::string_view decoded(buf.begin(), res.o - buf.begin());

    if (res.isSuccess && test.data == decoded) {
      return true;
    }

    EXPECT_TRUE(res.isSuccess) << "encoded: " << test.encodedStd;
    EXPECT_EQ(test.data, decoded) << "encoded: " << test.encodedStd;

    return false;
  }

  constexpr bool encodeURLTest(TestCase test) const {
    std::array<char, 1000> buf = {};
    std::string_view actual(
        buf.data(),
        base64URLEncodeScalar(test.data.begin(), test.data.end(), buf.data()));

    if (test.encodedURL == actual) {
      return true;
    }

    EXPECT_EQ(test.encodedURL, actual) << "URL encoding mismatch. Input data:\n"
                                       << byteRangeToString(test.data);
    return false;
  }

  constexpr bool decodeURLTest(TestCase test) const {
    auto oneInput = [&](std::string_view encoded) {
      std::array<char, 1000> buf = {};
      auto res = base64URLDecodeScalar(
          encoded.data(), encoded.data() + encoded.size(), buf.begin());

      std::string_view decoded(buf.begin(), res.o - buf.begin());
      if (res.isSuccess && test.data == decoded) {
        return true;
      }

      EXPECT_TRUE(res.isSuccess) << "encoded: " << encoded;
      EXPECT_EQ(test.data, decoded) << "encoded: " << encoded;

      return false;
    };

    return oneInput(test.encodedStd) && oneInput(test.encodedURL);
  }

  constexpr bool sizeTests(TestCase test) const {
    std::size_t encodedSize = base64EncodedSize(test.data.size());
    std::size_t encodedURLSize = base64URLEncodedSize(test.data.size());
    std::size_t decodedSize = base64DecodedSize(
        test.encodedStd.data(),
        test.encodedStd.data() + test.encodedStd.size());
    std::size_t decodedURLSize = base64URLDecodedSize(
        test.encodedURL.data(),
        test.encodedURL.data() + test.encodedURL.size());
    std::size_t decodedStdWithURlSize = base64URLDecodedSize(
        test.encodedStd.data(),
        test.encodedStd.data() + test.encodedStd.size());

    if (encodedSize == test.encodedStd.size() &&
        encodedURLSize == test.encodedURL.size() &&
        decodedSize == test.data.size() && decodedURLSize == test.data.size() &&
        decodedStdWithURlSize == test.data.size()) {
      return true;
    }

    EXPECT_EQ(test.encodedStd.size(), encodedSize) << test.encodedStd;
    EXPECT_EQ(test.encodedURL.size(), encodedURLSize) << test.encodedURL;
    EXPECT_EQ(test.data.size(), decodedSize) << test.encodedStd;
    EXPECT_EQ(test.data.size(), decodedStdWithURlSize) << test.encodedStd;
    EXPECT_EQ(test.data.size(), decodedURLSize) << test.encodedURL;
    return false;
  }

  constexpr bool operator()(TestCase test) const {
    return sizeTests(test) && encodeTest(test) && encodeURLTest(test) &&
        decodeTest(test) && decodeURLTest(test);
  }
};

struct SimdTester {
  using Encode = char* (*)(const char*, const char*, char*);
  using Decode = Base64DecodeResult (*)(const char*, const char*, char*);

  Encode encode;
  Encode encodeURL;
  Decode decode;
  Decode decodeURL;

  bool encodeTest(TestCase test) const {
    std::string actual(base64EncodedSize(test.data.size()), 0);
    encode(test.data.begin(), test.data.end(), actual.data());
    if (test.encodedStd == actual) {
      return true;
    }
    EXPECT_EQ(test.encodedStd, actual)
        << "Regular encoding mismatch. Input data:\n"
        << byteRangeToString(test.data);
    return false;
  }

  bool encodeURLTest(TestCase test) const {
    std::string actual(base64URLEncodedSize(test.data.size()), 0);
    encodeURL(test.data.begin(), test.data.end(), actual.data());
    if (test.encodedURL == actual) {
      return true;
    }
    EXPECT_EQ(test.encodedStd, actual) << "URL encoding mismatch. Input data:\n"
                                       << byteRangeToString(test.data);
    return false;
  }

  bool decodeTest(TestCase test) const {
    std::string actual(
        base64DecodedSize(
            test.encodedStd.data(),
            test.encodedStd.data() + test.encodedStd.size()),
        0);
    auto decodedResult = decode(
        test.encodedStd.data(),
        test.encodedStd.data() + test.encodedStd.size(),
        actual.data());

    if (decodedResult.isSuccess && test.data == actual) {
      return true;
    }

    EXPECT_TRUE(decodedResult.isSuccess) << byteRangeToString(test.data);
    EXPECT_EQ(test.data, actual) << byteRangeToString(test.data);
    return false;
  }

  bool decodeURLTest(TestCase test) const {
    auto oneInput = [&](std::string_view encoded) {
      std::string decoded(
          base64URLDecodedSize(encoded.data(), encoded.data() + encoded.size()),
          0);
      auto res = decodeURL(
          encoded.data(), encoded.data() + encoded.size(), decoded.data());

      if (res.isSuccess && test.data == decoded) {
        return true;
      }

      EXPECT_TRUE(res.isSuccess) << "encoded: " << encoded;
      EXPECT_EQ(test.data, decoded) << "encoded: " << encoded;
      return false;
    };

    return oneInput(test.encodedStd) && oneInput(test.encodedURL);
  }

  bool operator()(TestCase test) const {
    return encodeTest(test) && encodeURLTest(test) && decodeTest(test) &&
        decodeURLTest(test);
  }
};

TEST(Base64, ConstexprTests) {
  // Comment out the static assert to debug
  static_assert(runEncodeTests(ConstexprTester{}));
  ASSERT_TRUE(runEncodeTests(ConstexprTester{}));
}

TEST(Base64, SpecialCases) {
  ASSERT_TRUE(runEncodeTests(SimdTester{
      base64EncodeScalar,
      base64URLEncodeScalar,
      base64DecodeSWAR,
      base64URLDecodeSWAR}));
#if FOLLY_SSE_PREREQ(4, 2)
  ASSERT_TRUE(runEncodeTests(SimdTester{
      base64Encode_SSE4_2,
      base64URLEncode_SSE4_2,
      base64Decode_SSE4_2,
      base64URLDecodeSWAR}));
#endif
}

constexpr char kHasNegative0[] = {'A', 'b', 'c', -15, '\0'};
constexpr char kHasNegative1[] = {'a', 'b', 'c', 'd', 'a', -15, 'c',
                                  'd', 'a', 'b', 'c', 'd', 'a', 'b',
                                  'c', 'd', 'a', 'b', 'c', 'd', '\0'};

struct DecodingErrorDetectionTest {
  bool isSuccess;
  std::string_view input;
} constexpr kDecodingErrorDection[] = {
    // clang-format off
    { true,  "" },
    { false, "=" },
    { false, "==" },
    { false, "A" },
    { false, "B=" },
    { false, "ba=" },
    { true,  "00==" },
    { true,  "000=" },
    { false, "===" },
    { false, "0===" },
    { false, "aa=0" },
    { false, "aaaa""aaaa""aaaa""aaaa""0" },
    { true,  "aaaa""aaaa""aaaa""aaaa""00==" },
    { true,  "0aaa""aaaa""aaaa""aaaa""aaaa""aaaa" },
    { false, "$aaa""aaaa""aaaa""aaaa""aaaa""aaaa" },
    { false, "aaaa""aa$a""aaaa""aaaa""aaaa""aaaa" },
    { false, "aaaaa"},
    { false, kHasNegative0 },
    { false, kHasNegative1 },
    // clang-format on
};

constexpr std::string_view kDecodingOnlyURLValid[] = {
    "ba",
    "ba__",
    "ba__ba--ba__",
    "bA_/0a--ba+_",
    "_-==",
    "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-",
    "bA_/0a--ba+_bA_/0a--ba+_bA_/0a--ba+_bA_/0a--ba+_bA_/0a--ba+_bA_/0a--ba+_bA_/0a--ba+_bA_/0a--ba+_"
    "bA_/0a--ba+_bA_/0a--ba+_bA_/0a--ba+_bA_/0a--ba+_bA_/0a--ba+_bA_/0a--ba+_bA_/0a--ba+_bA_/0a--ba+_",
    "bA_/0a--ba+_bA_/0a--ba+_bA_/0a--ba+_bA_/0a--ba+_bA_/0a--ba+_bA_/0a--ba+_bA_/0a--ba+_bA_/0a--ba==",
};

template <bool isURLDecoder>
constexpr size_t decodedSize(std::string_view in) {
  const char* f = in.data();
  const char* l = in.data() + in.size();

  if constexpr (isURLDecoder) {
    return base64URLDecodedSize(f, l);
  } else {
    return base64DecodedSize(f, l);
  }
}

template <bool isURLDecoder, typename Decoder>
void triggerASANOnBadDecode(std::string_view in, Decoder decoder) {
  std::vector<char> buf(decodedSize<isURLDecoder>(in));
  decoder(in.data(), in.data() + in.size(), buf.data());
}

template <bool isURLDecoder, typename Decoder>
constexpr bool decodingErrorDectionTest(Decoder decoder) {
  std::array<char, 1000> buf = {};

  auto sizeTest = [&](std::string_view in, Base64DecodeResult r) {
    std::size_t allocatedSize = decodedSize<isURLDecoder>(in);

    std::size_t usedSize = static_cast<std::size_t>(r.o - buf.data());

    if (usedSize == allocatedSize) {
      return true;
    }

    if (r.isSuccess) {
      EXPECT_EQ(usedSize, allocatedSize) << in << " isURL: " << isURLDecoder;
      return false;
    }

    if (allocatedSize > 1000 || // overflow
        usedSize > allocatedSize) {
      EXPECT_LE(usedSize, allocatedSize) << in << " isURL: " << isURLDecoder;
      return false;
    }
    return true;
  };

  for (const auto& test : kDecodingErrorDection) {
    if (!std::is_constant_evaluated()) {
      triggerASANOnBadDecode<isURLDecoder>(test.input, decoder);
    }
    auto r = decoder(
        test.input.data(), test.input.data() + test.input.size(), buf.data());
    if (test.isSuccess != r.isSuccess) {
      EXPECT_EQ(test.isSuccess, r.isSuccess) << test.input;
      return false;
    }
    if (!sizeTest(test.input, r)) {
      return false;
    }
  }

  for (std::string_view URLOnly : kDecodingOnlyURLValid) {
    if (!std::is_constant_evaluated()) {
      triggerASANOnBadDecode<isURLDecoder>(URLOnly, decoder);
    }
    auto r =
        decoder(URLOnly.data(), URLOnly.data() + URLOnly.size(), buf.data());
    if (isURLDecoder != r.isSuccess) {
      EXPECT_EQ(isURLDecoder, r.isSuccess) << URLOnly;
      return false;
    }

    if (!sizeTest(URLOnly, r)) {
      return false;
    }
  }

  return true;
}

TEST(Base64, DecodingErrorDeteciton) {
  static_assert(decodingErrorDectionTest<false>(base64DecodeScalar));
  static_assert(decodingErrorDectionTest<true>(base64URLDecodeScalar));
  ASSERT_TRUE(decodingErrorDectionTest<false>(base64DecodeScalar));
  ASSERT_TRUE(decodingErrorDectionTest<true>(base64URLDecodeScalar));
  ASSERT_TRUE(decodingErrorDectionTest<false>(base64DecodeSWAR));
  ASSERT_TRUE(decodingErrorDectionTest<true>(base64URLDecodeSWAR));
#if FOLLY_SSE_PREREQ(4, 2)
  ASSERT_TRUE(decodingErrorDectionTest<false>(base64Decode_SSE4_2));
#endif
}

} // namespace
} // namespace folly::detail::base64_detail
