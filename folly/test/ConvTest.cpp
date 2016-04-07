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
#include <folly/Foreach.h>
#include <boost/lexical_cast.hpp>
#include <gtest/gtest.h>
#include <limits>
#include <stdexcept>

using namespace std;
using namespace folly;


TEST(Conv, digits10) {
  char buffer[100];
  uint64_t power;

  // first, some basic sniff tests
  EXPECT_EQ( 1, digits10(0));
  EXPECT_EQ( 1, digits10(1));
  EXPECT_EQ( 1, digits10(9));
  EXPECT_EQ( 2, digits10(10));
  EXPECT_EQ( 2, digits10(99));
  EXPECT_EQ( 3, digits10(100));
  EXPECT_EQ( 3, digits10(999));
  EXPECT_EQ( 4, digits10(1000));
  EXPECT_EQ( 4, digits10(9999));
  EXPECT_EQ(20, digits10(18446744073709551615ULL));

  // try the first X nonnegatives.
  // Covers some more cases of 2^p, 10^p
  for (uint64_t i = 0; i < 100000; i++) {
    snprintf(buffer, sizeof(buffer), "%" PRIu64, i);
    EXPECT_EQ(strlen(buffer), digits10(i));
  }

  // try powers of 2
  power = 1;
  for (int p = 0; p < 64; p++) {
    snprintf(buffer, sizeof(buffer), "%" PRIu64, power);
    EXPECT_EQ(strlen(buffer), digits10(power));
    snprintf(buffer, sizeof(buffer), "%" PRIu64, power - 1);
    EXPECT_EQ(strlen(buffer), digits10(power - 1));
    snprintf(buffer, sizeof(buffer), "%" PRIu64, power + 1);
    EXPECT_EQ(strlen(buffer), digits10(power + 1));
    power *= 2;
  }

  // try powers of 10
  power = 1;
  for (int p = 0; p < 20; p++) {
    snprintf(buffer, sizeof(buffer), "%" PRIu64, power);
    EXPECT_EQ(strlen(buffer), digits10(power));
    snprintf(buffer, sizeof(buffer), "%" PRIu64, power - 1);
    EXPECT_EQ(strlen(buffer), digits10(power - 1));
    snprintf(buffer, sizeof(buffer), "%" PRIu64, power + 1);
    EXPECT_EQ(strlen(buffer), digits10(power + 1));
    power *= 10;
  }
}

// Test to<T>(T)
TEST(Conv, Type2Type) {
  bool boolV = true;
  EXPECT_EQ(to<bool>(boolV), true);

  int intV = 42;
  EXPECT_EQ(to<int>(intV), 42);

  float floatV = 4.2;
  EXPECT_EQ(to<float>(floatV), 4.2f);

  double doubleV = 0.42;
  EXPECT_EQ(to<double>(doubleV), 0.42);

  std::string stringV = "StdString";
  EXPECT_EQ(to<std::string>(stringV), "StdString");

  folly::fbstring fbStrV = "FBString";
  EXPECT_EQ(to<folly::fbstring>(fbStrV), "FBString");

  folly::StringPiece spV("StringPiece");
  EXPECT_EQ(to<folly::StringPiece>(spV), "StringPiece");

  // Rvalues
  EXPECT_EQ(to<bool>(true), true);
  EXPECT_EQ(to<int>(42), 42);
  EXPECT_EQ(to<float>(4.2f), 4.2f);
  EXPECT_EQ(to<double>(.42), .42);
  EXPECT_EQ(to<std::string>(std::string("Hello")), "Hello");
  EXPECT_EQ(to<folly::fbstring>(folly::fbstring("hello")), "hello");
  EXPECT_EQ(to<folly::StringPiece>(folly::StringPiece("Forty Two")),
            "Forty Two");
}

TEST(Conv, Integral2Integral) {
  // Same size, different signs
  int64_t s64 = numeric_limits<uint8_t>::max();
  EXPECT_EQ(to<uint8_t>(s64), s64);

  s64 = numeric_limits<int8_t>::max();
  EXPECT_EQ(to<int8_t>(s64), s64);
}

