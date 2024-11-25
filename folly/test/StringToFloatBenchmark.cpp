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

// This benchmarks various functions that convert
// decimal values in string representation to IEEE-754 double representation.
//
// Functions benchmarked:
// - `std::strtof`
// - `std::strtof` with a copy (needed for non-null-terminated strings)
// - `std::strtofl` with the C locale (for locale-indepent processing)
// - `std::from_chars`
// - libdouble-conversion
// - libfast_float
//
// The functions are benchmarked on different inputs.
// Types of input:
// - hardcoded decimal notation
// - hardcoded exponentional notation
// - randomly generated values in the double space range
// - single-digit integers
// - double-digit integers
// - four-digit percentages (e.g., AB.XY)

#include <charconv>
#include <cmath>
#include <iostream>
#include <random>
#include <utility>

#include <folly/Benchmark.h>
#include <folly/Traits.h>
#include <folly/stop_watch.h>

#include <double-conversion/double-conversion.h>
#include <fast_float/fast_float.h> // @manual=fbsource//third-party/fast_float:fast_float

template <typename T, typename... A>
using detect_std_from_chars = decltype(void(std::from_chars(
    nullptr, nullptr, std::declval<T&>(), std::declval<A const&>()...)));

/// invoke_std_from_chars
///
/// Invokes std::from_chars if the requested overload is available.
///
/// Cannot use if-constexpr directly within benchmarks to accomplish this since
/// benchmarks are not function templates.
template <typename T, typename... A>
void invoke_std_from_chars(
    char const* first, char const* last, T& value, A const&... a) {
  if constexpr (folly::is_detected_v<detect_std_from_chars, T, A...>) {
    std::from_chars(first, last, value, a...);
  }
}

const char* kInputZero = "0";

/// An arbitrary hardcoded decimal number to benchmark
constexpr const char* kDecimalCStr = "1234567890987654321.51";
constexpr std::string_view kDecimalStr(kDecimalCStr);

/// An arbitrary hardcoded exponent number to benchmark
constexpr const char* kexponentialNotationCStr = "1.1234567890987654E21";
constexpr std::string_view kexponentialNotationStr(kexponentialNotationCStr);

static double_conversion::StringToDoubleConverter conv(
    double_conversion::StringToDoubleConverter::ALLOW_TRAILING_JUNK |
        double_conversion::StringToDoubleConverter::ALLOW_LEADING_SPACES,
    0.0,
    // return this for junk input string
    std::numeric_limits<double>::quiet_NaN(),
    nullptr,
    nullptr);

/// test data that is initialized in `main`
static std::vector<std::string> randomValues;
static std::vector<std::string> singleDigitIntValues;
static std::vector<std::string> doubleDigitIntValues;
static std::vector<std::string> fourDigitPercentageValues;

BENCHMARK(hardcoded_decimal_notation_STRTOF, n) {
  char* end;
  for (unsigned int i = 0; i < n; ++i) {
    std::strtod(kDecimalCStr, &end);
  }
}

BENCHMARK(hardcoded_decimal_notation_STRTOF_COPY, n) {
  char* end;
  for (unsigned int i = 0; i < n; ++i) {
    std::string s(kDecimalStr.data(), kDecimalStr.size());
    std::strtod(s.c_str(), &end);
  }
}

namespace {
/// Single instance of a "C" Numeric category locale.
/// This is used to have "C" based locale formatting in
/// `str_to_floating_strtof`.
class PosixNumericLocale {
 public:
  static const auto& get() {
    static PosixNumericLocale p;
    return p.locale_;
  }

 private:
#if defined(_WIN32)
  _locale_t locale_;
  PosixNumericLocale() : locale_(_create_locale(LC_NUMERIC, "C")) {}
  ~PosixNumericLocale() { _free_locale(locale_); }
#else
  locale_t locale_;
  PosixNumericLocale()
      : locale_(newlocale(LC_NUMERIC_MASK, "C", static_cast<locale_t>(0))) {}
  ~PosixNumericLocale() { freelocale(locale_); }
#endif
};
} // namespace

#if defined(_WIN32)
constexpr auto strtod_l = _strtod_l;
#endif

BENCHMARK(hardcoded_decimal_notation_STRTOFL_COPY, n) {
  char* end;
  for (unsigned int i = 0; i < n; ++i) {
    std::string s(kDecimalStr.data(), kDecimalStr.size());
    strtod_l(s.c_str(), &end, PosixNumericLocale::get());
  }
}

BENCHMARK(hardcoded_decimal_notation_STD_FROM_CHARS, n) {
  double value{};
  for (unsigned int i = 0; i < n; ++i) {
    invoke_std_from_chars(
        kDecimalStr.data(), kDecimalStr.data() + kDecimalStr.size(), value);
  }
}

