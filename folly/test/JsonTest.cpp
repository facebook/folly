/*
 * Copyright 2012 Facebook, Inc.
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

#include "folly/json.h"
#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include <cmath>
#include <limits>
#include <iostream>
#include <boost/next_prior.hpp>
#include "folly/Benchmark.h"

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
  EXPECT_THROW(parseJson("infinity"), std::runtime_error);
  EXPECT_THROW(parseJson("inf"), std::runtime_error);
  EXPECT_THROW(parseJson("nan"), std::runtime_error);

  auto array = parseJson(
    "[12,false, false  , null , [12e4,32, [], 12]]");
  EXPECT_EQ(array.size(), 5);
  if (array.size() == 5) {
    EXPECT_EQ(boost::prior(array.end())->size(), 4);
  }

  bool caught = false;
  try {
    parseJson("\n[12,\n\nnotvalidjson");
  } catch (const std::exception& e) {
    caught = true;
  }
  EXPECT_TRUE(caught);

  caught = false;
  try {
    parseJson("12e2e2");
  } catch (const std::exception& e) {
    caught = true;
  }
  EXPECT_TRUE(caught);

  caught = false;
  try {
    parseJson("{\"foo\":12,\"bar\":42} \"something\"");
  } catch (const std::exception& e) {
    // incomplete parse
    caught = true;
  }
  EXPECT_TRUE(caught);

  dynamic anotherVal = dynamic::object
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
  auto value = parseJson(toJson(anotherVal));
  EXPECT_EQ(value, anotherVal);

  // Test an object with non-string values.
  dynamic something = folly::parseJson(
    "{\"old_value\":40,\"changed\":true,\"opened\":false}");
  dynamic expected = dynamic::object
    ("old_value", 40)
    ("changed", true)
    ("opened", false);
  EXPECT_EQ(something, expected);
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

  bool caught = false;
  try {
    dynamic d = dynamic::object;
    d["abc"] = "xyz";
    d[42.33] = "asd";
    auto str = toJson(d);
  } catch (std::exception const& e) {
    // We're not allowed to have non-string keys in json.
    caught = true;
  }
  EXPECT_TRUE(caught);
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

TEST(Json, UTF8Validation) {
  folly::json::serialization_opts opts;
  opts.validate_utf8 = true;

  // valid utf8 strings
  EXPECT_EQ(folly::json::serialize("a\xc2\x80z", opts), R"("a\u00c2\u0080z")");
  EXPECT_EQ(
    folly::json::serialize("a\xe0\xa0\x80z", opts),
    R"("a\u00e0\u00a0\u0080z")");
  EXPECT_EQ(
    folly::json::serialize("a\xe0\xa0\x80m\xc2\x80z", opts),
    R"("a\u00e0\u00a0\u0080m\u00c2\u0080z")");

  // test with invalid utf8
  EXPECT_ANY_THROW(folly::json::serialize("a\xe0\xa0\x80z\xc0\x80", opts));
  EXPECT_ANY_THROW(folly::json::serialize("a\xe0\xa0\x80z\xe0\x80\x80", opts));
}

BENCHMARK(jsonSerialize, iters) {
  folly::json::serialization_opts opts;
  for (int i = 0; i < iters; ++i) {
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

  for (int i = 0; i < iters; ++i) {
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

  for (int i = 0; i < iters; ++i) {
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
  for (int i = 0; i < iters << 4; ++i) {
    parseJson("\"I \\u2665 UTF-8 thjasdhkjh blah blah blah\"");
  }
}

BENCHMARK(parseNormalString, iters) {
  for (int i = 0; i < iters << 4; ++i) {
    parseJson("\"akjhfk jhkjlakjhfk jhkjlakjhfk jhkjl akjhfk\"");
  }
}

BENCHMARK(parseBigString, iters) {
  for (int i = 0; i < iters; ++i) {
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

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);
  if (FLAGS_benchmark) {
    folly::runBenchmarks();
  }
  return RUN_ALL_TESTS();
}