TEST(Conv, Floating2Floating) {
  float f1 = 1e3;
  double d1 = to<double>(f1);
  EXPECT_EQ(f1, d1);

  double d2 = 23.0;
  auto f2 = to<float>(d2);
  EXPECT_EQ(double(f2), d2);

  double invalidFloat = std::numeric_limits<double>::max();
  EXPECT_ANY_THROW(to<float>(invalidFloat));
  invalidFloat = -std::numeric_limits<double>::max();
  EXPECT_ANY_THROW(to<float>(invalidFloat));

  try {
    auto shouldWork = to<float>(std::numeric_limits<double>::min());
    // The value of `shouldWork' is an implementation defined choice
    // between the following two alternatives.
    EXPECT_TRUE(shouldWork == std::numeric_limits<float>::min() ||
                shouldWork == 0.f);
  } catch (...) {
    EXPECT_TRUE(false);
  }
}

template <class String>
void testIntegral2String() {
}

template <class String, class Int, class... Ints>
void testIntegral2String() {
  typedef typename make_unsigned<Int>::type Uint;
  typedef typename make_signed<Int>::type Sint;

  Uint value = 123;
  EXPECT_EQ(to<String>(value), "123");
  Sint svalue = 123;
  EXPECT_EQ(to<String>(svalue), "123");
  svalue = -123;
  EXPECT_EQ(to<String>(svalue), "-123");

  value = numeric_limits<Uint>::min();
  EXPECT_EQ(to<Uint>(to<String>(value)), value);
  value = numeric_limits<Uint>::max();
  EXPECT_EQ(to<Uint>(to<String>(value)), value);

  svalue = numeric_limits<Sint>::min();
  EXPECT_EQ(to<Sint>(to<String>(svalue)), svalue);
  value = numeric_limits<Sint>::max();
  EXPECT_EQ(to<Sint>(to<String>(svalue)), svalue);

  testIntegral2String<String, Ints...>();
}

#if FOLLY_HAVE_INT128_T
template <class String>
void test128Bit2String() {
  typedef unsigned __int128 Uint;
  typedef __int128 Sint;

  EXPECT_EQ(detail::digitsEnough<unsigned __int128>(), 39);

  Uint value = 123;
  EXPECT_EQ(to<String>(value), "123");
  Sint svalue = 123;
  EXPECT_EQ(to<String>(svalue), "123");
  svalue = -123;
  EXPECT_EQ(to<String>(svalue), "-123");

  value = __int128(1) << 64;
  EXPECT_EQ(to<String>(value), "18446744073709551616");

  svalue =  -(__int128(1) << 64);
  EXPECT_EQ(to<String>(svalue), "-18446744073709551616");

  value = 0;
  EXPECT_EQ(to<String>(value), "0");

  svalue = 0;
  EXPECT_EQ(to<String>(svalue), "0");

  // TODO: the following do not compile to<__int128> ...

#if 0
  value = numeric_limits<Uint>::min();
  EXPECT_EQ(to<Uint>(to<String>(value)), value);
  value = numeric_limits<Uint>::max();
  EXPECT_EQ(to<Uint>(to<String>(value)), value);

  svalue = numeric_limits<Sint>::min();
  EXPECT_EQ(to<Sint>(to<String>(svalue)), svalue);
  value = numeric_limits<Sint>::max();
  EXPECT_EQ(to<Sint>(to<String>(svalue)), svalue);
#endif
}

#endif

TEST(Conv, Integral2String) {
  testIntegral2String<std::string, char, short, int, long>();
  testIntegral2String<fbstring, char, short, int, long>();

#if FOLLY_HAVE_INT128_T
  test128Bit2String<std::string>();
  test128Bit2String<fbstring>();
#endif
}

template <class String>
void testString2Integral() {
}