BENCHMARK(hardcoded_decimal_notation_DoubleConversion, n) {
  int processedCharsCount;
  for (unsigned int i = 0; i < n; ++i) {
    conv.StringToFloat(
        kDecimalStr.data(),
        static_cast<int>(kDecimalStr.size()),
        &processedCharsCount);
  }
}

BENCHMARK(hardcoded_decimal_notation_FAST_FLOAT, n) {
  double value{};
  for (unsigned int i = 0; i < n; ++i) {
    fast_float::from_chars(
        kDecimalStr.data(), kDecimalStr.data() + kDecimalStr.size(), value);
  }
}

BENCHMARK(hardcoded_exponential_notation_STRTOF, n) {
  char* end;
  for (unsigned int i = 0; i < n; ++i) {
    std::strtod(kexponentialNotationCStr, &end);
  }
}

BENCHMARK(hardcoded_exponential_notation_STRTOF_COPY, n) {
  char* end;
  for (unsigned int i = 0; i < n; ++i) {
    std::string s(kDecimalStr.data(), kDecimalStr.size());
    std::strtod(s.c_str(), &end);
  }
}

BENCHMARK(hardcoded_exponential_notation_STRTOFL_COPY, n) {
  char* end;
  for (unsigned int i = 0; i < n; ++i) {
    std::string s(
        kexponentialNotationStr.data(), kexponentialNotationStr.size());
    strtod_l(s.c_str(), &end, PosixNumericLocale::get());
  }
}

BENCHMARK(hardcoded_exponential_notation_STD_FROM_CHARS, n) {
  for (unsigned int i = 0; i < n; ++i) {
    double value{};
    invoke_std_from_chars(
        kexponentialNotationStr.data(),
        kexponentialNotationStr.data() + kexponentialNotationStr.size(),
        value);
  }
}

BENCHMARK(hardcoded_exponential_notation_DoubleConversion, n) {
  int processedCharsCount;
  for (unsigned int i = 0; i < n; ++i) {
    conv.StringToFloat(
        kexponentialNotationStr.data(),
        static_cast<int>(kexponentialNotationStr.size()),
        &processedCharsCount);
  }
}

BENCHMARK(hardcoded_exponential_notation_FAST_FLOAT, n) {
  double value{};
  for (unsigned int i = 0; i < n; ++i) {
    fast_float::from_chars(
        kexponentialNotationStr.data(),
        kexponentialNotationStr.data() + kexponentialNotationStr.size(),
        value);
  }
}

int doubleConversionRandomInputIndex = 0;
BENCHMARK(random_input_DoubleConversion, n) {
  int processedCharsCount;
  for (unsigned int i = 0; i < n; ++i) {
    std::string& input = randomValues[doubleConversionRandomInputIndex];
    doubleConversionRandomInputIndex += 1;
    conv.StringToFloat(
        input.data(), static_cast<int>(input.size()), &processedCharsCount);
  }
}

int fastFloatRandomInputIndex = 0;
BENCHMARK(random_input_FAST_FLOAT, n) {
  double value{};
  for (unsigned int i = 0; i < n; ++i) {
    std::string& input = randomValues[fastFloatRandomInputIndex];
    fastFloatRandomInputIndex += 1;
    char* b = input.data();
    char* e = b + input.size();
    fast_float::from_chars(b, e, value);
  }
}

BENCHMARK(zero_input_DoubleConversion, n) {
  int processedCharsCount;
  for (unsigned int i = 0; i < n; ++i) {
    doubleConversionRandomInputIndex += 1;
    conv.StringToFloat(kInputZero, 1, &processedCharsCount);
  }
}

BENCHMARK(zero_input_FAST_FLOAT, n) {
  double value{};
  for (unsigned int i = 0; i < n; ++i) {
    fast_float::from_chars(kInputZero, kInputZero + 1, value);
  }
}

BENCHMARK(single_digit_ints_DoubleConversion, n) {
  int processedCharsCount;
  // mask to select one of the first 8 out of 10 values.
  // this is done to avoid additional branching or modulus.
  constexpr std::size_t kSelectionMask = 8 - 1;
  for (std::size_t i = 0; i < n; i++) {
    std::string& input = singleDigitIntValues[i & kSelectionMask];
    conv.StringToFloat(
        input.data(), static_cast<int>(input.size()), &processedCharsCount);
  }
}

BENCHMARK(single_digit_ints_FAST_FLOAT, n) {
  double value{};
  constexpr std::size_t kSelectionMask = 8 - 1;
  for (std::size_t i = 0; i < n; i++) {
    std::string& input = singleDigitIntValues[i & kSelectionMask];
    char* b = input.data();
    char* e = b + input.size();
    fast_float::from_chars(b, e, value);
  }
}

