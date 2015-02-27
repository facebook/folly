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

#include <folly/FileUtil.h>
#include <folly/json.h>
#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include <cmath>
#include <limits>
#include <iostream>
#include <boost/next_prior.hpp>
#include <folly/Benchmark.h>

using folly::dynamic;
using folly::parseJson;
using folly::toJson;

TEST(Json, Unicode) {
  auto val = parseJson("\"I \u2665 UTF-8\"");
  EXPECT_EQ("I \u2665 UTF-8", val.asString());
  val = parseJson("\"I \\u2665 UTF-8\"");
  EXPECT_EQ("I \u2665 UTF-8", val.asString());
  val = parseJson("\"I \U0001D11E playing in G-clef\"");
  EXPECT_EQ("I \U0001D11E playing in G-clef", val.asString());

  val = parseJson("\"I \\uD834\\uDD1E playing in G-clef\"");
  EXPECT_EQ("I \U0001D11E playing in G-clef", val.asString());
}

TEST(Json, Parse) {
  auto num = parseJson("12");
  EXPECT_TRUE(num.isInt());
  EXPECT_EQ(num, 12);
  num = parseJson("12e5");
  EXPECT_TRUE(num.isDouble());
  EXPECT_EQ(num, 12e5);
  auto numAs1 = num.asDouble();
  EXPECT_EQ(numAs1, 12e5);
  EXPECT_EQ(num, 12e5);
  EXPECT_EQ(num, 1200000);

  auto largeNumber = parseJson("4611686018427387904");
  EXPECT_TRUE(largeNumber.isInt());
  EXPECT_EQ(largeNumber, 4611686018427387904L);

  auto negative = parseJson("-123");
  EXPECT_EQ(negative, -123);

  auto bfalse = parseJson("false");
  auto btrue = parseJson("true");
  EXPECT_EQ(bfalse, false);
  EXPECT_EQ(btrue, true);

  auto null = parseJson("null");
  EXPECT_TRUE(null == nullptr);

  auto doub1 = parseJson("12.0");
  auto doub2 = parseJson("12e2");
  EXPECT_EQ(doub1, 12.0);
  EXPECT_EQ(doub2, 12e2);
  EXPECT_EQ(std::numeric_limits<double>::infinity(),
            parseJson("Infinity").asDouble());
  EXPECT_EQ(-std::numeric_limits<double>::infinity(),
            parseJson("-Infinity").asDouble());
  EXPECT_TRUE(std::isnan(parseJson("NaN").asDouble()));

  // case matters
  EXPECT_THROW(parseJson("infinity"), std::runtime_error);
  EXPECT_THROW(parseJson("inf"), std::runtime_error);
  EXPECT_THROW(parseJson("nan"), std::runtime_error);

  auto array = parseJson(
    "[12,false, false  , null , [12e4,32, [], 12]]");
  EXPECT_EQ(array.size(), 5);
  if (array.size() == 5) {
    EXPECT_EQ(boost::prior(array.end())->size(), 4);
  }

  EXPECT_THROW(parseJson("\n[12,\n\nnotvalidjson"),
               std::runtime_error);

  EXPECT_THROW(parseJson("12e2e2"),
               std::runtime_error);

  EXPECT_THROW(parseJson("{\"foo\":12,\"bar\":42} \"something\""),
               std::runtime_error);

  dynamic value = dynamic::object
    ("foo", "bar")
    ("junk", 12)
    ("another", 32.2)
    ("a",
      {
        dynamic::object("a", "b")
                       ("c", "d"),
        12.5,
        "Yo Dawg",
        { "heh" },
        nullptr
      }
    )
    ;

  // Print then parse and get the same thing, hopefully.
  EXPECT_EQ(value, parseJson(toJson(value)));


  // Test an object with non-string values.
  dynamic something = parseJson(
    "{\"old_value\":40,\"changed\":true,\"opened\":false}");
  dynamic expected = dynamic::object
    ("old_value", 40)
    ("changed", true)
    ("opened", false);
  EXPECT_EQ(something, expected);
}

