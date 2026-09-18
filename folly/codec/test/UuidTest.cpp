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
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <fmt/format.h>

#include <folly/Random.h>

#include <folly/codec/Uuid.h>
#include <folly/portability/GTest.h>

// Define a struct to hold our parse function pointers
struct UuidParseTestParam {
  using ParseFuncType = folly::UuidParseCode(std::string&, std::string_view);

  ParseFuncType* func;
  std::string name;

  UuidParseTestParam(ParseFuncType f, std::string n)
      : func(f), name(std::move(n)) {}
};

// Define the test fixture
class UuidParseTest : public ::testing::TestWithParam<UuidParseTestParam> {
 public:
  auto parse(std::string& output, const std::string& input) {
    return GetParam().func(output, input);
  }
};

// Test cases
TEST_P(UuidParseTest, ValidUuid) {
  std::string input = "123e4567-e89b-12d3-a456-4266141740fc";
  ASSERT_EQ(36, input.size()); // 36 characters - the right length
  auto expected = std::array<char, 16>{};
  std::memcpy(
      expected.data(),
      "\x12\x3e\x45\x67\xe8\x9b\x12\xd3\xa4\x56\x42\x66\x14\x17\x40\xfc",
      16);
  std::string output{};
  folly::UuidParseCode result = parse(output, input);
  EXPECT_EQ(result, folly::UuidParseCode::SUCCESS);
  EXPECT_EQ(output.size(), 16);
  std::array<char, 16> ot{};
  std::memcpy(ot.data(), output.data(), 16);
  EXPECT_EQ(ot, expected);
}

TEST_P(UuidParseTest, InvalidUuidTooShort) {
  std::string input = "123e4567-e89b-12d3-a456-42661417400";
  ASSERT_EQ(35, input.size()); // 35 characters - too short
  std::string output{};
  folly::UuidParseCode result = parse(output, input);
  EXPECT_EQ(result, folly::UuidParseCode::WRONG_LENGTH);
}

TEST_P(UuidParseTest, InvalidUuidTooLong) {
  std::string input = "123e4567-e89b-12d3-a456-4266141740000";
  ASSERT_EQ(37, input.size()); // 37 characters - too long
  std::string output{};
  folly::UuidParseCode result = parse(output, input);
  EXPECT_EQ(result, folly::UuidParseCode::WRONG_LENGTH);
}

TEST_P(UuidParseTest, InvalidUuidWrongDashes) {
  std::string input =
      "123e4567e-89b-12d3-a456-426614174000"; // dash moved over by 1 byte
  ASSERT_EQ(36, input.size()); // 36 characters - the right length
  std::string output{};
  folly::UuidParseCode result = parse(output, input);
  EXPECT_EQ(result, folly::UuidParseCode::INVALID_CHAR);
}

TEST_P(UuidParseTest, InvalidUuidInvalidCharacters) {
  const std::string good_input = "123e4567-e89b-12d3-a456-42661417400f";
  std::string test_input{};
  std::string output{};

  ASSERT_EQ(36, good_input.size()); // 36 characters - the right length
  EXPECT_EQ(parse(output, good_input), folly::UuidParseCode::SUCCESS)
      << "Good input wasn't actually good";

  // For each position in the UUID string
  for (size_t i = 0; i < good_input.length(); ++i) {
    test_input = good_input;

    if (good_input[i] == '-') {
      // For hyphen positions, try all non-hyphen bytes
      for (int c = 0; c <= 255; ++c) {
        if (c != '-') {
          test_input[i] = static_cast<unsigned char>(c);
          ASSERT_EQ(36, test_input.size()); // 36 characters - the right length
          EXPECT_EQ(
              parse(output, test_input), folly::UuidParseCode::INVALID_CHAR)
              << "Failed for position " << i << " with byte value " << c;
        }
      }
    } else {
      // For hex positions, try all non-hex bytes
      for (int c = 0; c <= 255; ++c) {
        if (!isxdigit(static_cast<unsigned char>(c))) {
          test_input[i] = static_cast<unsigned char>(c);
          ASSERT_EQ(36, test_input.size()); // 36 characters - the right length
          EXPECT_EQ(
              parse(output, test_input), folly::UuidParseCode::INVALID_CHAR)
              << "Failed for position " << i << " with byte value " << c;
        }
      }
    }
  }
}

