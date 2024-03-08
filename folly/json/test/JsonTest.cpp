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

#include <folly/json/json.h>

#include <cstdint>
#include <iterator>
#include <limits>

#include <folly/portability/GTest.h>

using folly::dynamic;
using folly::parseJson;
using folly::parseJsonWithMetadata;
using folly::toJson;
using folly::json::parse_error;
using folly::json::print_error;

TEST(Json, Unicode) {
  auto val = parseJson(reinterpret_cast<const char*>(u8"\"I \u2665 UTF-8\""));
  EXPECT_EQ(reinterpret_cast<const char*>(u8"I \u2665 UTF-8"), val.asString());
  val = parseJson("\"I \\u2665 UTF-8\"");
  EXPECT_EQ(reinterpret_cast<const char*>(u8"I \u2665 UTF-8"), val.asString());
  val = parseJson(
      reinterpret_cast<const char*>(u8"\"I \U0001D11E playing in G-clef\""));
  EXPECT_EQ(
      reinterpret_cast<const char*>(u8"I \U0001D11E playing in G-clef"),
      val.asString());

  val = parseJson("\"I \\uD834\\uDD1E playing in G-clef\"");
  EXPECT_EQ(
      reinterpret_cast<const char*>(u8"I \U0001D11E playing in G-clef"),
      val.asString());
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
  EXPECT_EQ(largeNumber, 4611686018427387904LL);

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
  EXPECT_EQ(
      std::numeric_limits<double>::infinity(),
      parseJson("Infinity").asDouble());
  EXPECT_EQ(
      -std::numeric_limits<double>::infinity(),
      parseJson("-Infinity").asDouble());
  EXPECT_TRUE(std::isnan(parseJson("NaN").asDouble()));

  // case matters
  EXPECT_THROW(parseJson("infinity"), parse_error);
  EXPECT_THROW(parseJson("inf"), parse_error);
  EXPECT_THROW(parseJson("Inf"), parse_error);
  EXPECT_THROW(parseJson("INF"), parse_error);
  EXPECT_THROW(parseJson("nan"), parse_error);
  EXPECT_THROW(parseJson("NAN"), parse_error);

  auto array = parseJson("[12,false, false  , null , [12e4,32, [], 12]]");
  EXPECT_EQ(array.size(), 5);
  if (array.size() == 5) {
    EXPECT_EQ(std::prev(array.end())->size(), 4);
  }

  EXPECT_THROW(parseJson("\n[12,\n\nnotvalidjson"), parse_error);

  EXPECT_THROW(parseJson("12e2e2"), parse_error);

  EXPECT_THROW(parseJson("{\"foo\":12,\"bar\":42} \"something\""), parse_error);

  // clang-format off
  dynamic value = dynamic::object
    ("foo", "bar")
    ("junk", 12)
    ("another", 32.2)
    ("a",
      dynamic::array(
          dynamic::object("a", "b")("c", "d"),
          12.5,
          "Yo Dawg",
          dynamic::array("heh"),
          nullptr));
  // clang-format on

  // Print then parse and get the same thing, hopefully.
  EXPECT_EQ(value, parseJson(toJson(value)));

  // Test an object with non-string values.
  dynamic something =
      parseJson("{\"old_value\":40,\"changed\":true,\"opened\":false}");
  dynamic expected =
      dynamic::object("old_value", 40)("changed", true)("opened", false);
  EXPECT_EQ(something, expected);
}