TEST(Json, ParseTrailingComma) {
  folly::json::serialization_opts on, off;
  on.allow_trailing_comma = true;
  off.allow_trailing_comma = false;

  dynamic arr { 1, 2 };
  EXPECT_EQ(arr, parseJson("[1, 2]", on));
  EXPECT_EQ(arr, parseJson("[1, 2,]", on));
  EXPECT_EQ(arr, parseJson("[1, 2, ]", on));
  EXPECT_EQ(arr, parseJson("[1, 2 , ]", on));
  EXPECT_EQ(arr, parseJson("[1, 2 ,]", on));
  EXPECT_THROW(parseJson("[1, 2,]", off), std::runtime_error);

  dynamic obj = dynamic::object("a", 1);
  EXPECT_EQ(obj, parseJson("{\"a\": 1}", on));
  EXPECT_EQ(obj, parseJson("{\"a\": 1,}", on));
  EXPECT_EQ(obj, parseJson("{\"a\": 1, }", on));
  EXPECT_EQ(obj, parseJson("{\"a\": 1 , }", on));
  EXPECT_EQ(obj, parseJson("{\"a\": 1 ,}", on));
  EXPECT_THROW(parseJson("{\"a\":1,}", off), std::runtime_error);
}

TEST(Json, JavascriptSafe) {
  auto badDouble = (1ll << 63ll) + 1;
  dynamic badDyn = badDouble;
  EXPECT_EQ(folly::toJson(badDouble), folly::to<folly::fbstring>(badDouble));
  folly::json::serialization_opts opts;
  opts.javascript_safe = true;
  EXPECT_ANY_THROW(folly::json::serialize(badDouble, opts));

  auto okDouble = 1ll << 63ll;
  dynamic okDyn = okDouble;
  EXPECT_EQ(folly::toJson(okDouble), folly::to<folly::fbstring>(okDouble));
}

TEST(Json, Produce) {
  auto value = parseJson(R"( "f\"oo" )");
  EXPECT_EQ(toJson(value), R"("f\"oo")");
  value = parseJson("\"Control code: \001 \002 \x1f\"");
  EXPECT_EQ(toJson(value), R"("Control code: \u0001 \u0002 \u001f")");

  // We're not allowed to have non-string keys in json.
  EXPECT_THROW(toJson(dynamic::object("abc", "xyz")(42.33, "asd")),
               std::runtime_error);
}

TEST(Json, JsonEscape) {
  folly::json::serialization_opts opts;
  EXPECT_EQ(
    folly::json::serialize("\b\f\n\r\x01\t\\\"/\v\a", opts),
    R"("\b\f\n\r\u0001\t\\\"/\u000b\u0007")");
}