// Create a vector of parse functions to test
static std::vector<UuidParseTestParam> getUuidParseTestParams() {
  std::vector<UuidParseTestParam> functions;

  // Always add the scalar implementation
  functions.emplace_back(folly::detail::uuid_parse_scalar, "Scalar");

  // Add the default implementation
  using StrParseFunc = folly::UuidParseCode (*)(std::string&, std::string_view);
  functions.emplace_back(
      static_cast<StrParseFunc>(folly::uuid_parse), "Default");

  // Conditionally add SIMD implementations
#if (FOLLY_X64 && defined(__AVX2__))
  functions.emplace_back(folly::detail::uuid_parse_avx2, "AVX2");
#endif

#if (FOLLY_X64 && defined(__SSSE3__))
  functions.emplace_back(folly::detail::uuid_parse_ssse3, "SSSE3");
#endif

  return functions;
}

// Instantiate the test suite
INSTANTIATE_TEST_SUITE_P(
    UuidParsers,
    UuidParseTest,
    ::testing::ValuesIn(getUuidParseTestParams()),
    [](const ::testing::TestParamInfo<UuidParseTestParam>& info_) {
      return info_.param.name;
    });

// Additional tests for comparing implementations
#if (FOLLY_X64 && (defined(__SSSE3__) || defined(__AVX2__)))
static std::string generateRandomGuid() {
  // Generate random bytes of the form 123e4567-e89b-12d3-a456-4266141740fc
  uint64_t a = folly::Random::rand64() & 0xffffffff;
  uint64_t b = folly::Random::rand64() & 0xffff;
  uint64_t c = folly::Random::rand64() & 0xffff;
  uint64_t d = folly::Random::rand64() & 0xffff;
  uint64_t e = folly::Random::rand64() & 0xffffffffffff;

  // Format with proper UUID structure
  if (folly::Random::oneIn(2)) {
    return fmt::format("{:08X}-{:04X}-{:04X}-{:04X}-{:012X}", a, b, c, d, e);
  } else {
    return fmt::format("{:08x}-{:04x}-{:04x}-{:04x}-{:012x}", a, b, c, d, e);
  }
}

// Test fixture for comparing SIMD implementations to scalar
class UuidParseComparisonTest
    : public ::testing::TestWithParam<UuidParseTestParam> {};

TEST_P(UuidParseComparisonTest, CompareToScalar) {
  auto simd_parse_func = GetParam().func;

  for (int i = 0; i < 1000; i++) {
    std::string test_input = generateRandomGuid();
    std::string scalar_output{};
    std::string simd_output{};

    EXPECT_EQ(
        folly::detail::uuid_parse_scalar(scalar_output, test_input),
        folly::UuidParseCode::SUCCESS);
    EXPECT_EQ(
        simd_parse_func(simd_output, test_input),
        folly::UuidParseCode::SUCCESS);
    EXPECT_EQ(scalar_output, simd_output);
  }
}

// Get SIMD implementations only for comparison tests
static std::vector<UuidParseTestParam> getSimdParseFunctions() {
  std::vector<UuidParseTestParam> functions;

#if (FOLLY_X64 && defined(__AVX2__))
  functions.emplace_back(folly::detail::uuid_parse_avx2, "AVX2");
#endif

#if (FOLLY_X64 && defined(__SSSE3__))
  functions.emplace_back(folly::detail::uuid_parse_ssse3, "SSSE3");
#endif

  return functions;
}

INSTANTIATE_TEST_SUITE_P(
    SimdVsScalar,
    UuidParseComparisonTest,
    ::testing::ValuesIn(getSimdParseFunctions()),
    [](const ::testing::TestParamInfo<UuidParseTestParam>& info_) {
      return "CompareScalarTo" + info_.param.name;
    });
#endif

namespace {

std::array<std::uint8_t, 16> randomUuidBytes() {
  std::array<std::uint8_t, 16> bytes{};
  folly::Random::secureRandom(bytes.data(), bytes.size());
  return bytes;
}

// Independent rendering of the canonical 8-4-4-4-12 form, used as the oracle.
std::string expectedUuidString(const std::uint8_t* in, bool upper) {
  std::string out;
  for (int i = 0; i < 16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) {
      out += '-';
    }
    out += upper ? fmt::format("{:02X}", in[i]) : fmt::format("{:02x}", in[i]);
  }
  return out;
}

} // namespace