TEST(Json, TestLineNumbers) {
  // Simple object
  folly::json::metadata_map map;
  dynamic val = parseJsonWithMetadata("\n\n{\n\n\"value\":40}", &map);
  EXPECT_TRUE(val.isObject());
  auto ov = val.get_ptr("value");
  EXPECT_TRUE(ov != nullptr);
  auto it = map.find(ov);
  EXPECT_TRUE(it != map.end());
  EXPECT_EQ(it->second.key_range.begin.line, 4);
  EXPECT_EQ(it->second.value_range.begin.line, 4);

  // check with find() API too
  auto dv = val.find("value");
  it = map.find(&dv->second);
  EXPECT_TRUE(it != map.end());
  EXPECT_EQ(it->second.key_range.begin.line, 4);
  EXPECT_EQ(it->second.value_range.begin.line, 4);

  map.clear();

  // One line apart
  val = parseJsonWithMetadata(
      "{\"old_value\":40,\n\"changed\":true,\n\"opened\":1.5}", &map);

  EXPECT_TRUE(val.isObject());
  auto i1 = val.get_ptr("old_value");
  EXPECT_TRUE(i1 != nullptr);
  it = map.find(i1);
  EXPECT_TRUE(it != map.end());
  EXPECT_EQ(it->second.key_range.begin.line, 0);
  EXPECT_EQ(it->second.value_range.begin.line, 0);
  EXPECT_EQ(i1->asInt(), 40);

  auto i2 = val.get_ptr("changed");
  EXPECT_TRUE(i2 != nullptr);
  it = map.find(i2);
  EXPECT_TRUE(it != map.end());
  EXPECT_EQ(it->second.key_range.begin.line, 1);
  EXPECT_EQ(it->second.value_range.begin.line, 1);
  EXPECT_EQ(i2->asBool(), true);

  auto i3 = val.get_ptr("opened");
  EXPECT_TRUE(i3 != nullptr);
  it = map.find(i3);
  EXPECT_TRUE(it != map.end());
  EXPECT_EQ(it->second.key_range.begin.line, 2);
  EXPECT_EQ(it->second.value_range.begin.line, 2);
  EXPECT_EQ(i3->asDouble(), 1.5);
  map.clear();

  // Multiple lines apart
  val = parseJsonWithMetadata(
      "\n{\n\"a\":40,\n\"b\":1.45,\n"
      "\n\n\"c\":false,\n\n\n\n\n\"d\":\"dval\"\n\n}",
      &map);

  EXPECT_TRUE(val.isObject());

  i1 = val.get_ptr("a");
  EXPECT_TRUE(i1 != nullptr);
  it = map.find(i1);
  EXPECT_TRUE(it != map.end());
  EXPECT_EQ(it->second.key_range.begin.line, 2);
  EXPECT_EQ(it->second.value_range.begin.line, 2);
  EXPECT_EQ(i1->asInt(), 40);

  i2 = val.get_ptr("b");
  EXPECT_TRUE(i2 != nullptr);
  it = map.find(i2);
  EXPECT_TRUE(it != map.end());
  EXPECT_EQ(it->second.key_range.begin.line, 3);
  EXPECT_EQ(it->second.value_range.begin.line, 3);
  EXPECT_EQ(i2->asDouble(), 1.45);

  i3 = val.get_ptr("c");
  EXPECT_TRUE(i3 != nullptr);
  it = map.find(i3);
  EXPECT_TRUE(it != map.end());
  EXPECT_EQ(it->second.key_range.begin.line, 6);
  EXPECT_EQ(it->second.value_range.begin.line, 6);
  EXPECT_EQ(i3->asBool(), false);

  auto i4 = val.get_ptr("d");
  EXPECT_TRUE(i4 != nullptr);
  it = map.find(i4);
  EXPECT_TRUE(it != map.end());
  EXPECT_EQ(it->second.key_range.begin.line, 11);
  EXPECT_EQ(it->second.value_range.begin.line, 11);
  EXPECT_EQ(i4->asString(), "dval");
  map.clear();

  // All in the same line
  val = parseJsonWithMetadata("{\"x\":40,\"y\":true,\"z\":3.33}", &map);
  EXPECT_TRUE(val.isObject());

  i1 = val.get_ptr("x");
  EXPECT_TRUE(i1 != nullptr);
  it = map.find(i1);
  EXPECT_TRUE(it != map.end());
  EXPECT_EQ(it->second.key_range.begin.line, 0);
  EXPECT_EQ(it->second.value_range.begin.line, 0);
  EXPECT_EQ(i1->asInt(), 40);

  i2 = val.get_ptr("y");
  EXPECT_TRUE(i2 != nullptr);
  it = map.find(i2);
  EXPECT_TRUE(it != map.end());
  EXPECT_EQ(it->second.key_range.begin.line, 0);
  EXPECT_EQ(it->second.value_range.begin.line, 0);
  EXPECT_EQ(i2->asBool(), true);

  i3 = val.get_ptr("z");
  EXPECT_TRUE(i3 != nullptr);
  it = map.find(i3);
  EXPECT_TRUE(it != map.end());
  EXPECT_EQ(it->second.key_range.begin.line, 0);
  EXPECT_EQ(it->second.value_range.begin.line, 0);
  map.clear();

  // Key and value in different numbers
  val =
      parseJsonWithMetadata("{\"x\":\n70,\"y\":\n\n80,\n\"z\":\n\n\n33}", &map);
  EXPECT_TRUE(val.isObject());

  i1 = val.get_ptr("x");
  EXPECT_TRUE(i1 != nullptr);
  it = map.find(i1);
  EXPECT_TRUE(it != map.end());
  EXPECT_EQ(it->second.key_range.begin.line, 0);
  EXPECT_EQ(it->second.value_range.begin.line, 1);
  EXPECT_EQ(i1->asInt(), 70);

  i2 = val.get_ptr("y");
  EXPECT_TRUE(i2 != nullptr);
  it = map.find(i2);
  EXPECT_TRUE(it != map.end());
  EXPECT_EQ(it->second.key_range.begin.line, 1);
  EXPECT_EQ(it->second.value_range.begin.line, 3);
  EXPECT_EQ(i2->asInt(), 80);

  i3 = val.get_ptr("z");
  EXPECT_TRUE(i3 != nullptr);
  it = map.find(i3);
  EXPECT_TRUE(it != map.end());
  EXPECT_EQ(it->second.key_range.begin.line, 4);
  EXPECT_EQ(it->second.value_range.begin.line, 7);
  EXPECT_EQ(i3->asInt(), 33);
  map.clear();

  // With Arrays
  val = parseJsonWithMetadata(
      "{\"x\":\n[10, 20],\n\"y\":\n\n[80,\n90,\n100]}", &map);
  EXPECT_TRUE(val.isObject());

  i1 = val.get_ptr("x");
  EXPECT_TRUE(i1 != nullptr);
  it = map.find(i1);
  EXPECT_TRUE(it != map.end());
  EXPECT_EQ(it->second.key_range.begin.line, 0);
  EXPECT_EQ(it->second.value_range.begin.line, 1);

  int i = 1;
  for (auto arr_it = i1->begin(); arr_it != i1->end(); arr_it++, i++) {
    auto arr_it_md = map.find(&*arr_it);
    EXPECT_TRUE(arr_it_md != map.end());
    EXPECT_EQ(arr_it_md->second.value_range.begin.line, 1);
    EXPECT_EQ(arr_it->asInt(), 10 * i);
  }

  i2 = val.get_ptr("y");
  EXPECT_TRUE(i2 != nullptr);
  it = map.find(i2);
  EXPECT_TRUE(it != map.end());
  EXPECT_EQ(it->second.key_range.begin.line, 2);
  EXPECT_EQ(it->second.value_range.begin.line, 4);

  i = 8;
  int ln = 4;
  for (auto arr_it = i2->begin(); arr_it != i2->end(); arr_it++, i++, ln++) {
    auto arr_it_md = map.find(&*arr_it);
    EXPECT_TRUE(arr_it_md != map.end());
    EXPECT_EQ(arr_it_md->second.value_range.begin.line, ln);
    EXPECT_EQ(arr_it->asInt(), 10 * i);
  }
  map.clear();

  // With nested objects
  val = parseJsonWithMetadata(
      "{\"a1\":{\n\"a2\":{\n\"a3\":{\n\"a4\":4}}}}", &map);
  EXPECT_TRUE(val.isObject());
  i1 = val.get_ptr("a1");
  EXPECT_TRUE(i1 != nullptr);
  it = map.find(i1);
  EXPECT_TRUE(it != map.end());
  EXPECT_EQ(it->second.key_range.begin.line, 0);
  EXPECT_EQ(it->second.value_range.begin.line, 0);
  i2 = i1->get_ptr("a2");
  EXPECT_TRUE(i2 != nullptr);
  it = map.find(i2);
  EXPECT_TRUE(it != map.end());
  EXPECT_EQ(it->second.key_range.begin.line, 1);
  EXPECT_EQ(it->second.value_range.begin.line, 1);
  i3 = i2->get_ptr("a3");
  EXPECT_TRUE(i3 != nullptr);
  it = map.find(i3);
  EXPECT_TRUE(it != map.end());
  EXPECT_EQ(it->second.key_range.begin.line, 2);
  EXPECT_EQ(it->second.value_range.begin.line, 2);
  i4 = i3->get_ptr("a4");
  EXPECT_TRUE(i4 != nullptr);
  it = map.find(i4);
  EXPECT_TRUE(it != map.end());
  EXPECT_EQ(it->second.key_range.begin.line, 3);
  EXPECT_EQ(it->second.value_range.begin.line, 3);
  EXPECT_EQ(i4->asInt(), 4);
  map.clear();
}