template <class String, class Int, class... Ints>
void testString2Integral() {
  typedef typename make_unsigned<Int>::type Uint;
  typedef typename make_signed<Int>::type Sint;

  // Unsigned numbers small enough to fit in a signed type
  static const String strings[] = {
    "0",
    "00",
    "2 ",
    " 84",
    " \n 123    \t\n",
    " 127",
    "0000000000000000000000000042"
  };
  static const Uint values[] = {
    0,
    0,
    2,
    84,
    123,
    127,
    42
  };
  FOR_EACH_RANGE (i, 0, sizeof(strings) / sizeof(*strings)) {
    EXPECT_EQ(to<Uint>(strings[i]), values[i]);
    EXPECT_EQ(to<Sint>(strings[i]), values[i]);
  }

  // Unsigned numbers that won't fit in the signed variation
  static const String uStrings[] = {
    " 128",
    "213",
    "255"
  };
  static const Uint uValues[] = {
    128,
    213,
    255
  };
  FOR_EACH_RANGE (i, 0, sizeof(uStrings)/sizeof(*uStrings)) {
    EXPECT_EQ(to<Uint>(uStrings[i]), uValues[i]);
    if (sizeof(Int) == 1) {
      EXPECT_THROW(to<Sint>(uStrings[i]), std::range_error);
    }
  }

  if (sizeof(Int) >= 4) {
    static const String strings2[] = {
      "256",
      "6324 ",
      "63245675 ",
      "2147483647"
    };
    static const Uint values2[] = {
      (Uint)256,
      (Uint)6324,
      (Uint)63245675,
      (Uint)2147483647
    };
    FOR_EACH_RANGE (i, 0, sizeof(strings2)/sizeof(*strings2)) {
      EXPECT_EQ(to<Uint>(strings2[i]), values2[i]);
      EXPECT_EQ(to<Sint>(strings2[i]), values2[i]);
    }

    static const String uStrings2[] = {
      "2147483648",
      "3147483648",
      "4147483648",
      "4000000000",
    };
    static const Uint uValues2[] = {
      (Uint)2147483648U,
      (Uint)3147483648U,
      (Uint)4147483648U,
      (Uint)4000000000U,
    };
    FOR_EACH_RANGE (i, 0, sizeof(uStrings2)/sizeof(uStrings2)) {
      EXPECT_EQ(to<Uint>(uStrings2[i]), uValues2[i]);
      if (sizeof(Int) == 4) {
        EXPECT_THROW(to<Sint>(uStrings2[i]), std::range_error);
      }
    }
  }

  if (sizeof(Int) >= 8) {
    static_assert(sizeof(Int) <= 8, "Now that would be interesting");
    static const String strings3[] = {
      "2147483648",
      "5000000001",
      "25687346509278435",
      "100000000000000000",
      "9223372036854775807",
    };
    static const Uint values3[] = {
      (Uint)2147483648ULL,
      (Uint)5000000001ULL,
      (Uint)25687346509278435ULL,
      (Uint)100000000000000000ULL,
      (Uint)9223372036854775807ULL,
    };
    FOR_EACH_RANGE (i, 0, sizeof(strings3)/sizeof(*strings3)) {
      EXPECT_EQ(to<Uint>(strings3[i]), values3[i]);
      EXPECT_EQ(to<Sint>(strings3[i]), values3[i]);
    }

    static const String uStrings3[] = {
      "9223372036854775808",
      "9987435987394857987",
      "17873648761234698740",
      "18446744073709551615",
    };
    static const Uint uValues3[] = {
      (Uint)9223372036854775808ULL,
      (Uint)9987435987394857987ULL,
      (Uint)17873648761234698740ULL,
      (Uint)18446744073709551615ULL,
    };
    FOR_EACH_RANGE (i, 0, sizeof(uStrings3)/sizeof(*uStrings3)) {
      EXPECT_EQ(to<Uint>(uStrings3[i]), uValues3[i]);
      if (sizeof(Int) == 8) {
        EXPECT_THROW(to<Sint>(uStrings3[i]), std::range_error);
      }
    }
  }

  // Minimum possible negative values, and negative sign overflow
  static const String strings4[] = {
    "-128",
    "-32768",
    "-2147483648",
    "-9223372036854775808",
  };
  static const String strings5[] = {
    "-129",
    "-32769",
    "-2147483649",
    "-9223372036854775809",
  };
  static const Sint values4[] = {
    (Sint)-128LL,
    (Sint)-32768LL,
    (Sint)-2147483648LL,
    (Sint)(-9223372036854775807LL - 1),
  };
  FOR_EACH_RANGE (i, 0, sizeof(strings4)/sizeof(*strings4)) {
    if (sizeof(Int) > std::pow(2, i)) {
      EXPECT_EQ(values4[i], to<Sint>(strings4[i]));
      EXPECT_EQ(values4[i] - 1, to<Sint>(strings5[i]));
    } else if (sizeof(Int) == std::pow(2, i)) {
      EXPECT_EQ(values4[i], to<Sint>(strings4[i]));
      EXPECT_THROW(to<Sint>(strings5[i]), std::range_error);
    } else {
      EXPECT_THROW(to<Sint>(strings4[i]), std::range_error);
      EXPECT_THROW(to<Sint>(strings5[i]), std::range_error);
    }
  }

  // Bogus string values
  static const String bogusStrings[] = {
    "",
    "0x1234",
    "123L",
    "123a",
    "x 123 ",
    "234 y",
    "- 42",  // whitespace is not allowed between the sign and the value
    " +   13 ",
    "12345678901234567890123456789",
  };
  for (const auto& str : bogusStrings) {
    EXPECT_THROW(to<Sint>(str), std::range_error);
    EXPECT_THROW(to<Uint>(str), std::range_error);
  }

  // A leading '+' character is only allowed when converting to signed types.
  String posSign("+42");
  EXPECT_EQ(42, to<Sint>(posSign));
  EXPECT_THROW(to<Uint>(posSign), std::range_error);

  testString2Integral<String, Ints...>();
}