BENCHMARK(double_digit_ints_DoubleConversion, n) {
  int processedCharsCount;
  // mask to select one of the first 64 out of 100 values.
  // this is done to avoid additional branching or modulus.
  constexpr std::size_t kSelectioMask = 64 - 1;
  for (std::size_t i = 0; i < n; i++) {
    std::string& input = doubleDigitIntValues[i & kSelectioMask];
    conv.StringToFloat(
        input.data(), static_cast<int>(input.size()), &processedCharsCount);
  }
}

BENCHMARK(double_digit_ints_FAST_FLOAT, n) {
  double value{};
  constexpr std::size_t kSelectioMask = 64 - 1;
  for (std::size_t i = 0; i < n; i++) {
    std::string& input = doubleDigitIntValues[i & kSelectioMask];
    char* b = input.data();
    char* e = b + input.size();
    fast_float::from_chars(b, e, value);
  }
}

BENCHMARK(four_digit_percentages_DoubleConversion, n) {
  int processedCharsCount;
  // mask to select one of the first 8096 out of 10,000 values.
  // this is done to avoid additional branching or modulus.
  constexpr std::size_t kSelectioMask = 8096 - 1;
  for (std::size_t i = 0; i < n; i++) {
    std::string& input = fourDigitPercentageValues[i & kSelectioMask];
    conv.StringToFloat(
        input.data(), static_cast<int>(input.size()), &processedCharsCount);
  }
}

BENCHMARK(four_digit_percentages_FAST_FLOAT, n) {
  double value{};
  constexpr std::size_t kSelectioMask = 8096 - 1;
  for (std::size_t i = 0; i < n; i++) {
    std::string& input = fourDigitPercentageValues[i & kSelectioMask];
    char* b = input.data();
    char* e = b + input.size();
    fast_float::from_chars(b, e, value);
  }
}

int main(int argc, char** argv) {
  folly::gflags::ParseCommandLineFlags(&argc, &argv, true);

  std::cout << "Generating input...";
  folly::stop_watch<std::chrono::milliseconds> stopwatch;
  std::random_device rd; // a seed source for the random number engine
  std::mt19937 gen(rd()); // mersenne_twister_engine seeded with rd()
  constexpr int kNumValues =
      10'000'000; // enough values for the the benchmark iterations

  {
    // this block generates random values by separately sampling values
    // for the exponent and the base (a.k.a., mantissa).
    // This is done to select from a wider range of values.
    // As opposed to sampling from uniform_real_distribution(min, max), which
    // gravitates towards values with large exponents because of the larger
    // proportion of them.
    std::uniform_int_distribution<> intDist(
        -1022, 1023); // the IEEE 754 binary64 exponent range
    std::uniform_real_distribution<> realDist(-1.0, 1.0);

    randomValues.reserve(kNumValues);
    char buffer[256];
    for (int i = 0; i < kNumValues; ++i) {
      int exp = intDist(gen);
      double mantissa = realDist(gen);
      // ldexp does mantissa * 2^exp
      double value = std::ldexp(mantissa, exp);
      auto [end, err] = std::to_chars(buffer, buffer + sizeof(buffer), value);
      if (err != std::errc()) {
        throw std::runtime_error("failed to convert to string");
      }
      randomValues.push_back(std::string(buffer, end));
    }
  }

  {
    // generates single digit ints values 0 through 9
    for (char i = '0'; i <= '9'; ++i) {
      singleDigitIntValues.push_back(std::string(1, i));
    }

    // shuffle to fuzz out performance of different ordering
    std::shuffle(singleDigitIntValues.begin(), singleDigitIntValues.end(), gen);
  }

  {
    // generates double digit ints values 10 through 99
    char buf[2];
    for (char i = '1'; i <= '9'; ++i) {
      buf[0] = i;
      for (char j = '0'; j <= '9'; ++j) {
        buf[1] = j;
        doubleDigitIntValues.push_back(std::string(buf, 2));
      }
    }
    std::shuffle(doubleDigitIntValues.begin(), doubleDigitIntValues.end(), gen);
  }

  {
    // generate four digit percentage values 00.00 through 99.99
    char buf[5];
    buf[2] = '.';

    for (char a = '0'; a <= '9'; ++a) {
      buf[0] = a;
      for (char b = '0'; b <= '9'; ++b) {
        buf[1] = b;
        for (char x = '0'; x <= '9'; ++x) {
          buf[3] = x;
          for (char y = '0'; y <= '9'; ++y) {
            buf[4] = y;
            fourDigitPercentageValues.push_back(std::string(buf, 5));
          }
        }
      }
    }
    std::shuffle(
        fourDigitPercentageValues.begin(),
        fourDigitPercentageValues.end(),
        gen);
  }

  std::cout << "done in " << stopwatch.elapsed().count() << "ms" << std::endl;

  std::cout << "First 10 random values: " << std::endl;

  for (int i = 0; i < 10; ++i) {
    std::cout << randomValues[i] << std::endl;
  }

  folly::runBenchmarks();
  return 0;
}