TEST(Json, DuplicateKeys) {
  dynamic obj = dynamic::object("a", 1);
  EXPECT_EQ(obj, parseJson("{\"a\": 1}"));

  // Default behavior keeps *last* value.
  EXPECT_EQ(obj, parseJson("{\"a\": 2, \"a\": 1}"));
  EXPECT_THROW(
      parseJson("{\"a\": 2, \"a\": 1}", {.validate_keys = true}), parse_error);
}

TEST(Json, ParseConvertInt) {
  EXPECT_THROW(parseJson("{2: 4}"), parse_error);
  dynamic obj = dynamic::object("2", 4);
  EXPECT_EQ(obj, parseJson("{2: 4}", {.convert_int_keys = true}));
  EXPECT_THROW(
      parseJson("{2: 4, \"2\": 5}", {.convert_int_keys = true}), parse_error);
  EXPECT_THROW(
      parseJson("{2: 4, 2: 5}", {.convert_int_keys = true}), parse_error);
}

TEST(Json, PrintConvertInt) {
  dynamic obj = dynamic::object(2, 4);
  EXPECT_THROW(toJson(obj), print_error);
  EXPECT_EQ(
      "{2:4}", folly::json::serialize(obj, {.allow_non_string_keys = true}));
  EXPECT_EQ(
      "{\"2\":4}", folly::json::serialize(obj, {.convert_int_keys = true}));
  EXPECT_EQ(
      "{\"2\":4}",
      folly::json::serialize(
          obj,
          {
              .allow_non_string_keys = true,
              .convert_int_keys = true,
          }));

  obj["2"] = 5; // Would lead to duplicate keys in output JSON.
  EXPECT_THROW(
      folly::json::serialize(
          obj,
          {
              .allow_non_string_keys = true,
              .convert_int_keys = true,
          }),
      print_error);
}