TEST(Conv, String2Integral) {
  testString2Integral<const char*, signed char, short, int, long, long long>();
  testString2Integral<std::string, signed char, short, int, long, long long>();
  testString2Integral<fbstring, signed char, short, int, long, long long>();

  // Testing the behavior of the StringPiece* API
  // StringPiece* normally parses as much valid data as it can,
  // and advances the StringPiece to the end of the valid data.
  char buf1[] = "100foo";
  StringPiece sp1(buf1);
  EXPECT_EQ(100, to<uint8_t>(&sp1));
  EXPECT_EQ(buf1 + 3, sp1.begin());
  // However, if the next character would cause an overflow it throws a
  // range_error rather than consuming only as much as it can without
  // overflowing.
  char buf2[] = "1002";
  StringPiece sp2(buf2);
  EXPECT_THROW(to<uint8_t>(&sp2), std::range_error);
  EXPECT_EQ(buf2, sp2.begin());
}

TEST(Conv, StringPiece2Integral) {
  string s = "  +123  hello world  ";
  StringPiece sp = s;
  EXPECT_EQ(to<int>(&sp), 123);
  EXPECT_EQ(sp, "  hello world  ");
}

TEST(Conv, StringPieceAppend) {
  string s = "foobar";
  {
    StringPiece sp(s, 0, 3);
    string result = to<string>(s, sp);
    EXPECT_EQ(result, "foobarfoo");
  }
  {
    StringPiece sp1(s, 0, 3);
    StringPiece sp2(s, 3, 3);
    string result = to<string>(sp1, sp2);
    EXPECT_EQ(result, s);
  }
}

TEST(Conv, BadStringToIntegral) {
  // Note that leading spaces (e.g.  " 1") are valid.
  vector<string> v = { "a", "", " ", "\n", " a0", "abcdef", "1Z", "!#" };
  for (auto& s: v) {
    EXPECT_THROW(to<int>(s), std::range_error) << "s=" << s;
  }
}

template <class String>
void testIdenticalTo() {
  String s("Yukkuri shiteitte ne!!!");

  String result = to<String>(s);
  EXPECT_EQ(result, s);
}