TEST(UuidUnparseTest, MatchesCanonicalRendering) {
  for (int i = 0; i < 1000; ++i) {
    const auto raw = randomUuidBytes();

    std::string actualUpper;
    std::string actualLower;
    folly::uuid_unparse_upper(actualUpper, raw.data());
    folly::uuid_unparse_lower(actualLower, raw.data());

    ASSERT_EQ(expectedUuidString(raw.data(), true), actualUpper);
    ASSERT_EQ(expectedUuidString(raw.data(), false), actualLower);
  }
}

TEST(UuidUnparseTest, RoundTripsThroughParse) {
  for (int i = 0; i < 1000; ++i) {
    const auto raw = randomUuidBytes();

    std::string text;
    folly::uuid_unparse_lower(text, raw.data());

    std::string parsed;
    ASSERT_EQ(folly::uuid_parse(parsed, text), folly::UuidParseCode::SUCCESS);
    ASSERT_EQ(
        std::string_view(reinterpret_cast<const char*>(raw.data()), 16),
        parsed);
  }
}

TEST(UuidUnparseTest, BufferOverloadNulTerminatesAndDoesNotOverrun) {
  const auto raw = randomUuidBytes();

  char out[39];
  std::memset(out, '\xFF', sizeof(out));
  folly::uuid_unparse_upper(out + 1, raw.data());

  EXPECT_EQ(out[0], '\xFF') << "wrote before the buffer";
  EXPECT_EQ(out[37], '\0') << "missing NUL terminator";
  EXPECT_EQ(out[38], '\xFF') << "wrote past the terminator";
  EXPECT_EQ(std::strlen(out + 1), 36u);
}

// The std::string overload leaves termination to the string itself.
TEST(UuidUnparseTest, StringOverloadIsExactly36Chars) {
  const auto raw = randomUuidBytes();

  std::string out = "some pre-existing longer value";
  folly::uuid_unparse_upper(out, raw.data());

  EXPECT_EQ(out.size(), 36u);
  EXPECT_EQ(std::strlen(out.c_str()), 36u);
}

// Sequential input bytes must appear in order across the 8-4-4-4-12 layout.
TEST(UuidUnparseTest, KnownVectors) {
  const std::uint8_t sequential[16] = {
      0x00,
      0x11,
      0x22,
      0x33,
      0x44,
      0x55,
      0x66,
      0x77,
      0x88,
      0x99,
      0xAA,
      0xBB,
      0xCC,
      0xDD,
      0xEE,
      0xFF};
  const std::uint8_t zeros[16] = {};
  std::uint8_t ones[16];
  std::memset(ones, 0xFF, sizeof(ones));

  std::string out;

  folly::uuid_unparse_lower(out, sequential);
  EXPECT_EQ("00112233-4455-6677-8899-aabbccddeeff", out);
  folly::uuid_unparse_upper(out, sequential);
  EXPECT_EQ("00112233-4455-6677-8899-AABBCCDDEEFF", out);

  folly::uuid_unparse_lower(out, zeros);
  EXPECT_EQ("00000000-0000-0000-0000-000000000000", out);

  folly::uuid_unparse_lower(out, ones);
  EXPECT_EQ("ffffffff-ffff-ffff-ffff-ffffffffffff", out);
}

// Filling all 16 bytes with the same value exercises one hex-pair table entry,
// both nibbles, at every output offset. Covers all 256 entries of both tables.
TEST(UuidUnparseTest, AllByteValues) {
  for (int v = 0; v < 256; ++v) {
    std::uint8_t in[16];
    std::memset(in, static_cast<std::uint8_t>(v), sizeof(in));

    std::string actualUpper;
    std::string actualLower;
    folly::uuid_unparse_upper(actualUpper, in);
    folly::uuid_unparse_lower(actualLower, in);

    ASSERT_EQ(expectedUuidString(in, true), actualUpper) << "byte " << v;
    ASSERT_EQ(expectedUuidString(in, false), actualLower) << "byte " << v;
  }
}