TEST(Json, JsonNonAsciiEncoding) {
  folly::json::serialization_opts opts;
  opts.encode_non_ascii = true;

  // simple tests
  EXPECT_EQ(folly::json::serialize("\x1f", opts), R"("\u001f")");
  EXPECT_EQ(folly::json::serialize("\xc2\xa2", opts), R"("\u00a2")");
  EXPECT_EQ(folly::json::serialize("\xe2\x82\xac", opts), R"("\u20ac")");

  // multiple unicode encodings
  EXPECT_EQ(
    folly::json::serialize("\x1f\xe2\x82\xac", opts),
    R"("\u001f\u20ac")");
  EXPECT_EQ(
    folly::json::serialize("\x1f\xc2\xa2\xe2\x82\xac", opts),
    R"("\u001f\u00a2\u20ac")");
  EXPECT_EQ(
    folly::json::serialize("\xc2\x80\xef\xbf\xbf", opts),
    R"("\u0080\uffff")");
  EXPECT_EQ(
    folly::json::serialize("\xe0\xa0\x80\xdf\xbf", opts),
    R"("\u0800\u07ff")");

  // first possible sequence of a certain length
  EXPECT_EQ(folly::json::serialize("\xc2\x80", opts), R"("\u0080")");
  EXPECT_EQ(folly::json::serialize("\xe0\xa0\x80", opts), R"("\u0800")");

  // last possible sequence of a certain length
  EXPECT_EQ(folly::json::serialize("\xdf\xbf", opts), R"("\u07ff")");
  EXPECT_EQ(folly::json::serialize("\xef\xbf\xbf", opts), R"("\uffff")");

  // other boundary conditions
  EXPECT_EQ(folly::json::serialize("\xed\x9f\xbf", opts), R"("\ud7ff")");
  EXPECT_EQ(folly::json::serialize("\xee\x80\x80", opts), R"("\ue000")");
  EXPECT_EQ(folly::json::serialize("\xef\xbf\xbd", opts), R"("\ufffd")");

  // incomplete sequences
  EXPECT_ANY_THROW(folly::json::serialize("a\xed\x9f", opts));
  EXPECT_ANY_THROW(folly::json::serialize("b\xee\x80", opts));
  EXPECT_ANY_THROW(folly::json::serialize("c\xef\xbf", opts));

  // impossible bytes
  EXPECT_ANY_THROW(folly::json::serialize("\xfe", opts));
  EXPECT_ANY_THROW(folly::json::serialize("\xff", opts));

  // Sample overlong sequences
  EXPECT_ANY_THROW(folly::json::serialize("\xc0\xaf", opts));
  EXPECT_ANY_THROW(folly::json::serialize("\xe0\x80\xaf", opts));

  // Maximum overlong sequences
  EXPECT_ANY_THROW(folly::json::serialize("\xc1\xbf", opts));
  EXPECT_ANY_THROW(folly::json::serialize("\x30\x9f\xbf", opts));

  // illegal code positions
  EXPECT_ANY_THROW(folly::json::serialize("\xed\xa0\x80", opts));
  EXPECT_ANY_THROW(folly::json::serialize("\xed\xbf\xbf", opts));

  // Overlong representation of NUL character
  EXPECT_ANY_THROW(folly::json::serialize("\xc0\x80", opts));
  EXPECT_ANY_THROW(folly::json::serialize("\xe0\x80\x80", opts));

  // Longer than 3 byte encodings
  EXPECT_ANY_THROW(folly::json::serialize("\xf4\x8f\xbf\xbf", opts));
  EXPECT_ANY_THROW(folly::json::serialize("\xed\xaf\xbf\xed\xbf\xbf", opts));
}

TEST(Json, UTF8Retention) {

  // test retention with valid utf8 strings
  folly::fbstring input = "\u2665";
  folly::fbstring jsonInput = folly::toJson(input);
  folly::fbstring output = folly::parseJson(jsonInput).asString();
  folly::fbstring jsonOutput = folly::toJson(output);

  EXPECT_EQ(input, output);
  EXPECT_EQ(jsonInput, jsonOutput);

  // test retention with invalid utf8 - note that non-ascii chars are retained
  // as is, and no unicode encoding is attempted so no exception is thrown.
  EXPECT_EQ(
    folly::toJson("a\xe0\xa0\x80z\xc0\x80"),
    "\"a\xe0\xa0\x80z\xc0\x80\""
  );
}

TEST(Json, UTF8EncodeNonAsciiRetention) {

  folly::json::serialization_opts opts;
  opts.encode_non_ascii = true;

  // test encode_non_ascii valid utf8 strings
  folly::fbstring input = "\u2665";
  folly::fbstring jsonInput = folly::json::serialize(input, opts);
  folly::fbstring output = folly::parseJson(jsonInput).asString();
  folly::fbstring jsonOutput = folly::json::serialize(output, opts);

  EXPECT_EQ(input, output);
  EXPECT_EQ(jsonInput, jsonOutput);

  // test encode_non_ascii with invalid utf8 - note that an attempt to encode
  // non-ascii to unicode will result is a utf8 validation and throw exceptions.
  EXPECT_ANY_THROW(folly::json::serialize("a\xe0\xa0\x80z\xc0\x80", opts));
  EXPECT_ANY_THROW(folly::json::serialize("a\xe0\xa0\x80z\xe0\x80\x80", opts));
}