TEST(Json, ParseTrailingComma) {
  folly::json::serialization_opts on, off;
  on.allow_trailing_comma = true;
  off.allow_trailing_comma = false;

  dynamic arr = dynamic::array(1, 2);
  EXPECT_EQ(arr, parseJson("[1, 2]", on));
  EXPECT_EQ(arr, parseJson("[1, 2,]", on));
  EXPECT_EQ(arr, parseJson("[1, 2, ]", on));
  EXPECT_EQ(arr, parseJson("[1, 2 , ]", on));
  EXPECT_EQ(arr, parseJson("[1, 2 ,]", on));
  EXPECT_THROW(parseJson("[1, 2,]", off), parse_error);

  dynamic obj = dynamic::object("a", 1);
  EXPECT_EQ(obj, parseJson("{\"a\": 1}", on));
  EXPECT_EQ(obj, parseJson("{\"a\": 1,}", on));
  EXPECT_EQ(obj, parseJson("{\"a\": 1, }", on));
  EXPECT_EQ(obj, parseJson("{\"a\": 1 , }", on));
  EXPECT_EQ(obj, parseJson("{\"a\": 1 ,}", on));
  EXPECT_THROW(parseJson("{\"a\":1,}", off), parse_error);
}

TEST(Json, BoolConversion) {
  EXPECT_TRUE(parseJson("42").asBool());
}

TEST(Json, JavascriptSafe) {
  auto badDouble = int64_t((1ULL << 63ULL) + 1);
  dynamic badDyn = badDouble;
  EXPECT_EQ(folly::toJson(badDouble), folly::to<std::string>(badDouble));
  folly::json::serialization_opts opts;
  opts.javascript_safe = true;
  EXPECT_ANY_THROW(folly::json::serialize(badDouble, opts));

  auto okDouble = int64_t(1ULL << 63ULL);
  dynamic okDyn = okDouble;
  EXPECT_EQ(folly::toJson(okDouble), folly::to<std::string>(okDouble));
}

TEST(Json, Produce) {
  auto value = parseJson(R"( "f\"oo" )");
  EXPECT_EQ(toJson(value), R"("f\"oo")");
  value = parseJson("\"Control code: \001 \002 \x1f\"");
  EXPECT_EQ(toJson(value), R"("Control code: \u0001 \u0002 \u001f")");

  // We're not allowed to have non-string keys in json.
  EXPECT_THROW(
      toJson(dynamic::object("abc", "xyz")(42.33, "asd")), print_error);

  // Check Infinity/Nan
  folly::json::serialization_opts opts;
  opts.allow_nan_inf = true;
  EXPECT_EQ("Infinity", folly::json::serialize(parseJson("Infinity"), opts));
  EXPECT_EQ("NaN", folly::json::serialize(parseJson("NaN"), opts));
}

TEST(Json, PrintExceptionErrorMessages) {
  folly::json::serialization_opts opts;
  opts.allow_nan_inf = false;
  try {
    const char* jsonWithInfValue =
        "[{}, {}, {\"outerKey\":{\"innerKey\": Infinity}}]";
    EXPECT_THROW(
        toJson(folly::json::serialize(parseJson(jsonWithInfValue), opts)),
        print_error);

    toJson(folly::json::serialize(parseJson(jsonWithInfValue), opts));
  } catch (const print_error& error) {
    EXPECT_EQ(
        std::string(
            "folly::toJson: JSON object value was an INF when serializing value at 2->\"outerKey\"->\"innerKey\""),
        error.what());
  }

  try {
    const char* jsonWithNanValue = "[{\"outerKey\":{\"innerKey\": NaN}}]";
    EXPECT_THROW(
        toJson(folly::json::serialize(parseJson(jsonWithNanValue), opts)),
        print_error);
    toJson(folly::json::serialize(parseJson(jsonWithNanValue), opts));
  } catch (const print_error& error) {
    EXPECT_EQ(
        std::string(
            "folly::toJson: JSON object value was a NaN when serializing value at 0->\"outerKey\"->\"innerKey\""),
        error.what());
  }
  try {
    const dynamic jsonWithNonStringKey(
        dynamic::object("abc", "xyz")(42.33, "asd"));
    EXPECT_THROW(toJson(jsonWithNonStringKey), print_error);
    toJson(jsonWithNonStringKey);
  } catch (const print_error& error) {
    EXPECT_EQ(
        std::string(
            "folly::toJson: JSON object key 42.33 was not a string when serializing key at 42.33"),
        error.what());
  }
}

TEST(Json, JsonEscape) {
  folly::json::serialization_opts opts;
  EXPECT_EQ(
      folly::json::serialize("\b\f\n\r\x01\t\\\"/\v\a", opts),
      R"("\b\f\n\r\u0001\t\\\"/\u000b\u0007")");
}

