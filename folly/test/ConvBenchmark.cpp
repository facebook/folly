/*
 * Copyright 2016 Facebook, Inc.
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

#include <folly/Conv.h>

#include <boost/lexical_cast.hpp>

#include <folly/Benchmark.h>
#include <folly/Foreach.h>

#include <array>
#include <limits>
#include <stdexcept>

using namespace std;
using namespace folly;

////////////////////////////////////////////////////////////////////////////////
// Benchmarks for ASCII to int conversion
////////////////////////////////////////////////////////////////////////////////
// @author: Rajat Goel (rajat)

static int64_t handwrittenAtoi(const char* start, const char* end) {

  bool positive = true;
  int64_t retVal = 0;

  if (start == end) {
    throw std::runtime_error("empty string");
  }

  while (start < end && isspace(*start)) {
    ++start;
  }

  switch (*start) {
    case '-':
      positive = false;
    case '+':
      ++start;
    default:
      ;
  }

  while (start < end && *start >= '0' && *start <= '9') {
    auto const newRetVal = retVal * 10 + (*start++ - '0');
    if (newRetVal < retVal) {
      throw std::runtime_error("overflow");
    }
    retVal = newRetVal;
  }

  if (start != end) {
    throw std::runtime_error("extra chars at the end");
  }

  return positive ? retVal : -retVal;
}

static StringPiece pc1 = "1234567890123456789";

void handwrittenAtoiMeasure(unsigned int n, unsigned int digits) {
  auto p = pc1.subpiece(pc1.size() - digits, digits);
  FOR_EACH_RANGE(i, 0, n) {
    doNotOptimizeAway(handwrittenAtoi(p.begin(), p.end()));
  }
}

void follyAtoiMeasure(unsigned int n, unsigned int digits) {
  auto p = pc1.subpiece(pc1.size() - digits, digits);
  FOR_EACH_RANGE(i, 0, n) {
    doNotOptimizeAway(folly::to<int64_t>(p.begin(), p.end()));
  }
}

void clibAtoiMeasure(unsigned int n, unsigned int digits) {
  auto p = pc1.subpiece(pc1.size() - digits, digits);
  assert(*p.end() == 0);
  static_assert(sizeof(long) == 8, "64-bit long assumed");
  FOR_EACH_RANGE(i, 0, n) { doNotOptimizeAway(atol(p.begin())); }
}

void clibStrtoulMeasure(unsigned int n, unsigned int digits) {
  auto p = pc1.subpiece(pc1.size() - digits, digits);
  assert(*p.end() == 0);
  char* endptr;
  FOR_EACH_RANGE(i, 0, n) {
    doNotOptimizeAway(strtoul(p.begin(), &endptr, 10));
  }
}

void lexicalCastMeasure(unsigned int n, unsigned int digits) {
  auto p = pc1.subpiece(pc1.size() - digits, digits);
  assert(*p.end() == 0);
  FOR_EACH_RANGE(i, 0, n) {
    doNotOptimizeAway(boost::lexical_cast<uint64_t>(p.begin()));
  }
}

// Benchmarks for unsigned to string conversion, raw

unsigned u64ToAsciiTable(uint64_t value, char* dst) {
  static const char digits[201] =
      "00010203040506070809"
      "10111213141516171819"
      "20212223242526272829"
      "30313233343536373839"
      "40414243444546474849"
      "50515253545556575859"
      "60616263646566676869"
      "70717273747576777879"
      "80818283848586878889"
      "90919293949596979899";

  uint32_t const length = digits10(value);
  uint32_t next = length - 1;
  while (value >= 100) {
    auto const i = (value % 100) * 2;
    value /= 100;
    dst[next] = digits[i + 1];
    dst[next - 1] = digits[i];
    next -= 2;
  }
  // Handle last 1-2 digits
  if (value < 10) {
    dst[next] = '0' + uint32_t(value);
  } else {
    auto i = uint32_t(value) * 2;
    dst[next] = digits[i + 1];
    dst[next - 1] = digits[i];
  }
  return length;
}

void u64ToAsciiTableBM(unsigned int n, uint64_t value) {
  // This is too fast, need to do 10 times per iteration
  char buf[20];
  FOR_EACH_RANGE(i, 0, n) {
    doNotOptimizeAway(u64ToAsciiTable(value + n, buf));
  }
}

unsigned u64ToAsciiClassic(uint64_t value, char* dst) {
  // Write backwards.
  char* next = (char*)dst;
  char* start = next;
  do {
    *next++ = '0' + (value % 10);
    value /= 10;
  } while (value != 0);
  unsigned length = next - start;

  // Reverse in-place.
  next--;
  while (next > start) {
    char swap = *next;
    *next = *start;
    *start = swap;
    next--;
    start++;
  }
  return length;
}

void u64ToAsciiClassicBM(unsigned int n, uint64_t value) {
  // This is too fast, need to do 10 times per iteration
  char buf[20];
  FOR_EACH_RANGE(i, 0, n) {
    doNotOptimizeAway(u64ToAsciiClassic(value + n, buf));
  }
}

void u64ToAsciiFollyBM(unsigned int n, uint64_t value) {
  // This is too fast, need to do 10 times per iteration
  char buf[20];
  FOR_EACH_RANGE(i, 0, n) {
    doNotOptimizeAway(uint64ToBufferUnsafe(value + n, buf));
  }
}

// Benchmark unsigned to string conversion

void u64ToStringClibMeasure(unsigned int n, uint64_t value) {
  // FOLLY_RANGE_CHECK_TO_STRING expands to std::to_string, except on Android
  // where std::to_string is not supported
  FOR_EACH_RANGE(i, 0, n) { FOLLY_RANGE_CHECK_TO_STRING(value + n); }
}

void u64ToStringFollyMeasure(unsigned int n, uint64_t value) {
  FOR_EACH_RANGE(i, 0, n) { to<std::string>(value + n); }
}

// Benchmark uitoa with string append

void u2aAppendClassicBM(unsigned int n, uint64_t value) {
  string s;
  FOR_EACH_RANGE(i, 0, n) {
    // auto buf = &s.back() + 1;
    char buffer[20];
    s.append(buffer, u64ToAsciiClassic(value, buffer));
    doNotOptimizeAway(s.size());
  }
}

void u2aAppendFollyBM(unsigned int n, uint64_t value) {
  string s;
  FOR_EACH_RANGE(i, 0, n) {
    // auto buf = &s.back() + 1;
    char buffer[20];
    s.append(buffer, uint64ToBufferUnsafe(value, buffer));
    doNotOptimizeAway(s.size());
  }
}

template <class String>
struct StringIdenticalToBM {
  StringIdenticalToBM() {}
  void operator()(unsigned int n, size_t len) const {
    String s;
    BENCHMARK_SUSPEND { s.append(len, '0'); }
    FOR_EACH_RANGE(i, 0, n) {
      String result = to<String>(s);
      doNotOptimizeAway(result.size());
    }
  }
};

template <class String>
struct StringVariadicToBM {
  StringVariadicToBM() {}
  void operator()(unsigned int n, size_t len) const {
    String s;
    BENCHMARK_SUSPEND { s.append(len, '0'); }
    FOR_EACH_RANGE(i, 0, n) {
      String result = to<String>(s, nullptr);
      doNotOptimizeAway(result.size());
    }
  }
};

static size_t bigInt = 11424545345345;
static size_t smallInt = 104;
static char someString[] = "this is some nice string";
static char otherString[] = "this is a long string, so it's not so nice";
static char reallyShort[] = "meh";
static std::string stdString = "std::strings are very nice";
static float fValue = 1.2355;
static double dValue = 345345345.435;

BENCHMARK(preallocateTestNoFloat, n) {
  for (size_t i = 0; i < n; ++i) {
    auto val1 = to<std::string>(bigInt, someString, stdString, otherString);
    auto val3 = to<std::string>(reallyShort, smallInt);
    auto val2 = to<std::string>(bigInt, stdString);
    auto val4 = to<std::string>(bigInt, stdString, dValue, otherString);
    auto val5 = to<std::string>(bigInt, someString, reallyShort);
  }
}

BENCHMARK(preallocateTestFloat, n) {
  for (size_t i = 0; i < n; ++i) {
    auto val1 = to<std::string>(stdString, ',', fValue, dValue);
    auto val2 = to<std::string>(stdString, ',', dValue);
  }
}
BENCHMARK_DRAW_LINE();

static const StringIdenticalToBM<std::string> stringIdenticalToBM;
static const StringVariadicToBM<std::string> stringVariadicToBM;
static const StringIdenticalToBM<fbstring> fbstringIdenticalToBM;
static const StringVariadicToBM<fbstring> fbstringVariadicToBM;

#define DEFINE_BENCHMARK_GROUP(n)                 \
  BENCHMARK_PARAM(u64ToAsciiClassicBM, n);        \
  BENCHMARK_RELATIVE_PARAM(u64ToAsciiTableBM, n); \
  BENCHMARK_RELATIVE_PARAM(u64ToAsciiFollyBM, n); \
  BENCHMARK_DRAW_LINE();

DEFINE_BENCHMARK_GROUP(1);
DEFINE_BENCHMARK_GROUP(12);
DEFINE_BENCHMARK_GROUP(123);
DEFINE_BENCHMARK_GROUP(1234);
DEFINE_BENCHMARK_GROUP(12345);
DEFINE_BENCHMARK_GROUP(123456);
DEFINE_BENCHMARK_GROUP(1234567);
DEFINE_BENCHMARK_GROUP(12345678);
DEFINE_BENCHMARK_GROUP(123456789);
DEFINE_BENCHMARK_GROUP(1234567890);
DEFINE_BENCHMARK_GROUP(12345678901);
DEFINE_BENCHMARK_GROUP(123456789012);
DEFINE_BENCHMARK_GROUP(1234567890123);
DEFINE_BENCHMARK_GROUP(12345678901234);
DEFINE_BENCHMARK_GROUP(123456789012345);
DEFINE_BENCHMARK_GROUP(1234567890123456);
DEFINE_BENCHMARK_GROUP(12345678901234567);
DEFINE_BENCHMARK_GROUP(123456789012345678);
DEFINE_BENCHMARK_GROUP(1234567890123456789);
DEFINE_BENCHMARK_GROUP(12345678901234567890U);

#undef DEFINE_BENCHMARK_GROUP

#define DEFINE_BENCHMARK_GROUP(n)                       \
  BENCHMARK_PARAM(u64ToStringClibMeasure, n);           \
  BENCHMARK_RELATIVE_PARAM(u64ToStringFollyMeasure, n); \
  BENCHMARK_DRAW_LINE();

DEFINE_BENCHMARK_GROUP(1);
DEFINE_BENCHMARK_GROUP(12);
DEFINE_BENCHMARK_GROUP(123);
DEFINE_BENCHMARK_GROUP(1234);
DEFINE_BENCHMARK_GROUP(12345);
DEFINE_BENCHMARK_GROUP(123456);
DEFINE_BENCHMARK_GROUP(1234567);
DEFINE_BENCHMARK_GROUP(12345678);
DEFINE_BENCHMARK_GROUP(123456789);
DEFINE_BENCHMARK_GROUP(1234567890);
DEFINE_BENCHMARK_GROUP(12345678901);
DEFINE_BENCHMARK_GROUP(123456789012);
DEFINE_BENCHMARK_GROUP(1234567890123);
DEFINE_BENCHMARK_GROUP(12345678901234);
DEFINE_BENCHMARK_GROUP(123456789012345);
DEFINE_BENCHMARK_GROUP(1234567890123456);
DEFINE_BENCHMARK_GROUP(12345678901234567);
DEFINE_BENCHMARK_GROUP(123456789012345678);
DEFINE_BENCHMARK_GROUP(1234567890123456789);
DEFINE_BENCHMARK_GROUP(12345678901234567890U);

#undef DEFINE_BENCHMARK_GROUP

#define DEFINE_BENCHMARK_GROUP(n)                      \
  BENCHMARK_PARAM(clibAtoiMeasure, n);                 \
  BENCHMARK_RELATIVE_PARAM(lexicalCastMeasure, n);     \
  BENCHMARK_RELATIVE_PARAM(handwrittenAtoiMeasure, n); \
  BENCHMARK_RELATIVE_PARAM(follyAtoiMeasure, n);       \
  BENCHMARK_DRAW_LINE();

DEFINE_BENCHMARK_GROUP(1);
DEFINE_BENCHMARK_GROUP(2);
DEFINE_BENCHMARK_GROUP(3);
DEFINE_BENCHMARK_GROUP(4);
DEFINE_BENCHMARK_GROUP(5);
DEFINE_BENCHMARK_GROUP(6);
DEFINE_BENCHMARK_GROUP(7);
DEFINE_BENCHMARK_GROUP(8);
DEFINE_BENCHMARK_GROUP(9);
DEFINE_BENCHMARK_GROUP(10);
DEFINE_BENCHMARK_GROUP(11);
DEFINE_BENCHMARK_GROUP(12);
DEFINE_BENCHMARK_GROUP(13);
DEFINE_BENCHMARK_GROUP(14);
DEFINE_BENCHMARK_GROUP(15);
DEFINE_BENCHMARK_GROUP(16);
DEFINE_BENCHMARK_GROUP(17);
DEFINE_BENCHMARK_GROUP(18);
DEFINE_BENCHMARK_GROUP(19);

#undef DEFINE_BENCHMARK_GROUP

#define DEFINE_BENCHMARK_GROUP(T, n)             \
  BENCHMARK_PARAM(T##VariadicToBM, n);           \
  BENCHMARK_RELATIVE_PARAM(T##IdenticalToBM, n); \
  BENCHMARK_DRAW_LINE();

DEFINE_BENCHMARK_GROUP(string, 32);
DEFINE_BENCHMARK_GROUP(string, 1024);
DEFINE_BENCHMARK_GROUP(string, 32768);
DEFINE_BENCHMARK_GROUP(fbstring, 32);
DEFINE_BENCHMARK_GROUP(fbstring, 1024);
DEFINE_BENCHMARK_GROUP(fbstring, 32768);

#undef DEFINE_BENCHMARK_GROUP

namespace {

template <typename T>
inline void stringToTypeClassic(const char* str, uint32_t n) {
  for (uint32_t i = 0; i < n; ++i) {
    try {
      auto val = to<T>(str);
      doNotOptimizeAway(val);
    } catch (const std::exception& e) {
      doNotOptimizeAway(e.what());
    }
    doNotOptimizeAway(i);
  }
}

template <typename T>
inline void ptrPairToIntClassic(StringPiece sp, uint32_t n) {
  for (uint32_t i = 0; i < n; ++i) {
    try {
      auto val = to<T>(sp.begin(), sp.end());
      doNotOptimizeAway(val);
    } catch (const std::exception& e) {
      doNotOptimizeAway(e.what());
    }
    doNotOptimizeAway(i);
  }
}

constexpr uint32_t kArithNumIter = 10000;

template <typename T, typename U>
inline size_t arithToArithClassic(const U* in, uint32_t numItems) {
  for (uint32_t i = 0; i < kArithNumIter; ++i) {
    for (uint32_t j = 0; j < numItems; ++j) {
      try {
        auto val = to<T>(in[j]);
        doNotOptimizeAway(val);
      } catch (const std::exception& e) {
        doNotOptimizeAway(e.what());
      }
      doNotOptimizeAway(j);
    }
    doNotOptimizeAway(i);
  }

  return kArithNumIter * numItems;
}

} // namespace

namespace conv {

std::array<int, 4> int2ScharGood{{-128, 127, 0, -50}};
std::array<int, 4> int2ScharBad{{-129, 128, 255, 10000}};
std::array<int, 4> int2UcharGood{{0, 1, 254, 255}};
std::array<int, 4> int2UcharBad{{-128, -1000, 256, -1}};

std::array<long long, 4> ll2SintOrFloatGood{{-2, -1, 0, 1}};
std::array<long long, 4> ll2SintOrFloatBad{{
    std::numeric_limits<long long>::min() / 5,
    std::numeric_limits<long long>::min() / 2,
    std::numeric_limits<long long>::max() / 2,
    std::numeric_limits<long long>::max() / 5,
}};
std::array<long long, 4> ll2UintGood{{1, 2, 3, 4}};
std::array<long long, 4> ll2UintBad{{-1, -2, -3, -4}};

std::array<double, 4> double2FloatGood{{1.0, 1.25, 2.5, 1000.0}};
std::array<double, 4> double2FloatBad{{1e100, 1e101, 1e102, 1e103}};
std::array<double, 4> double2IntGood{{1.0, 10.0, 100.0, 1000.0}};
std::array<double, 4> double2IntBad{{1e100, 1.25, 2.5, 100.00001}};
}

using namespace conv;

#define STRING_TO_TYPE_BENCHMARK(type, name, pass, fail) \
  BENCHMARK(stringTo##name##Classic, n) {                \
    stringToTypeClassic<type>(pass, n);                  \
  }                                                      \
  BENCHMARK(stringTo##name##ClassicError, n) {           \
    stringToTypeClassic<type>(fail, n);                  \
  }

#define PTR_PAIR_TO_INT_BENCHMARK(type, name, pass, fail) \
  BENCHMARK(ptrPairTo##name##Classic, n) {                \
    ptrPairToIntClassic<type>(pass, n);                   \
  }                                                       \
  BENCHMARK(ptrPairTo##name##ClassicError, n) {           \
    ptrPairToIntClassic<type>(fail, n);                   \
  }

#define ARITH_TO_ARITH_BENCHMARK(type, name, pass, fail)        \
  BENCHMARK_MULTI(name##Classic) {                              \
    return arithToArithClassic<type>(pass.data(), pass.size()); \
  }                                                             \
  BENCHMARK_MULTI(name##ClassicError) {                         \
    return arithToArithClassic<type>(fail.data(), fail.size()); \
  }

#define INT_TO_ARITH_BENCHMARK(type, name, pass, fail) \
  ARITH_TO_ARITH_BENCHMARK(type, intTo##name, pass, fail)

#define FLOAT_TO_ARITH_BENCHMARK(type, name, pass, fail) \
  ARITH_TO_ARITH_BENCHMARK(type, floatTo##name, pass, fail)

STRING_TO_TYPE_BENCHMARK(bool, BoolNum, " 1 ", "2")
STRING_TO_TYPE_BENCHMARK(bool, BoolStr, "true", "xxxx")
BENCHMARK_DRAW_LINE();
STRING_TO_TYPE_BENCHMARK(float, FloatNum, " 3.14 ", "3e5000x")
STRING_TO_TYPE_BENCHMARK(float, FloatStr, "-infinity", "xxxx")
STRING_TO_TYPE_BENCHMARK(double, DoubleNum, " 3.14 ", "3e5000x")
STRING_TO_TYPE_BENCHMARK(double, DoubleStr, "-infinity", "xxxx")
BENCHMARK_DRAW_LINE();
STRING_TO_TYPE_BENCHMARK(signed char, CharSigned, " -47 ", "1000")
STRING_TO_TYPE_BENCHMARK(unsigned char, CharUnsigned, " 47 ", "-47")
STRING_TO_TYPE_BENCHMARK(int, IntSigned, " -4711 ", "-10000000000000000000000")
STRING_TO_TYPE_BENCHMARK(unsigned int, IntUnsigned, " 4711 ", "-4711")
STRING_TO_TYPE_BENCHMARK(
    long long,
    LongLongSigned,
    " -8123456789123456789 ",
    "-10000000000000000000000")
STRING_TO_TYPE_BENCHMARK(
    unsigned long long,
    LongLongUnsigned,
    " 18123456789123456789 ",
    "-4711")
BENCHMARK_DRAW_LINE();

PTR_PAIR_TO_INT_BENCHMARK(signed char, CharSigned, "-47", "1000")
PTR_PAIR_TO_INT_BENCHMARK(unsigned char, CharUnsigned, "47", "1000")
PTR_PAIR_TO_INT_BENCHMARK(int, IntSigned, "-4711", "-10000000000000000000000")
PTR_PAIR_TO_INT_BENCHMARK(
    unsigned int,
    IntUnsigned,
    "4711",
    "10000000000000000000000")
PTR_PAIR_TO_INT_BENCHMARK(
    long long,
    LongLongSigned,
    "-8123456789123456789",
    "-10000000000000000000000")
PTR_PAIR_TO_INT_BENCHMARK(
    unsigned long long,
    LongLongUnsigned,
    "18123456789123456789",
    "20000000000000000000")
BENCHMARK_DRAW_LINE();

INT_TO_ARITH_BENCHMARK(signed char, CharSigned, int2ScharGood, int2ScharBad)
INT_TO_ARITH_BENCHMARK(unsigned char, CharUnsigned, int2UcharGood, int2UcharBad)
INT_TO_ARITH_BENCHMARK(int, IntSigned, ll2SintOrFloatGood, ll2SintOrFloatBad)
INT_TO_ARITH_BENCHMARK(unsigned int, IntUnsigned, ll2UintGood, ll2UintBad)
BENCHMARK_DRAW_LINE();
INT_TO_ARITH_BENCHMARK(float, Float, ll2SintOrFloatGood, ll2SintOrFloatBad)
BENCHMARK_DRAW_LINE();
FLOAT_TO_ARITH_BENCHMARK(float, Float, double2FloatGood, double2FloatBad)
BENCHMARK_DRAW_LINE();
FLOAT_TO_ARITH_BENCHMARK(int, Int, double2IntGood, double2IntBad)

#undef STRING_TO_TYPE_BENCHMARK
#undef PTR_PAIR_TO_INT_BENCHMARK
#undef ARITH_TO_ARITH_BENCHMARK
#undef INT_TO_ARITH_BENCHMARK
#undef FLOAT_TO_ARITH_BENCHMARK

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