TEST(Json, UTF8Validation) {
  folly::json::serialization_opts opts;
  opts.validate_utf8 = true;

  // test validate_utf8 valid utf8 strings - note that we only validate the
  // for utf8 but don't encode non-ascii to unicode so they are retained as is.
  EXPECT_EQ(folly::json::serialize("a\xc2\x80z", opts), "\"a\xc2\x80z\"");
  EXPECT_EQ(
    folly::json::serialize("a\xe0\xa0\x80z", opts),
    "\"a\xe0\xa0\x80z\"");
  EXPECT_EQ(
    folly::json::serialize("a\xe0\xa0\x80m\xc2\x80z", opts),
    "\"a\xe0\xa0\x80m\xc2\x80z\"");

  // test validate_utf8 with invalid utf8
  EXPECT_ANY_THROW(folly::json::serialize("a\xe0\xa0\x80z\xc0\x80", opts));
  EXPECT_ANY_THROW(folly::json::serialize("a\xe0\xa0\x80z\xe0\x80\x80", opts));

  opts.skip_invalid_utf8 = true;
  EXPECT_EQ(folly::json::serialize("a\xe0\xa0\x80z\xc0\x80", opts),
            "\"a\xe0\xa0\x80z\ufffd\ufffd\"");
  EXPECT_EQ(folly::json::serialize("a\xe0\xa0\x80z\xc0\x80\x80", opts),
            "\"a\xe0\xa0\x80z\ufffd\ufffd\ufffd\"");
  EXPECT_EQ(folly::json::serialize("z\xc0\x80z\xe0\xa0\x80", opts),
            "\"z\ufffd\ufffdz\xe0\xa0\x80\"");

  opts.encode_non_ascii = true;
  EXPECT_EQ(folly::json::serialize("a\xe0\xa0\x80z\xc0\x80", opts),
            "\"a\\u0800z\\ufffd\\ufffd\"");
  EXPECT_EQ(folly::json::serialize("a\xe0\xa0\x80z\xc0\x80\x80", opts),
            "\"a\\u0800z\\ufffd\\ufffd\\ufffd\"");
  EXPECT_EQ(folly::json::serialize("z\xc0\x80z\xe0\xa0\x80", opts),
            "\"z\\ufffd\\ufffdz\\u0800\"");

}


TEST(Json, ParseNonStringKeys) {
  // test string keys
  EXPECT_EQ("a", parseJson("{\"a\":[]}").items().begin()->first.asString());

  // check that we don't allow non-string keys as this violates the
  // strict JSON spec (though it is emitted by the output of
  // folly::dynamic with operator <<).
  EXPECT_THROW(parseJson("{1:[]}"), std::runtime_error);

  // check that we can parse colloquial JSON if the option is set
  folly::json::serialization_opts opts;
  opts.allow_non_string_keys = true;

  auto val = parseJson("{1:[]}", opts);
  EXPECT_EQ(1, val.items().begin()->first.asInt());


  // test we can still read in strings
  auto sval = parseJson("{\"a\":[]}", opts);
  EXPECT_EQ("a", sval.items().begin()->first.asString());

  // test we can read in doubles
  auto dval = parseJson("{1.5:[]}", opts);
  EXPECT_EQ(1.5, dval.items().begin()->first.asDouble());
}

TEST(Json, SortKeys) {
  folly::json::serialization_opts opts_on, opts_off;
  opts_on.sort_keys = true;
  opts_off.sort_keys = false;

  dynamic value = dynamic::object
    ("foo", "bar")
    ("junk", 12)
    ("another", 32.2)
    ("a",
      {
        dynamic::object("a", "b")
                       ("c", "d"),
        12.5,
        "Yo Dawg",
        { "heh" },
        nullptr
      }
    )
    ;

  std::string sorted_keys =
    R"({"a":[{"a":"b","c":"d"},12.5,"Yo Dawg",["heh"],null],)"
    R"("another":32.2,"foo":"bar","junk":12})";

  EXPECT_EQ(value, parseJson(folly::json::serialize(value, opts_on)));
  EXPECT_EQ(value, parseJson(folly::json::serialize(value, opts_off)));

  EXPECT_EQ(sorted_keys, folly::json::serialize(value, opts_on));
}