TEST(Json, EscapeCornerCases) {
  // The escaping logic uses some bitwise operations to determine
  // which bytes need escaping 8 bytes at a time. Test that this logic
  // is correct regardless of positions by planting 2 characters that
  // may need escaping at each possible position and checking the
  // result, for varying string lengths.

  folly::json::serialization_opts opts;
  opts.validate_utf8 = true;

  std::string s;
  std::string expected;
  for (bool ascii : {true, false}) {
    opts.encode_non_ascii = ascii;

    for (size_t len = 2; len < 32; ++len) {
      for (size_t i = 0; i < len; ++i) {
        for (size_t j = 0; j < len; ++j) {
          if (i == j) {
            continue;
          }

          s.clear();
          expected.clear();

          expected.push_back('"');
          for (size_t pos = 0; pos < len; ++pos) {
            if (pos == i) {
              s.push_back('\\');
              expected.append("\\\\");
            } else if (pos == j) {
              s.append("\xe2\x82\xac");
              expected.append(ascii ? "\\u20ac" : "\xe2\x82\xac");
            } else {
              s.push_back('x');
              expected.push_back('x');
            }
          }
          expected.push_back('"');

          EXPECT_EQ(folly::json::serialize(s, opts), expected) << ascii;
        }
      }
    }
  }
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
      folly::json::serialize("\x1f\xe2\x82\xac", opts), R"("\u001f\u20ac")");
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

  // Allow 4 byte encodings, escape using 2 UTF-16 surrogate pairs.
  // "\xf0\x9f\x8d\x80" is Unicode Character 'FOUR LEAF CLOVER' (U+1F340)
  // >>> json.dumps({"a": u"\U0001F340"})
  // '{"a": "\\ud83c\\udf40"}'
  EXPECT_EQ(
      folly::json::serialize("\xf0\x9f\x8d\x80", opts), R"("\ud83c\udf40")");
  // Longer than 4 byte encodings
  EXPECT_ANY_THROW(folly::json::serialize("\xed\xaf\xbf\xed\xbf\xbf", opts));
}

TEST(Json, UTF8Retention) {
  // test retention with valid utf8 strings
  std::string input = reinterpret_cast<const char*>(u8"\u2665");
  std::string jsonInput = folly::toJson(input);
  std::string output = folly::parseJson(jsonInput).asString();
  std::string jsonOutput = folly::toJson(output);

  EXPECT_EQ(input, output);
  EXPECT_EQ(jsonInput, jsonOutput);

  // test retention with invalid utf8 - note that non-ascii chars are retained
  // as is, and no unicode encoding is attempted so no exception is thrown.
  EXPECT_EQ(
      folly::toJson("a\xe0\xa0\x80z\xc0\x80"), "\"a\xe0\xa0\x80z\xc0\x80\"");
}

TEST(Json, UTF8EncodeNonAsciiRetention) {
  folly::json::serialization_opts opts;
  opts.encode_non_ascii = true;

  // test encode_non_ascii valid utf8 strings
  std::string input = reinterpret_cast<const char*>(u8"\u2665");
  std::string jsonInput = folly::json::serialize(input, opts);
  std::string output = folly::parseJson(jsonInput).asString();
  std::string jsonOutput = folly::json::serialize(output, opts);

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
      folly::json::serialize("a\xe0\xa0\x80z", opts), "\"a\xe0\xa0\x80z\"");
  EXPECT_EQ(
      folly::json::serialize("a\xe0\xa0\x80m\xc2\x80z", opts),
      "\"a\xe0\xa0\x80m\xc2\x80z\"");

  // test validate_utf8 with invalid utf8
  EXPECT_ANY_THROW(folly::json::serialize("a\xe0\xa0\x80z\xc0\x80", opts));
  EXPECT_ANY_THROW(folly::json::serialize("a\xe0\xa0\x80z\xe0\x80\x80", opts));
  // not a valid unicode because it is larger than the max 0x10FFFF code-point
  EXPECT_ANY_THROW(folly::json::serialize("\xF6\x8D\x9B\xBC", opts));

  opts.skip_invalid_utf8 = true;
  EXPECT_EQ(
      folly::json::serialize("a\xe0\xa0\x80z\xc0\x80", opts),
      reinterpret_cast<const char*>(u8"\"a\xe0\xa0\x80z\ufffd\ufffd\""));
  EXPECT_EQ(
      folly::json::serialize("a\xe0\xa0\x80z\xc0\x80\x80", opts),
      reinterpret_cast<const char*>(u8"\"a\xe0\xa0\x80z\ufffd\ufffd\ufffd\""));
  EXPECT_EQ(
      folly::json::serialize("z\xc0\x80z\xe0\xa0\x80", opts),
      reinterpret_cast<const char*>(u8"\"z\ufffd\ufffdz\xe0\xa0\x80\""));
  EXPECT_EQ(
      folly::json::serialize("\xF6\x8D\x9B\xBC", opts),
      reinterpret_cast<const char*>(u8"\"\ufffd\ufffd\ufffd\ufffd\""));
  EXPECT_EQ(
      folly::json::serialize("invalid\xF6\x8D\x9B\xBCinbetween", opts),
      reinterpret_cast<const char*>(
          u8"\"invalid\ufffd\ufffd\ufffd\ufffdinbetween\""));

  opts.encode_non_ascii = true;
  EXPECT_EQ(
      folly::json::serialize("a\xe0\xa0\x80z\xc0\x80", opts),
      "\"a\\u0800z\\ufffd\\ufffd\"");
  EXPECT_EQ(
      folly::json::serialize("a\xe0\xa0\x80z\xc0\x80\x80", opts),
      "\"a\\u0800z\\ufffd\\ufffd\\ufffd\"");
  EXPECT_EQ(
      folly::json::serialize("z\xc0\x80z\xe0\xa0\x80", opts),
      "\"z\\ufffd\\ufffdz\\u0800\"");
}

