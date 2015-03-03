/*
 * Copyright 2015 Facebook, Inc.
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

#include <folly/Format.h>

#include <glog/logging.h>
#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include <folly/FBVector.h>
#include <folly/FileUtil.h>
#include <folly/dynamic.h>
#include <folly/json.h>

#include <string>

using namespace folly;

template <class Uint>
void compareOctal(Uint u) {
  char buf1[detail::kMaxOctalLength + 1];
  buf1[detail::kMaxOctalLength] = '\0';
  char* p = buf1 + detail::uintToOctal(buf1, detail::kMaxOctalLength, u);

  char buf2[detail::kMaxOctalLength + 1];
  sprintf(buf2, "%jo", static_cast<uintmax_t>(u));

  EXPECT_EQ(std::string(buf2), std::string(p));
}

template <class Uint>
void compareHex(Uint u) {
  char buf1[detail::kMaxHexLength + 1];
  buf1[detail::kMaxHexLength] = '\0';
  char* p = buf1 + detail::uintToHexLower(buf1, detail::kMaxHexLength, u);

  char buf2[detail::kMaxHexLength + 1];
  sprintf(buf2, "%jx", static_cast<uintmax_t>(u));

  EXPECT_EQ(std::string(buf2), std::string(p));
}

template <class Uint>
void compareBinary(Uint u) {
  char buf[detail::kMaxBinaryLength + 1];
  buf[detail::kMaxBinaryLength] = '\0';
  char* p = buf + detail::uintToBinary(buf, detail::kMaxBinaryLength, u);

  std::string repr;
  if (u == 0) {
    repr = '0';
  } else {
    std::string tmp;
    for (; u; u >>= 1) {
      tmp.push_back(u & 1 ? '1' : '0');
    }
    repr.assign(tmp.rbegin(), tmp.rend());
  }

  EXPECT_EQ(repr, std::string(p));
}

TEST(Format, uintToOctal) {
  for (unsigned i = 0; i < (1u << 16) + 2; i++) {
    compareOctal(i);
  }
}

TEST(Format, uintToHex) {
  for (unsigned i = 0; i < (1u << 16) + 2; i++) {
    compareHex(i);
  }
}

TEST(Format, uintToBinary) {
  for (unsigned i = 0; i < (1u << 16) + 2; i++) {
    compareBinary(i);
  }
}

TEST(Format, Simple) {
  EXPECT_EQ("hello", sformat("hello"));
  EXPECT_EQ("42", sformat("{}", 42));
  EXPECT_EQ("42 42", sformat("{0} {0}", 42));
  EXPECT_EQ("00042  23   42", sformat("{0:05} {1:3} {0:4}", 42, 23));
  EXPECT_EQ("hello world hello 42",
            sformat("{0} {1} {0} {2}", "hello", "world", 42));
  EXPECT_EQ("XXhelloXX", sformat("{:X^9}", "hello"));
  EXPECT_EQ("XXX42XXXX", sformat("{:X^9}", 42));
  EXPECT_EQ("-0xYYYY2a", sformat("{:Y=#9x}", -42));
  EXPECT_EQ("*", sformat("{}", '*'));
  EXPECT_EQ("42", sformat("{}", 42));
  EXPECT_EQ("0042", sformat("{:04}", 42));

  EXPECT_EQ("hello  ", sformat("{:7}", "hello"));
  EXPECT_EQ("hello  ", sformat("{:<7}", "hello"));
  EXPECT_EQ("  hello", sformat("{:>7}", "hello"));

  std::vector<int> v1 {10, 20, 30};
  EXPECT_EQ("0020", sformat("{0[1]:04}", v1));
  EXPECT_EQ("0020", svformat("{1:04}", v1));
  EXPECT_EQ("10 20", svformat("{} {}", v1));

  const std::vector<int> v2 = v1;
  EXPECT_EQ("0020", sformat("{0[1]:04}", v2));
  EXPECT_EQ("0020", svformat("{1:04}", v2));
  EXPECT_THROW(sformat("{0[3]:04}", v2), std::out_of_range);
  EXPECT_THROW(svformat("{3:04}", v2), std::out_of_range);
  EXPECT_EQ("0020", sformat("{0[1]:04}", defaulted(v2, 42)));
  EXPECT_EQ("0020", svformat("{1:04}", defaulted(v2, 42)));
  EXPECT_EQ("0042", sformat("{0[3]:04}", defaulted(v2, 42)));
  EXPECT_EQ("0042", svformat("{3:04}", defaulted(v2, 42)));

  const int p[] = {10, 20, 30};
  const int* q = p;
  EXPECT_EQ("0020", sformat("{0[1]:04}", p));
  EXPECT_EQ("0020", svformat("{1:04}", p));
  EXPECT_EQ("0020", sformat("{0[1]:04}", q));
  EXPECT_EQ("0020", svformat("{1:04}", q));
  EXPECT_NE("", sformat("{}", q));

  EXPECT_EQ("0x", sformat("{}", p).substr(0, 2));
  EXPECT_EQ("10", svformat("{}", p));
  EXPECT_EQ("0x", sformat("{}", q).substr(0, 2));
  EXPECT_EQ("10", svformat("{}", q));
  q = nullptr;
  EXPECT_EQ("(null)", sformat("{}", q));

  std::map<int, std::string> m { {10, "hello"}, {20, "world"} };
  EXPECT_EQ("worldXX", sformat("{[20]:X<7}", m));
  EXPECT_EQ("worldXX", svformat("{20:X<7}", m));
  EXPECT_THROW(sformat("{[42]:X<7}", m), std::out_of_range);
  EXPECT_THROW(svformat("{42:X<7}", m), std::out_of_range);
  EXPECT_EQ("worldXX", sformat("{[20]:X<7}", defaulted(m, "meow")));
  EXPECT_EQ("worldXX", svformat("{20:X<7}", defaulted(m, "meow")));
  EXPECT_EQ("meowXXX", sformat("{[42]:X<7}", defaulted(m, "meow")));
  EXPECT_EQ("meowXXX", svformat("{42:X<7}", defaulted(m, "meow")));

  std::map<std::string, std::string> m2 { {"hello", "world"} };
  EXPECT_EQ("worldXX", sformat("{[hello]:X<7}", m2));
  EXPECT_EQ("worldXX", svformat("{hello:X<7}", m2));
  EXPECT_THROW(sformat("{[none]:X<7}", m2), std::out_of_range);
  EXPECT_THROW(svformat("{none:X<7}", m2), std::out_of_range);
  EXPECT_EQ("worldXX", sformat("{[hello]:X<7}", defaulted(m2, "meow")));
  EXPECT_EQ("worldXX", svformat("{hello:X<7}", defaulted(m2, "meow")));
  EXPECT_EQ("meowXXX", sformat("{[none]:X<7}", defaulted(m2, "meow")));
  EXPECT_EQ("meowXXX", svformat("{none:X<7}", defaulted(m2, "meow")));

  // Test indexing in strings
  EXPECT_EQ("61 62", sformat("{0[0]:x} {0[1]:x}", "abcde"));
  EXPECT_EQ("61 62", svformat("{0:x} {1:x}", "abcde"));
  EXPECT_EQ("61 62", sformat("{0[0]:x} {0[1]:x}", std::string("abcde")));
  EXPECT_EQ("61 62", svformat("{0:x} {1:x}", std::string("abcde")));

  // Test booleans
  EXPECT_EQ("true", sformat("{}", true));
  EXPECT_EQ("1", sformat("{:d}", true));
  EXPECT_EQ("false", sformat("{}", false));
  EXPECT_EQ("0", sformat("{:d}", false));

  // Test pairs
  {
    std::pair<int, std::string> p {42, "hello"};
    EXPECT_EQ("    42 hello ", sformat("{0[0]:6} {0[1]:6}", p));
    EXPECT_EQ("    42 hello ", svformat("{:6} {:6}", p));
  }

  // Test tuples
  {
    std::tuple<int, std::string, int> t { 42, "hello", 23 };
    EXPECT_EQ("    42 hello      23", sformat("{0[0]:6} {0[1]:6} {0[2]:6}", t));
    EXPECT_EQ("    42 hello      23", svformat("{:6} {:6} {:6}", t));
  }

  // Test writing to stream
  std::ostringstream os;
  os << format("{} {}", 42, 23);
  EXPECT_EQ("42 23", os.str());

  // Test appending to string
  std::string s;
  format(&s, "{} {}", 42, 23);
  format(&s, " hello {:X<7}", "world");
  EXPECT_EQ("42 23 hello worldXX", s);

  // Test writing to FILE. I'd use open_memstream but that's not available
  // outside of Linux (even though it's in POSIX.1-2008).
  {
    int fds[2];
    CHECK_ERR(pipe(fds));
    SCOPE_EXIT { closeNoInt(fds[1]); };
    {
      FILE* fp = fdopen(fds[1], "wb");
      PCHECK(fp);
      SCOPE_EXIT { fclose(fp); };
      writeTo(fp, format("{} {}", 42, 23));  // <= 512 bytes (PIPE_BUF)
    }

    char buf[512];
    ssize_t n = readFull(fds[0], buf, sizeof(buf));
    CHECK_GE(n, 0);

    EXPECT_EQ("42 23", std::string(buf, n));
  }
}

TEST(Format, Float) {
  double d = 1;
  EXPECT_EQ("1", sformat("{}", 1.0));
  EXPECT_EQ("0.1", sformat("{}", 0.1));
  EXPECT_EQ("0.01", sformat("{}", 0.01));
  EXPECT_EQ("0.001", sformat("{}", 0.001));
  EXPECT_EQ("0.0001", sformat("{}", 0.0001));
  EXPECT_EQ("1e-5", sformat("{}", 0.00001));
  EXPECT_EQ("1e-6", sformat("{}", 0.000001));

  EXPECT_EQ("10", sformat("{}", 10.0));
  EXPECT_EQ("100", sformat("{}", 100.0));
  EXPECT_EQ("1000", sformat("{}", 1000.0));
  EXPECT_EQ("10000", sformat("{}", 10000.0));
  EXPECT_EQ("100000", sformat("{}", 100000.0));
  EXPECT_EQ("1e+6", sformat("{}", 1000000.0));
  EXPECT_EQ("1e+7", sformat("{}", 10000000.0));

  EXPECT_EQ("1.00", sformat("{:.2f}", 1.0));
  EXPECT_EQ("0.10", sformat("{:.2f}", 0.1));
  EXPECT_EQ("0.01", sformat("{:.2f}", 0.01));
  EXPECT_EQ("0.00", sformat("{:.2f}", 0.001));

  EXPECT_EQ("100000. !== 100000", sformat("{:.} !== {:.}", 100000.0, 100000));
  EXPECT_EQ("100000.", sformat("{:.}", 100000.0));
  EXPECT_EQ("1e+6", sformat("{:.}", 1000000.0));
  EXPECT_EQ(" 100000.", sformat("{:8.}", 100000.0));
  EXPECT_EQ("100000.", sformat("{:4.}", 100000.0));
  EXPECT_EQ("  100000", sformat("{:8.8}", 100000.0));
  EXPECT_EQ(" 100000.", sformat("{:8.8.}", 100000.0));
}

TEST(Format, MultiLevel) {
  std::vector<std::map<std::string, std::string>> v = {
    {
      {"hello", "world"},
    },
  };

  EXPECT_EQ("world", sformat("{[0.hello]}", v));
}

TEST(Format, dynamic) {
  auto dyn = parseJson(
      "{\n"
      "  \"hello\": \"world\",\n"
      "  \"x\": [20, 30],\n"
      "  \"y\": {\"a\" : 42}\n"
      "}");

  EXPECT_EQ("world", sformat("{0[hello]}", dyn));
  EXPECT_THROW(sformat("{0[none]}", dyn), std::out_of_range);
  EXPECT_EQ("world", sformat("{0[hello]}", defaulted(dyn, "meow")));
  EXPECT_EQ("meow", sformat("{0[none]}", defaulted(dyn, "meow")));

  EXPECT_EQ("20", sformat("{0[x.0]}", dyn));
  EXPECT_THROW(sformat("{0[x.2]}", dyn), std::out_of_range);

  // No support for "deep" defaulting (dyn["x"] is not defaulted)
  auto v = dyn.at("x");
  EXPECT_EQ("20", sformat("{0[0]}", v));
  EXPECT_THROW(sformat("{0[2]}", v), std::out_of_range);
  EXPECT_EQ("20", sformat("{0[0]}", defaulted(v, 42)));
  EXPECT_EQ("42", sformat("{0[2]}", defaulted(v, 42)));

  EXPECT_EQ("42", sformat("{0[y.a]}", dyn));

  EXPECT_EQ("(null)", sformat("{}", dynamic(nullptr)));
}

TEST(Format, separatorDecimalInteger) {
  EXPECT_EQ("0", sformat("{:,d}", 0));
  EXPECT_EQ("1", sformat("{:d}", 1));
  EXPECT_EQ("1", sformat("{:,d}", 1));
  EXPECT_EQ("1", sformat("{:,}", 1));
  EXPECT_EQ("123", sformat("{:d}", 123));
  EXPECT_EQ("123", sformat("{:,d}", 123));
  EXPECT_EQ("123", sformat("{:,}", 123));
  EXPECT_EQ("1234", sformat("{:d}", 1234));
  EXPECT_EQ("1,234", sformat("{:,d}", 1234));
  EXPECT_EQ("1,234", sformat("{:,}", 1234));
  EXPECT_EQ("12345678", sformat("{:d}", 12345678));
  EXPECT_EQ("12,345,678", sformat("{:,d}", 12345678));
  EXPECT_EQ("12,345,678", sformat("{:,}", 12345678));
  EXPECT_EQ("-1234", sformat("{:d}", -1234));
  EXPECT_EQ("-1,234", sformat("{:,d}", -1234));
  EXPECT_EQ("-1,234", sformat("{:,}", -1234));

  int64_t max_int64_t = std::numeric_limits<int64_t>::max();
  int64_t min_int64_t = std::numeric_limits<int64_t>::min();
  uint64_t max_uint64_t = std::numeric_limits<uint64_t>::max();
  EXPECT_EQ("9223372036854775807", sformat("{:d}", max_int64_t));
  EXPECT_EQ("9,223,372,036,854,775,807", sformat("{:,d}", max_int64_t));
  EXPECT_EQ("9,223,372,036,854,775,807", sformat("{:,}", max_int64_t));
  EXPECT_EQ("-9223372036854775808", sformat("{:d}", min_int64_t));
  EXPECT_EQ("-9,223,372,036,854,775,808", sformat("{:,d}", min_int64_t));
  EXPECT_EQ("-9,223,372,036,854,775,808", sformat("{:,}", min_int64_t));
  EXPECT_EQ("18446744073709551615", sformat("{:d}", max_uint64_t));
  EXPECT_EQ("18,446,744,073,709,551,615", sformat("{:,d}", max_uint64_t));
  EXPECT_EQ("18,446,744,073,709,551,615", sformat("{:,}", max_uint64_t));

  EXPECT_EQ("  -1,234", sformat("{: 8,}", -1234));
  EXPECT_EQ("-001,234", sformat("{:08,d}", -1234));
  EXPECT_EQ("-00001,234", sformat("{:010,d}", -1234));
  EXPECT_EQ(" -1,234 ", sformat("{:^ 8,d}", -1234));
}

// Note that sformat("{:n}", ...) uses the current locale setting to insert the
// appropriate number separator characters.
TEST(Format, separatorNumber) {
  EXPECT_EQ("0", sformat("{:n}", 0));
  EXPECT_EQ("1", sformat("{:n}", 1));
  EXPECT_EQ("123", sformat("{:n}", 123));
  EXPECT_EQ("1234", sformat("{:n}", 1234));
  EXPECT_EQ("12345678", sformat("{:n}", 12345678));
  EXPECT_EQ("-1234", sformat("{:n}", -1234));

  int64_t max_int64_t = std::numeric_limits<int64_t>::max();
  int64_t min_int64_t = std::numeric_limits<int64_t>::min();
  uint64_t max_uint64_t = std::numeric_limits<uint64_t>::max();
  EXPECT_EQ("9223372036854775807", sformat("{:n}", max_int64_t));
  EXPECT_EQ("-9223372036854775808", sformat("{:n}", min_int64_t));
  EXPECT_EQ("18446744073709551615", sformat("{:n}", max_uint64_t));

  EXPECT_EQ("   -1234", sformat("{: 8n}", -1234));
  EXPECT_EQ("-0001234", sformat("{:08n}", -1234));
  EXPECT_EQ("-000001234", sformat("{:010n}", -1234));
  EXPECT_EQ(" -1234  ", sformat("{:^ 8n}", -1234));
}

// insertThousandsGroupingUnsafe requires non-const params
static void testGrouping(const char* a_str, const char* expected) {
  char str[256];
  strcpy(str, a_str);
  char * end_ptr = str + strlen(str);
  folly::detail::insertThousandsGroupingUnsafe(str, &end_ptr);
  ASSERT_STREQ(expected, str);
}

TEST(Format, separatorUnit) {
  testGrouping("0", "0");
  testGrouping("1", "1");
  testGrouping("12", "12");
  testGrouping("123", "123");
  testGrouping("1234", "1,234");
  testGrouping("12345", "12,345");
  testGrouping("123456", "123,456");
  testGrouping("1234567", "1,234,567");
  testGrouping("1234567890", "1,234,567,890");
  testGrouping("9223372036854775807", "9,223,372,036,854,775,807");
  testGrouping("18446744073709551615", "18,446,744,073,709,551,615");
}


namespace {

struct KeyValue {
  std::string key;
  int value;
};

}  // namespace

namespace folly {

template <> class FormatValue<KeyValue> {
 public:
  explicit FormatValue(const KeyValue& kv) : kv_(kv) { }

  template <class FormatCallback>
  void format(FormatArg& arg, FormatCallback& cb) const {
    format_value::formatFormatter(
        folly::format("<key={}, value={}>", kv_.key, kv_.value),
        arg, cb);
  }

 private:
  const KeyValue& kv_;
};

}  // namespace

TEST(Format, Custom) {
  KeyValue kv { "hello", 42 };

  EXPECT_EQ("<key=hello, value=42>", sformat("{}", kv));
  EXPECT_EQ("<key=hello, value=42>", sformat("{:10}", kv));
  EXPECT_EQ("<key=hello", sformat("{:.10}", kv));
  EXPECT_EQ("<key=hello, value=42>XX", sformat("{:X<23}", kv));
  EXPECT_EQ("XX<key=hello, value=42>", sformat("{:X>23}", kv));
  EXPECT_EQ("<key=hello, value=42>", sformat("{0[0]}", &kv));
  EXPECT_NE("", sformat("{}", &kv));
}

namespace {

struct Opaque {
  int k;
};

} // namespace

TEST(Format, Unformatted) {
  Opaque o;
  EXPECT_NE("", sformat("{}", &o));
  EXPECT_DEATH(sformat("{0[0]}", &o), "No formatter available for this type");
  EXPECT_THROW(sformatChecked("{0[0]}", &o), std::invalid_argument);
}

TEST(Format, Nested) {
  EXPECT_EQ("1 2 3 4", sformat("{} {} {}", 1, 2, format("{} {}", 3, 4)));
  //
  // not copyable, must hold temporary in scope instead.
  auto&& saved = format("{} {}", 3, 4);
  EXPECT_EQ("1 2 3 4", sformat("{} {} {}", 1, 2, saved));
}

TEST(Format, OutOfBounds) {
  std::vector<int> ints{1, 2, 3, 4, 5};
  EXPECT_EQ("1 3 5", sformat("{0[0]} {0[2]} {0[4]}", ints));
  EXPECT_THROW(sformat("{[5]}", ints), std::out_of_range);
  EXPECT_THROW(sformatChecked("{[5]}", ints), std::out_of_range);

  std::map<std::string, int> map{{"hello", 0}, {"world", 1}};
  EXPECT_EQ("hello = 0", sformat("hello = {[hello]}", map));
  EXPECT_THROW(sformat("{[nope]}", map), std::out_of_range);
  EXPECT_THROW(svformat("{nope}", map), std::out_of_range);
  EXPECT_THROW(svformatChecked("{nope}", map), std::out_of_range);
}

TEST(Format, BogusFormatString) {
  // format() will crash the program if the format string is invalid.
  EXPECT_DEATH(sformat("}"), "single '}' in format string");
  EXPECT_DEATH(sformat("foo}bar"), "single '}' in format string");
  EXPECT_DEATH(sformat("foo{bar"), "missing ending '}'");
  EXPECT_DEATH(sformat("{[test]"), "missing ending '}'");
  EXPECT_DEATH(sformat("{-1.3}"), "argument index must be non-negative");
  EXPECT_DEATH(sformat("{1.3}", 0, 1, 2), "index not allowed");
  EXPECT_DEATH(sformat("{0} {} {1}", 0, 1, 2),
               "may not have both default and explicit arg indexes");

  // formatChecked() should throw exceptions rather than crashing the program
  EXPECT_THROW(sformatChecked("}"), std::invalid_argument);
  EXPECT_THROW(sformatChecked("foo}bar"), std::invalid_argument);
  EXPECT_THROW(sformatChecked("foo{bar"), std::invalid_argument);
  EXPECT_THROW(sformatChecked("{[test]"), std::invalid_argument);
  EXPECT_THROW(sformatChecked("{-1.3}"), std::invalid_argument);
  EXPECT_THROW(sformatChecked("{1.3}", 0, 1, 2), std::invalid_argument);
  EXPECT_THROW(sformatChecked("{0} {} {1}", 0, 1, 2), std::invalid_argument);

  // This one fails in detail::enforceWhitespace(), which throws
  // std::range_error
  EXPECT_DEATH(sformat("{0[test}"), "Non-whitespace: \\[");
  EXPECT_THROW(sformatChecked("{0[test}"), std::exception);
}

template <bool containerMode, class... Args>
class TestExtendingFormatter;

template <bool containerMode, class... Args>
class TestExtendingFormatter
    : public BaseFormatter<TestExtendingFormatter<containerMode, Args...>,
                           containerMode,
                           Args...> {
 private:
  explicit TestExtendingFormatter(StringPiece& str, Args&&... args)
      : BaseFormatter<TestExtendingFormatter<containerMode, Args...>,
                      containerMode,
                      Args...>(str, std::forward<Args>(args)...) {}

  template <size_t K, class Callback>
  void doFormatArg(FormatArg& arg, Callback& cb) const {
    std::string result;
    auto appender = [&result](StringPiece s) {
      result.append(s.data(), s.size());
    };
    std::get<K>(this->values_).format(arg, appender);
    result = sformat("{{{}}}", result);
    cb(StringPiece(result));
  }

  friend class BaseFormatter<TestExtendingFormatter<containerMode, Args...>,
                             containerMode,
                             Args...>;

  template <class... A>
  friend std::string texsformat(StringPiece fmt, A&&... arg);
};

template <class... Args>
std::string texsformat(StringPiece fmt, Args&&... args) {
  return TestExtendingFormatter<false, Args...>(
      fmt, std::forward<Args>(args)...).str();
}

TEST(Format, Extending) {
  EXPECT_EQ(texsformat("I {} brackets", "love"), "I {love} brackets");
  EXPECT_EQ(texsformat("I {} nesting", sformat("really {}", "love")),
            "I {really love} nesting");
  EXPECT_EQ(
      sformat("I also {} nesting", texsformat("have an {} for", "affinity")),
      "I also have an {affinity} for nesting");
  EXPECT_EQ(texsformat("Extending {} in {}",
                       texsformat("a {}", "formatter"),
                       "another formatter"),
            "Extending {a {formatter}} in {another formatter}");
}

int main(int argc, char *argv[]) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  return RUN_ALL_TESTS();
}
