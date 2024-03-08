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

#include <iostream>

#include <folly/gen/Base.h>
#include <folly/json/dynamic.h>
#include <folly/json/json.h>
#include <folly/portability/GFlags.h>
#include <folly/portability/GTest.h>

using folly::dynamic;
using folly::TypeError;

TEST(Dynamic, ArrayGenerator) {
  // Make sure arrays can be used with folly::gen.
  using namespace folly::gen;
  dynamic arr = dynamic::array(1, 2, 3, 4);
  EXPECT_EQ(from(arr) | take(3) | member(&dynamic::asInt) | sum, 6);
}

TEST(Dynamic, StringPtrs) {
  dynamic str = "12.0";
  dynamic num = 12.0;
  dynamic nullStr = folly::parseJson("\"foo\\u0000bar\"");

  EXPECT_EQ(0, strcmp(str.c_str(), "12.0"));
  EXPECT_EQ(0, strncmp(str.c_str(), "12.0", str.asString().length()));
  EXPECT_EQ(str.stringPiece(), "12.0");

  EXPECT_THROW(num.c_str(), TypeError);
  EXPECT_THROW(num.stringPiece(), TypeError);

  EXPECT_EQ(nullStr.stringPiece(), folly::StringPiece("foo\0bar", 7));

  nullStr.getString()[3] = '|';
  EXPECT_EQ(nullStr.stringPiece(), "foo|bar");
}

TEST(Dynamic, Getters) {
  dynamic dStr = folly::parseJson("\"foo\\u0000bar\"");
  dynamic dInt = 1;
  dynamic dDouble = 0.5;
  dynamic dBool = true;

  EXPECT_EQ(dStr.getString(), std::string("foo\0bar", 7));
  EXPECT_EQ(dInt.getInt(), 1);
  EXPECT_EQ(dDouble.getDouble(), 0.5);
  EXPECT_EQ(dBool.getBool(), true);

  dStr.getString()[3] = '|';
  EXPECT_EQ(dStr.getString(), "foo|bar");

  dInt.getInt() = 2;
  EXPECT_EQ(dInt.getInt(), 2);

  dDouble.getDouble() = 0.7;
  EXPECT_EQ(dDouble.getDouble(), 0.7);

  dBool.getBool() = false;
  EXPECT_EQ(dBool.getBool(), false);

  EXPECT_THROW(dStr.getInt(), TypeError);
  EXPECT_THROW(dStr.getDouble(), TypeError);
  EXPECT_THROW(dStr.getBool(), TypeError);

  EXPECT_THROW(dInt.getString(), TypeError);
  EXPECT_THROW(dInt.getDouble(), TypeError);
  EXPECT_THROW(dInt.getBool(), TypeError);

  EXPECT_THROW(dDouble.getString(), TypeError);
  EXPECT_THROW(dDouble.getInt(), TypeError);
  EXPECT_THROW(dDouble.getBool(), TypeError);

  EXPECT_THROW(dBool.getString(), TypeError);
  EXPECT_THROW(dBool.getInt(), TypeError);
  EXPECT_THROW(dBool.getDouble(), TypeError);
}

TEST(Dynamic, ViewTruthinessTest) {
  // An default constructed view is falsey.
  folly::const_dynamic_view view{};
  EXPECT_EQ((bool)view, false);

  dynamic d{nullptr};

  // A view of a dynamic that is null is still truthy.
  // Assigning a dynamic to a view sets it to that dynamic.
  view = d;
  EXPECT_EQ((bool)view, true);

  // reset() empties a view.
  view.reset();
  EXPECT_EQ((bool)view, false);
}

TEST(Dynamic, ViewDescendTruthinessTest) {
  folly::const_dynamic_view view{};

  dynamic obj = dynamic::object("key", "value");
  dynamic arr = dynamic::array("one", "two", "three");

  // Descending should return truthy views.
  view = obj;
  EXPECT_EQ((bool)view, true);
  view = view.descend("key");
  EXPECT_EQ((bool)view, true);

  view = arr;
  EXPECT_EQ((bool)view, true);
  view = view.descend(1);
  EXPECT_EQ((bool)view, true);
}