TEST(Json, StripComments) {
  const std::string kTestDir = "folly/test/";
  const std::string kTestFile = "json_test_data/commented.json";
  const std::string kTestExpected = "json_test_data/commented.json.exp";

  std::string testStr;
  std::string expectedStr;
  if (!folly::readFile(kTestFile.data(), testStr) &&
      !folly::readFile((kTestDir + kTestFile).data(), testStr)) {
    FAIL() << "can not read test file " << kTestFile;
  }
  if (!folly::readFile(kTestExpected.data(), expectedStr) &&
      !folly::readFile((kTestDir + kTestExpected).data(), expectedStr)) {
    FAIL() << "can not read test file " << kTestExpected;
  }
  EXPECT_EQ(expectedStr, folly::json::stripComments(testStr));
}

BENCHMARK(jsonSerialize, iters) {
  folly::json::serialization_opts opts;
  for (size_t i = 0; i < iters; ++i) {
    folly::json::serialize(
      "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
      "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
      "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
      "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
      "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
      "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
      "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
      "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
      "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
      "qwerty \xc2\x80 \xef\xbf\xbf poiuy",
      opts);
  }
}

BENCHMARK(jsonSerializeWithNonAsciiEncoding, iters) {
  folly::json::serialization_opts opts;
  opts.encode_non_ascii = true;

  for (size_t i = 0; i < iters; ++i) {
    folly::json::serialize(
      "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
      "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
      "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
      "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
      "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
      "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
      "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
      "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
      "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
      "qwerty \xc2\x80 \xef\xbf\xbf poiuy",
      opts);
  }
}

BENCHMARK(jsonSerializeWithUtf8Validation, iters) {
  folly::json::serialization_opts opts;
  opts.validate_utf8 = true;

  for (size_t i = 0; i < iters; ++i) {
    folly::json::serialize(
      "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
      "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
      "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
      "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
      "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
      "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
      "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
      "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
      "qwerty \xc2\x80 \xef\xbf\xbf poiuy"
      "qwerty \xc2\x80 \xef\xbf\xbf poiuy",
      opts);
  }
}

BENCHMARK(parseSmallStringWithUtf, iters) {
  for (size_t i = 0; i < iters << 4; ++i) {
    parseJson("\"I \\u2665 UTF-8 thjasdhkjh blah blah blah\"");
  }
}

BENCHMARK(parseNormalString, iters) {
  for (size_t i = 0; i < iters << 4; ++i) {
    parseJson("\"akjhfk jhkjlakjhfk jhkjlakjhfk jhkjl akjhfk\"");
  }
}

BENCHMARK(parseBigString, iters) {
  for (size_t i = 0; i < iters; ++i) {
    parseJson("\""
      "akjhfk jhkjlakjhfk jhkjlakjhfk jhkjl akjhfk"
      "akjhfk jhkjlakjhfk jhkjlakjhfk jhkjl akjhfk"
      "akjhfk jhkjlakjhfk jhkjlakjhfk jhkjl akjhfk"
      "akjhfk jhkjlakjhfk jhkjlakjhfk jhkjl akjhfk"
      "akjhfk jhkjlakjhfk jhkjlakjhfk jhkjl akjhfk"
      "akjhfk jhkjlakjhfk jhkjlakjhfk jhkjl akjhfk"
      "akjhfk jhkjlakjhfk jhkjlakjhfk jhkjl akjhfk"
      "akjhfk jhkjlakjhfk jhkjlakjhfk jhkjl akjhfk"
      "akjhfk jhkjlakjhfk jhkjlakjhfk jhkjl akjhfk"
      "akjhfk jhkjlakjhfk jhkjlakjhfk jhkjl akjhfk"
      "akjhfk jhkjlakjhfk jhkjlakjhfk jhkjl akjhfk"
      "\"");
  }
}

BENCHMARK(toJson, iters) {
  dynamic something = parseJson(
    "{\"old_value\":40,\"changed\":true,\"opened\":false,\"foo\":[1,2,3,4,5,6]}"
  );

  for (size_t i = 0; i < iters; i++) {
    toJson(something);
  }
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  if (FLAGS_benchmark) {
    folly::runBenchmarks();
  }
  return RUN_ALL_TESTS();
}