template <class String>
void testVariadicTo() {
  String s;
  toAppend(&s);
  toAppend("Lorem ipsum ", 1234, String(" dolor amet "), 567.89, '!', &s);
  EXPECT_EQ(s, "Lorem ipsum 1234 dolor amet 567.89!");

  s = to<String>();
  EXPECT_TRUE(s.empty());

  s = to<String>("Lorem ipsum ", nullptr, 1234, " dolor amet ", 567.89, '.');
  EXPECT_EQ(s, "Lorem ipsum 1234 dolor amet 567.89.");
}

template <class String>
void testIdenticalToDelim() {
  String s("Yukkuri shiteitte ne!!!");

  String charDelim = toDelim<String>('$', s);
  EXPECT_EQ(charDelim, s);

  String strDelim = toDelim<String>(String(">_<"), s);
  EXPECT_EQ(strDelim, s);
}

template <class String>
void testVariadicToDelim() {
  String s;
  toAppendDelim(":", &s);
  toAppendDelim(
      ":", "Lorem ipsum ", 1234, String(" dolor amet "), 567.89, '!', &s);
  EXPECT_EQ(s, "Lorem ipsum :1234: dolor amet :567.89:!");

  s = toDelim<String>(':');
  EXPECT_TRUE(s.empty());

  s = toDelim<String>(
      ":", "Lorem ipsum ", nullptr, 1234, " dolor amet ", 567.89, '.');
  EXPECT_EQ(s, "Lorem ipsum ::1234: dolor amet :567.89:.");
}

TEST(Conv, NullString) {
  string s1 = to<string>((char *) nullptr);
  EXPECT_TRUE(s1.empty());
  fbstring s2 = to<fbstring>((char *) nullptr);
  EXPECT_TRUE(s2.empty());
}

TEST(Conv, VariadicTo) {
  testIdenticalTo<string>();
  testIdenticalTo<fbstring>();
  testVariadicTo<string>();
  testVariadicTo<fbstring>();
}

TEST(Conv, VariadicToDelim) {
  testIdenticalToDelim<string>();
  testIdenticalToDelim<fbstring>();
  testVariadicToDelim<string>();
  testVariadicToDelim<fbstring>();
}

template <class String>
void testDoubleToString() {
  EXPECT_EQ(to<string>(0.0), "0");
  EXPECT_EQ(to<string>(0.5), "0.5");
  EXPECT_EQ(to<string>(10.25), "10.25");
  EXPECT_EQ(to<string>(1.123e10), "11230000000");
}

TEST(Conv, DoubleToString) {
  testDoubleToString<string>();
  testDoubleToString<fbstring>();
}

TEST(Conv, FBStringToString) {
  fbstring foo("foo");
  string ret = to<string>(foo);
  EXPECT_EQ(ret, "foo");
  string ret2 = to<string>(foo, 2);
  EXPECT_EQ(ret2, "foo2");
}

TEST(Conv, StringPieceToDouble) {
  string s = "2134123.125 zorro";
  StringPiece pc(s);
  EXPECT_EQ(to<double>(&pc), 2134123.125);
  EXPECT_EQ(pc, " zorro");

  EXPECT_THROW(to<double>(StringPiece(s)), std::range_error);
  EXPECT_EQ(to<double>(StringPiece(s.data(), pc.data())), 2134123.125);

// Test NaN conversion
  try {
    to<double>("not a number");
    EXPECT_TRUE(false);
  } catch (const std::range_error &) {
  }

  EXPECT_TRUE(std::isnan(to<double>("NaN")));
  EXPECT_EQ(to<double>("inf"), numeric_limits<double>::infinity());
  EXPECT_EQ(to<double>("infinity"), numeric_limits<double>::infinity());
  EXPECT_THROW(to<double>("infinitX"), std::range_error);
  EXPECT_EQ(to<double>("-inf"), -numeric_limits<double>::infinity());
  EXPECT_EQ(to<double>("-infinity"), -numeric_limits<double>::infinity());
  EXPECT_THROW(to<double>("-infinitX"), std::range_error);
}

TEST(Conv, EmptyStringToInt) {
  string s = "";
  StringPiece pc(s);

  try {
    to<int>(pc);
    EXPECT_TRUE(false);
  } catch (const std::range_error &) {
  }
}