TEST(Json, ParseNonStringKeys) {
  // test string keys
  EXPECT_EQ("a", parseJson("{\"a\":[]}").items().begin()->first.asString());

  // check that we don't allow non-string keys as this violates the
  // strict JSON spec (though it is emitted by the output of
  // folly::dynamic with operator <<).
  EXPECT_THROW(parseJson("{1:[]}"), parse_error);

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

TEST(Json, ParseDoubleFallback) {
  // default behavior
  EXPECT_THROW(
      parseJson("{\"a\":847605071342477600000000000000}"), std::range_error);
  EXPECT_THROW(parseJson("{\"a\":-9223372036854775809}"), std::range_error);
  EXPECT_THROW(parseJson("{\"a\":9223372036854775808}"), std::range_error);
  EXPECT_EQ(
      std::numeric_limits<int64_t>::min(),
      parseJson("{\"a\":-9223372036854775808}")
          .items()
          .begin()
          ->second.asInt());
  EXPECT_EQ(
      std::numeric_limits<int64_t>::max(),
      parseJson("{\"a\":9223372036854775807}").items().begin()->second.asInt());
  // with double_fallback numbers outside int64_t range are parsed as double
  folly::json::serialization_opts opts;
  opts.double_fallback = true;
  EXPECT_EQ(
      847605071342477600000000000000.0,
      parseJson("{\"a\":847605071342477600000000000000}", opts)
          .items()
          .begin()
          ->second.asDouble());
  EXPECT_EQ(
      847605071342477600000000000000.0,
      parseJson("{\"a\": 847605071342477600000000000000}", opts)
          .items()
          .begin()
          ->second.asDouble());
  EXPECT_EQ(
      847605071342477600000000000000.0,
      parseJson("{\"a\":847605071342477600000000000000 }", opts)
          .items()
          .begin()
          ->second.asDouble());
  EXPECT_EQ(
      847605071342477600000000000000.0,
      parseJson("{\"a\": 847605071342477600000000000000 }", opts)
          .items()
          .begin()
          ->second.asDouble());
  // show that some precision gets lost
  EXPECT_EQ(
      847605071342477612345678900000.0,
      parseJson("{\"a\":847605071342477612345678912345}", opts)
          .items()
          .begin()
          ->second.asDouble());
  EXPECT_DOUBLE_EQ(
      -9223372036854775809.0,
      parseJson("{\"a\":-9223372036854775809}", opts) // first smaller than min
          .items()
          .begin()
          ->second.asDouble());
  EXPECT_DOUBLE_EQ(
      9223372036854775808.0,
      parseJson("{\"a\":9223372036854775808}", opts) // first larger than max
          .items()
          .begin()
          ->second.asDouble());
  EXPECT_DOUBLE_EQ(
      -10000000000000000000.0,
      parseJson("{\"a\":-10000000000000000000}", opts) // minus + 20 digits
          .items()
          .begin()
          ->second.asDouble());
  EXPECT_DOUBLE_EQ(
      10000000000000000000.0,
      parseJson("{\"a\":10000000000000000000}", opts) // 20 digits
          .items()
          .begin()
          ->second.asDouble());
  // numbers within int64_t range are deserialized as int64_t, no loss
  EXPECT_EQ(
      std::numeric_limits<int64_t>::min(),
      parseJson("{\"a\":-9223372036854775808}", opts)
          .items()
          .begin()
          ->second.asInt());
  EXPECT_EQ(
      std::numeric_limits<int64_t>::max(),
      parseJson("{\"a\":9223372036854775807}", opts)
          .items()
          .begin()
          ->second.asInt());
  EXPECT_EQ(
      INT64_C(-1234567890123456789),
      parseJson("{\"a\":-1234567890123456789}", opts) // minus + 19 digits
          .items()
          .begin()
          ->second.asInt());
  EXPECT_EQ(
      INT64_C(1234567890123456789),
      parseJson("{\"a\":1234567890123456789}", opts) // 19 digits
          .items()
          .begin()
          ->second.asInt());
  EXPECT_EQ(
      INT64_C(-123456789012345678),
      parseJson("{\"a\":-123456789012345678}", opts) // minus + 18 digits
          .items()
          .begin()
          ->second.asInt());
  EXPECT_EQ(
      INT64_C(123456789012345678),
      parseJson("{\"a\":123456789012345678}", opts) // 18 digits
          .items()
          .begin()
          ->second.asInt());
  EXPECT_EQ(
      toJson(parseJson(R"({"a":-9223372036854775808})", opts)),
      R"({"a":-9223372036854775808})");
  EXPECT_EQ(
      toJson(parseJson(R"({"a":9223372036854775807})", opts)),
      R"({"a":9223372036854775807})");
  EXPECT_EQ(
      toJson(parseJson(R"({"a":-1234567890123456789})", opts)),
      R"({"a":-1234567890123456789})");
  EXPECT_EQ(
      toJson(parseJson(R"({"a":1234567890123456789})", opts)),
      R"({"a":1234567890123456789})");
  EXPECT_EQ(
      toJson(parseJson(R"({"a":-123456789012345678})", opts)),
      R"({"a":-123456789012345678})");
  EXPECT_EQ(
      toJson(parseJson(R"({"a":123456789012345678})", opts)),
      R"({"a":123456789012345678})");
}

TEST(Json, ParseNumbersAsStrings) {
  folly::json::serialization_opts opts;
  opts.parse_numbers_as_strings = true;
  auto parse = [&](std::string number) {
    return parseJson(number, opts).asString();
  };

  EXPECT_EQ("0", parse("0"));
  EXPECT_EQ("1234", parse("1234"));
  EXPECT_EQ("3.00", parse("3.00"));
  EXPECT_EQ("3.14", parse("3.14"));
  EXPECT_EQ("0.1234", parse("0.1234"));
  EXPECT_EQ("0.0", parse("0.0"));
  EXPECT_EQ(
      "46845131213548676854213265486468451312135486768542132",
      parse("46845131213548676854213265486468451312135486768542132"));
  EXPECT_EQ(
      "-468451312135486768542132654864684513121354867685.5e4",
      parse("-468451312135486768542132654864684513121354867685.5e4"));
  EXPECT_EQ("6.62607004e-34", parse("6.62607004e-34"));
  EXPECT_EQ("6.62607004E+34", parse("6.62607004E+34"));
  EXPECT_EQ("Infinity", parse("Infinity"));
  EXPECT_EQ("-Infinity", parse("-Infinity"));
  EXPECT_EQ("NaN", parse("NaN"));

  EXPECT_THROW(parse("ThisIsWrong"), parse_error);
  EXPECT_THROW(parse("34-2"), parse_error);
  EXPECT_THROW(parse(""), parse_error);
  EXPECT_THROW(parse("-"), parse_error);
  EXPECT_THROW(parse("34-e2"), parse_error);
  EXPECT_THROW(parse("34e2.4"), parse_error);
  EXPECT_THROW(parse("infinity"), parse_error);
  EXPECT_THROW(parse("nan"), parse_error);
}

TEST(Json, SortKeys) {
  folly::json::serialization_opts opts_on, opts_off, opts_custom_sort;
  opts_on.sort_keys = true;
  opts_off.sort_keys = false;

  opts_custom_sort.sort_keys = false; // should not be required
  opts_custom_sort.sort_keys_by = [](folly::dynamic const& a,
                                     folly::dynamic const& b) {
    // just an inverse sort
    return b < a;
  };

  // clang-format off
  dynamic value = dynamic::object
    ("foo", "bar")
    ("junk", 12)
    ("another", 32.2)
    ("a",
      dynamic::array(
          dynamic::object("a", "b")("c", "d"),
          12.5,
          "Yo Dawg",
          dynamic::array("heh"),
          nullptr));
  // clang-format on

  // dynamic object uses F14NodeMap which may randomize the table iteration
  // order; consequently, we must force the table iteration order to be
  // different from sorted order so that we can deterministically test sorting
  // below
  auto get_top_keys = [&] {
    std::vector<std::string> top_keys;
    for (auto const& key : value.keys()) {
      top_keys.push_back(key.asString());
    }
    return top_keys;
  };
  std::vector<std::string> sorted_top_keys = get_top_keys();
  std::sort(sorted_top_keys.begin(), sorted_top_keys.end());
  while (get_top_keys() == sorted_top_keys) {
    for (size_t i = 0; i < 64; ++i) {
      value.insert(folly::to<std::string>("fake-", i), i);
    }
    for (size_t i = 0; i < 64; ++i) {
      value.erase(folly::to<std::string>("fake-", i));
    }
  }

  std::string sorted_keys =
      R"({"a":[{"a":"b","c":"d"},12.5,"Yo Dawg",["heh"],null],)"
      R"("another":32.2,"foo":"bar","junk":12})";

  std::string inverse_sorted_keys =
      R"({"junk":12,"foo":"bar","another":32.2,)"
      R"("a":[{"c":"d","a":"b"},12.5,"Yo Dawg",["heh"],null]})";

  EXPECT_EQ(value, parseJson(folly::json::serialize(value, opts_on)));
  EXPECT_EQ(value, parseJson(folly::json::serialize(value, opts_off)));
  EXPECT_EQ(value, parseJson(folly::json::serialize(value, opts_custom_sort)));

  EXPECT_EQ(sorted_keys, folly::json::serialize(value, opts_on));
  EXPECT_NE(sorted_keys, folly::json::serialize(value, opts_off));
  EXPECT_EQ(
      inverse_sorted_keys, folly::json::serialize(value, opts_custom_sort));
}

TEST(Json, PrettyPrintIndent) {
  folly::json::serialization_opts opts;
  opts.sort_keys = true;
  opts.pretty_formatting = true;
  opts.pretty_formatting_indent_width = 4;

  // clang-format off
  dynamic value = dynamic::object
    ("foo", "bar")
    ("nested",
      dynamic::object
        ("abc", "def")
        ("qrs", dynamic::array("tuv", 789))
        ("xyz", 123)
    )
    ("zzz", 456)
    ;
  // clang-format on

  std::string expected = R"({
    "foo": "bar",
    "nested": {
        "abc": "def",
        "qrs": [
            "tuv",
            789
        ],
        "xyz": 123
    },
    "zzz": 456
})";

  EXPECT_EQ(expected, folly::json::serialize(value, opts));
}