TEST(Dynamic, ViewDescendFailedTest) {
  folly::const_dynamic_view view{};

  dynamic nully{};
  dynamic obj = dynamic::object("key", "value");
  dynamic arr = dynamic::array("one", "two", "three");

  // Failed descents should return empty views.
  view = nully;
  EXPECT_EQ((bool)view, true);
  view = view.descend("not an object");
  EXPECT_EQ((bool)view, false);

  view = obj;
  EXPECT_EQ((bool)view, true);
  view = view.descend("missing key");
  EXPECT_EQ((bool)view, false);

  view = arr;
  EXPECT_EQ((bool)view, true);
  view = view.descend("not an object");
  EXPECT_EQ((bool)view, false);

  view = arr;
  EXPECT_EQ((bool)view, true);
  view = view.descend(5); // out of range
  EXPECT_EQ((bool)view, false);
}

TEST(Dynamic, ViewScalarsTest) {
  dynamic i = 5;
  dynamic str = "hello";
  double pi = 3.14;
  dynamic d = pi;
  dynamic b = true;

  folly::const_dynamic_view view{};

  view = i;
  EXPECT_EQ(view.int_or(0), 5);

  view = str;
  EXPECT_EQ(view.string_or("default"), "hello");

  view = d;
  EXPECT_EQ(view.double_or(0.7), d.asDouble());

  view = b;
  EXPECT_EQ(view.bool_or(false), true);
}

TEST(Dynamic, ViewScalarsWrongTest) {
  dynamic i = 5;
  dynamic str = "hello";
  double pi = 3.14;
  dynamic d = pi;
  dynamic b = true;

  folly::const_dynamic_view view{};

  view = i;
  EXPECT_EQ(view.string_or("default"), "default");

  view = str;
  EXPECT_EQ(view.double_or(pi), pi);

  view = d;
  EXPECT_EQ(view.bool_or(false), false);

  view = b;
  EXPECT_EQ(view.int_or(777), 777);
}

TEST(Dynamic, ViewDescendObjectOnceTest) {
  double d = 3.14;
  dynamic obj =
      dynamic::object("string", "hello")("int", 5)("double", d)("bool", true);
  folly::const_dynamic_view view{obj};

  EXPECT_EQ(view.descend("string").string_or(""), "hello");
  EXPECT_EQ(view.descend("int").int_or(0), 5);
  EXPECT_EQ(view.descend("double").double_or(0.0), d);
  EXPECT_EQ(view.descend("bool").bool_or(false), true);
}

TEST(Dynamic, ViewDescendObjectTwiceTest) {
  double d = 3.14;
  dynamic nested =
      dynamic::object("string", "hello")("int", 5)("double", d)("bool", true);
  dynamic wrapper = dynamic::object("wrapped", nested);

  folly::const_dynamic_view view{wrapper};

  EXPECT_EQ(view.descend("wrapped").descend("string").string_or(""), "hello");
  EXPECT_EQ(view.descend("wrapped").descend("int").int_or(0), 5);
  EXPECT_EQ(view.descend("wrapped").descend("double").double_or(0.0), d);
  EXPECT_EQ(view.descend("wrapped").descend("bool").bool_or(false), true);

  EXPECT_EQ(view.descend("wrapped", "string").string_or(""), "hello");
  EXPECT_EQ(view.descend("wrapped", "int").int_or(0), 5);
  EXPECT_EQ(view.descend("wrapped", "double").double_or(0.0), d);
  EXPECT_EQ(view.descend("wrapped", "bool").bool_or(false), true);
}