TEST(Conv, CorruptedStringToInt) {
  string s = "-1";
  StringPiece pc(s.data(), s.data() + 1); // Only  "-"

  try {
    to<int64_t>(&pc);
    EXPECT_TRUE(false);
  } catch (const std::range_error &) {
  }
}

TEST(Conv, EmptyStringToDouble) {
  string s = "";
  StringPiece pc(s);

  try {
    to<double>(pc);
    EXPECT_TRUE(false);
  } catch (const std::range_error &) {
  }
}

TEST(Conv, IntToDouble) {
  auto d = to<double>(42);
  EXPECT_EQ(d, 42);
  /* This seems not work in ubuntu11.10, gcc 4.6.1
  try {
    auto f = to<float>(957837589847);
    EXPECT_TRUE(false);
  } catch (std::range_error& e) {
    //LOG(INFO) << e.what();
  }
  */
}

TEST(Conv, DoubleToInt) {
  auto i = to<int>(42.0);
  EXPECT_EQ(i, 42);
  try {
    auto i = to<int>(42.1);
    LOG(ERROR) << "to<int> returned " << i << " instead of throwing";
    EXPECT_TRUE(false);
  } catch (std::range_error& e) {
    //LOG(INFO) << e.what();
  }
}

TEST(Conv, EnumToInt) {
  enum A { x = 42, y = 420, z = 65 };
  auto i = to<int>(x);
  EXPECT_EQ(i, 42);
  auto j = to<char>(x);
  EXPECT_EQ(j, 42);
  try {
    auto i = to<char>(y);
    LOG(ERROR) << "to<char> returned "
               << static_cast<unsigned int>(i)
               << " instead of throwing";
    EXPECT_TRUE(false);
  } catch (std::range_error& e) {
    //LOG(INFO) << e.what();
  }
}

TEST(Conv, EnumToString) {
  // task 813959
  enum A { x = 4, y = 420, z = 65 };
  EXPECT_EQ("foo.4", to<string>("foo.", x));
  EXPECT_EQ("foo.420", to<string>("foo.", y));
  EXPECT_EQ("foo.65", to<string>("foo.", z));
}

TEST(Conv, IntToEnum) {
  enum A { x = 42, y = 420 };
  auto i = to<A>(42);
  EXPECT_EQ(i, x);
  auto j = to<A>(100);
  EXPECT_EQ(j, 100);
  try {
    auto i = to<A>(5000000000L);
    LOG(ERROR) << "to<A> returned "
               << static_cast<unsigned int>(i)
               << " instead of throwing";
    EXPECT_TRUE(false);
  } catch (std::range_error& e) {
    //LOG(INFO) << e.what();
  }
}

TEST(Conv, UnsignedEnum) {
  enum E : uint32_t { x = 3000000000U };
  auto u = to<uint32_t>(x);
  EXPECT_EQ(u, 3000000000U);
  auto s = to<string>(x);
  EXPECT_EQ("3000000000", s);
  auto e = to<E>(3000000000U);
  EXPECT_EQ(e, x);
  try {
    auto i = to<int32_t>(x);
    LOG(ERROR) << "to<int32_t> returned " << i << " instead of throwing";
    EXPECT_TRUE(false);
  } catch (std::range_error& e) {
  }
}

TEST(Conv, UnsignedEnumClass) {
  enum class E : uint32_t { x = 3000000000U };
  auto u = to<uint32_t>(E::x);
  EXPECT_GT(u, 0);
  EXPECT_EQ(u, 3000000000U);
  EXPECT_EQ("3000000000", to<string>(E::x));
  EXPECT_EQ(E::x, to<E>(3000000000U));
  EXPECT_EQ(E::x, to<E>("3000000000"));
  E e;
  parseTo("3000000000", e);
  EXPECT_EQ(E::x, e);
  EXPECT_THROW(to<int32_t>(E::x), std::range_error);
}