TEST(Json, PrintTo) {
  std::ostringstream oss;

  // clang-format off
  dynamic value = dynamic::object
    ("foo", "bar")
    ("junk", 12)
    ("another", 32.2)
    (true, false) // include non-string keys
    (false, true)
    (2, 3)
    (0, 1)
    (1, 2)
    (1.5, 2.25)
    (0.5, 0.25)
    (0, 1)
    (1, 2)
    ("a",
      dynamic::array(
        dynamic::object("a", "b")
                       ("c", "d"),
        12.5,
        "Yo Dawg",
        dynamic::array("heh"),
        nullptr
      )
    )
    ;
  // clang-format on

  std::string expected =
      R"({
  false: true,
  true: false,
  0: 1,
  0.5: 0.25,
  1: 2,
  1.5: 2.25,
  2: 3,
  "a": [
    {
      "a": "b",
      "c": "d"
    },
    12.5,
    "Yo Dawg",
    [
      "heh"
    ],
    null
  ],
  "another": 32.2,
  "foo": "bar",
  "junk": 12
})";
  PrintTo(value, &oss);
  EXPECT_EQ(expected, oss.str());
}

TEST(Json, RecursionLimit) {
  std::string in;
  for (int i = 0; i < 1000; i++) {
    in.append("{\"x\":");
  }
  in.append("\"hi\"");
  for (int i = 0; i < 1000; i++) {
    in.append("}");
  }
  EXPECT_ANY_THROW(parseJson(in));

  folly::json::serialization_opts opts_high_recursion_limit;
  opts_high_recursion_limit.recursion_limit = 10000;
  parseJson(in, opts_high_recursion_limit);
}