TEST(Dynamic, ViewDescendObjectMissingKeyTest) {
  double d = 3.14;
  dynamic nested =
      dynamic::object("string", "string")("int", 5)("double", d)("bool", true);
  dynamic wrapper = dynamic::object("wrapped", nested);

  folly::const_dynamic_view view{wrapper};

  EXPECT_EQ(view.descend("wrapped").descend("string").string_or(""), "string");
  EXPECT_EQ(view.descend("wrapped").descend("int").int_or(0), 5);
  EXPECT_EQ(view.descend("wrapped").descend("double").double_or(0.0), d);
  EXPECT_EQ(view.descend("wrapped").descend("bool").bool_or(false), true);

  EXPECT_EQ(view.descend("wrapped", "string").string_or(""), "string");
  EXPECT_EQ(view.descend("wrapped", "int").int_or(0), 5);
  EXPECT_EQ(view.descend("wrapped", "double").double_or(0.0), d);
  EXPECT_EQ(view.descend("wrapped", "bool").bool_or(false), true);
}

TEST(Dynamic, ViewDescendObjectAndArrayTest) {
  dynamic leaf = dynamic::object("key", "value");
  dynamic arr = dynamic::array(leaf);
  dynamic root = dynamic::object("arr", arr);

  folly::const_dynamic_view view{root};
  EXPECT_EQ(view.descend("arr", 0, "key").string_or(""), "value");
}

std::string make_long_string() {
  return std::string(100, 'a');
}

TEST(Dynamic, ViewMoveValuesTest) {
  dynamic leaf = dynamic::object("key", make_long_string());
  dynamic obj = dynamic::object("leaf", std::move(leaf));

  folly::dynamic_view view{obj};

  const dynamic value = view.descend("leaf").move_value_or(nullptr);
  EXPECT_TRUE(value.isObject());
  EXPECT_EQ(value.count("key"), 1);
  EXPECT_TRUE(value["key"].isString());
  EXPECT_EQ(value["key"].getString(), make_long_string());

  // Original dynamic should have a moved-from "leaf" key.
  EXPECT_EQ(obj.count("leaf"), 1);
  EXPECT_TRUE(obj["leaf"].isObject());
  EXPECT_EQ(obj["leaf"].count("key"), 0);
}

TEST(Dynamic, ViewMoveStringsTest) {
  dynamic obj = dynamic::object("long_string", make_long_string());

  folly::dynamic_view view{obj};

  std::string value = view.descend("long_string").move_string_or("");
  EXPECT_EQ(value, make_long_string());

  // Original dynamic should still have a "long_string" entry with moved-from
  // string value.
  EXPECT_EQ(obj.count("long_string"), 1);
  EXPECT_TRUE(obj["long_string"].isString());
  EXPECT_NE(obj["long_string"].getString(), make_long_string());
}

TEST(Dynamic, ViewMakerTest) {
  dynamic d{nullptr};

  auto view = folly::make_dynamic_view(d);

  EXPECT_TRUE((std::is_same<decltype(view), folly::dynamic_view>::value));

  const dynamic cd{nullptr};

  auto cv = folly::make_dynamic_view(cd);

  EXPECT_TRUE((std::is_same<decltype(cv), folly::const_dynamic_view>::value));

  // This should not compile, because you can't view temporaries.
  // auto fail = folly::make_dynamic_view(folly::dynamic{nullptr});
}

TEST(Dynamic, FormattedIO) {
  std::ostringstream out;
  dynamic doubl = 123.33;
  dynamic dint = 12;
  out << "0x" << std::hex << ++dint << ' ' << std::setprecision(1) << doubl
      << '\n';
  EXPECT_EQ(out.str(), "0xd 1e+02\n");

  out.str("");
  dynamic arrr = dynamic::array(1, 2, 3);
  out << arrr;
  EXPECT_EQ(out.str(), "[1,2,3]");

  out.str("");
  dynamic objy = dynamic::object("a", 12);
  out << objy;
  EXPECT_EQ(out.str(), R"({"a":12})");

  out.str("");
  dynamic objy2 = dynamic::array(
      objy, dynamic::object(12, "str"), dynamic::object(true, false));
  out << objy2;
  EXPECT_EQ(out.str(), R"([{"a":12},{12:"str"},{true:false}])");
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  return RUN_ALL_TESTS();
}