// Multi-argument to<string> uses toAppend, a different code path than
// to<string>(enum).
TEST(Conv, EnumClassToString) {
  enum class A { x = 4, y = 420, z = 65 };
  EXPECT_EQ("foo.4", to<string>("foo.", A::x));
  EXPECT_EQ("foo.420", to<string>("foo.", A::y));
  EXPECT_EQ("foo.65", to<string>("foo.", A::z));
}

TEST(Conv, IntegralToBool) {
  EXPECT_FALSE(to<bool>(0));
  EXPECT_FALSE(to<bool>(0ul));

  EXPECT_TRUE(to<bool>(1));
  EXPECT_TRUE(to<bool>(1ul));

  EXPECT_TRUE(to<bool>(-42));
  EXPECT_TRUE(to<bool>(42ul));
}

template<typename Src>
void testStr2Bool() {
  EXPECT_FALSE(to<bool>(Src("0")));
  EXPECT_FALSE(to<bool>(Src("  000  ")));

  EXPECT_FALSE(to<bool>(Src("n")));
  EXPECT_FALSE(to<bool>(Src("no")));
  EXPECT_FALSE(to<bool>(Src("false")));
  EXPECT_FALSE(to<bool>(Src("False")));
  EXPECT_FALSE(to<bool>(Src("  fAlSe"  )));
  EXPECT_FALSE(to<bool>(Src("F")));
  EXPECT_FALSE(to<bool>(Src("off")));

  EXPECT_TRUE(to<bool>(Src("1")));
  EXPECT_TRUE(to<bool>(Src("  001 ")));
  EXPECT_TRUE(to<bool>(Src("y")));
  EXPECT_TRUE(to<bool>(Src("yes")));
  EXPECT_TRUE(to<bool>(Src("\nyEs\t")));
  EXPECT_TRUE(to<bool>(Src("true")));
  EXPECT_TRUE(to<bool>(Src("True")));
  EXPECT_TRUE(to<bool>(Src("T")));
  EXPECT_TRUE(to<bool>(Src("on")));

  EXPECT_THROW(to<bool>(Src("")), std::range_error);
  EXPECT_THROW(to<bool>(Src("2")), std::range_error);
  EXPECT_THROW(to<bool>(Src("11")), std::range_error);
  EXPECT_THROW(to<bool>(Src("19")), std::range_error);
  EXPECT_THROW(to<bool>(Src("o")), std::range_error);
  EXPECT_THROW(to<bool>(Src("fal")), std::range_error);
  EXPECT_THROW(to<bool>(Src("tru")), std::range_error);
  EXPECT_THROW(to<bool>(Src("ye")), std::range_error);
  EXPECT_THROW(to<bool>(Src("yes foo")), std::range_error);
  EXPECT_THROW(to<bool>(Src("bar no")), std::range_error);
  EXPECT_THROW(to<bool>(Src("one")), std::range_error);
  EXPECT_THROW(to<bool>(Src("true_")), std::range_error);
  EXPECT_THROW(to<bool>(Src("bogus_token_that_is_too_long")),
               std::range_error);
}

TEST(Conv, StringToBool) {
  // testStr2Bool<const char *>();
  testStr2Bool<std::string>();

  // Test with strings that are not NUL terminated.
  const char buf[] = "01234";
  EXPECT_FALSE(to<bool>(StringPiece(buf, buf + 1)));  // "0"
  EXPECT_TRUE(to<bool>(StringPiece(buf + 1, buf + 2)));  // "1"
  const char buf2[] = "one two three";
  EXPECT_TRUE(to<bool>(StringPiece(buf2, buf2 + 2)));  // "on"
  const char buf3[] = "false";
  EXPECT_THROW(to<bool>(StringPiece(buf3, buf3 + 3)),  // "fal"
               std::range_error);

  // Test the StringPiece* API
  const char buf4[] = "001foo";
  StringPiece sp4(buf4);
  EXPECT_TRUE(to<bool>(&sp4));
  EXPECT_EQ(buf4 + 3, sp4.begin());
  const char buf5[] = "0012";
  StringPiece sp5(buf5);
  EXPECT_THROW(to<bool>(&sp5), std::range_error);
  EXPECT_EQ(buf5, sp5.begin());
}