TEST(Json, ExtraEscapes) {
  folly::json::serialization_opts opts;
  dynamic in = dynamic::object("a", "<foo@bar%baz?>");

  // Only in second index, only first bit of that index.
  opts.extra_ascii_to_escape_bitmap =
      folly::json::buildExtraAsciiToEscapeBitmap("@");
  auto serialized = folly::json::serialize(in, opts);
  EXPECT_EQ("{\"a\":\"<foo\\u0040bar%baz?>\"}", serialized);
  EXPECT_EQ(in, folly::parseJson(serialized));

  // Only last bit.
  opts.extra_ascii_to_escape_bitmap =
      folly::json::buildExtraAsciiToEscapeBitmap("?");
  serialized = folly::json::serialize(in, opts);
  EXPECT_EQ("{\"a\":\"<foo@bar%baz\\u003f>\"}", serialized);
  EXPECT_EQ(in, folly::parseJson(serialized));

  // Multiple bits.
  opts.extra_ascii_to_escape_bitmap =
      folly::json::buildExtraAsciiToEscapeBitmap("<%@?");
  serialized = folly::json::serialize(in, opts);
  EXPECT_EQ("{\"a\":\"\\u003cfoo\\u0040bar\\u0025baz\\u003f>\"}", serialized);
  EXPECT_EQ(in, folly::parseJson(serialized));

  // Non-ASCII
  in = dynamic::object("a", "a\xe0\xa0\x80z\xc0\x80");
  opts.extra_ascii_to_escape_bitmap =
      folly::json::buildExtraAsciiToEscapeBitmap("@");
  serialized = folly::json::serialize(in, opts);
  EXPECT_EQ("{\"a\":\"a\xe0\xa0\x80z\xc0\x80\"}", serialized);
  EXPECT_EQ(in, folly::parseJson(serialized));
}

TEST(Json, CharsToUnicodeEscape) {
  auto testPair = [](std::array<uint64_t, 2> arr, uint64_t zero, uint64_t one) {
    EXPECT_EQ(zero, arr[0]);
    EXPECT_EQ(one, arr[1]);
  };

  testPair(folly::json::buildExtraAsciiToEscapeBitmap(""), 0, 0);

  // ?=63
  testPair(folly::json::buildExtraAsciiToEscapeBitmap("?"), (1ULL << 63), 0);

  // @=64
  testPair(
      folly::json::buildExtraAsciiToEscapeBitmap("@"), 0, (1ULL << (64 - 64)));

  testPair(
      folly::json::buildExtraAsciiToEscapeBitmap("?@"),
      (1ULL << 63),
      (1ULL << (64 - 64)));
  testPair(
      folly::json::buildExtraAsciiToEscapeBitmap("@?"),
      (1ULL << 63),
      (1ULL << (64 - 64)));

  // duplicates
  testPair(
      folly::json::buildExtraAsciiToEscapeBitmap("@?@?"),
      (1ULL << 63),
      (1ULL << (64 - 64)));

  // ?=63, @=64, $=36
  testPair(
      folly::json::buildExtraAsciiToEscapeBitmap("?@$"),
      (1ULL << 63) | (1ULL << 36),
      (1ULL << (64 - 64)));

  // ?=63, $=36, @=64, !=33
  testPair(
      folly::json::buildExtraAsciiToEscapeBitmap("?@$!"),
      (1ULL << 63) | (1ULL << 36) | (1ULL << 33),
      (1ULL << (64 - 64)));

  // ?=63, $=36, @=64, !=33, ]=93
  testPair(
      folly::json::buildExtraAsciiToEscapeBitmap("?@$!]"),
      (1ULL << 63) | (1ULL << 36) | (1ULL << 33),
      (1ULL << (64 - 64)) | (1ULL << (93 - 64)));
}
