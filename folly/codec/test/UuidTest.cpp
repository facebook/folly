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

#include <cctype>
#include <cstdlib>
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