TEST(Conv, NewUint64ToString) {
  char buf[21];

#define THE_GREAT_EXPECTATIONS(n, len)                  \
  do {                                                  \
    EXPECT_EQ((len), uint64ToBufferUnsafe((n), buf));   \
    buf[(len)] = 0;                                     \
    auto s = string(#n);                                \
    s = s.substr(0, s.size() - 2);                      \
    EXPECT_EQ(s, buf);                                  \
  } while (0)

  THE_GREAT_EXPECTATIONS(0UL, 1);
  THE_GREAT_EXPECTATIONS(1UL, 1);
  THE_GREAT_EXPECTATIONS(12UL, 2);
  THE_GREAT_EXPECTATIONS(123UL, 3);
  THE_GREAT_EXPECTATIONS(1234UL, 4);
  THE_GREAT_EXPECTATIONS(12345UL, 5);
  THE_GREAT_EXPECTATIONS(123456UL, 6);
  THE_GREAT_EXPECTATIONS(1234567UL, 7);
  THE_GREAT_EXPECTATIONS(12345678UL, 8);
  THE_GREAT_EXPECTATIONS(123456789UL, 9);
  THE_GREAT_EXPECTATIONS(1234567890UL, 10);
  THE_GREAT_EXPECTATIONS(12345678901UL, 11);
  THE_GREAT_EXPECTATIONS(123456789012UL, 12);
  THE_GREAT_EXPECTATIONS(1234567890123UL, 13);
  THE_GREAT_EXPECTATIONS(12345678901234UL, 14);
  THE_GREAT_EXPECTATIONS(123456789012345UL, 15);
  THE_GREAT_EXPECTATIONS(1234567890123456UL, 16);
  THE_GREAT_EXPECTATIONS(12345678901234567UL, 17);
  THE_GREAT_EXPECTATIONS(123456789012345678UL, 18);
  THE_GREAT_EXPECTATIONS(1234567890123456789UL, 19);
  THE_GREAT_EXPECTATIONS(18446744073709551614UL, 20);
  THE_GREAT_EXPECTATIONS(18446744073709551615UL, 20);

#undef THE_GREAT_EXPECTATIONS
}

TEST(Conv, allocate_size) {
  std::string str1 = "meh meh meh";
  std::string str2 = "zdech zdech zdech";

  auto res1 = folly::to<std::string>(str1, ".", str2);
  EXPECT_EQ(res1, str1 + "." + str2);

  std::string res2; //empty
  toAppendFit(str1, str2, 1, &res2);
  EXPECT_EQ(res2, str1 + str2 + "1");

  std::string res3;
  toAppendDelimFit(",", str1, str2, &res3);
  EXPECT_EQ(res3, str1 + "," + str2);
}

namespace my {
struct Dimensions {
  int w, h;
  std::tuple<const int&, const int&> tuple_view() const {
    return tie(w, h);
  }
  bool operator==(const Dimensions& other) const {
    return this->tuple_view() == other.tuple_view();
  }
};

void parseTo(folly::StringPiece in, Dimensions& out) {
  out.w = folly::to<int>(&in);
  in.removePrefix("x");
  out.h = folly::to<int>(&in);
}

template <class String>
void toAppend(const Dimensions& in, String* result) {
  folly::toAppend(in.w, 'x', in.h, result);
}

size_t estimateSpaceNeeded(const Dimensions&in) {
  return 2000 + folly::estimateSpaceNeeded(in.w) +
      folly::estimateSpaceNeeded(in.h);
}
}

TEST(Conv, custom_kkproviders) {
  my::Dimensions expected{7, 8};
  EXPECT_EQ(expected, folly::to<my::Dimensions>("7x8"));
  auto str = folly::to<std::string>(expected);
  EXPECT_EQ("7x8", str);
  // make sure above implementation of estimateSpaceNeeded() is used.
  EXPECT_GT(str.capacity(), 2000);
  EXPECT_LT(str.capacity(), 2500);
}
