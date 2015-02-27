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

#include <boost/next_prior.hpp>
#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include <folly/Benchmark.h>
#include <folly/dynamic.h>
#include <folly/gen/Base.h>
#include <folly/json.h>

using folly::dynamic;
using folly::TypeError;

TEST(Dynamic, ObjectBasics) {
  dynamic obj = dynamic::object("a", false);
  EXPECT_EQ(obj.at("a"), false);
  EXPECT_EQ(obj.size(), 1);
  obj.insert("a", true);
  EXPECT_EQ(obj.size(), 1);
  EXPECT_EQ(obj.at("a"), true);
  obj.at("a") = nullptr;
  EXPECT_EQ(obj.size(), 1);
  EXPECT_TRUE(obj.at("a") == nullptr);

  dynamic newObject = dynamic::object;

  newObject["z"] = 12;
  EXPECT_EQ(newObject.size(), 1);
  newObject["a"] = true;
  EXPECT_EQ(newObject.size(), 2);

  EXPECT_EQ(*newObject.keys().begin(), newObject.items().begin()->first);
  EXPECT_EQ(*newObject.values().begin(), newObject.items().begin()->second);
  std::vector<std::pair<folly::fbstring, dynamic>> found;
  found.push_back(std::make_pair(
     newObject.keys().begin()->asString(),
     *newObject.values().begin()));

  EXPECT_EQ(*boost::next(newObject.keys().begin()),
            boost::next(newObject.items().begin())->first);
  EXPECT_EQ(*boost::next(newObject.values().begin()),
            boost::next(newObject.items().begin())->second);
  found.push_back(std::make_pair(
      boost::next(newObject.keys().begin())->asString(),
      *boost::next(newObject.values().begin())));

  std::sort(found.begin(), found.end());

  EXPECT_EQ("a", found[0].first);
  EXPECT_TRUE(found[0].second.asBool());

  EXPECT_EQ("z", found[1].first);
  EXPECT_EQ(12, found[1].second.asInt());

  dynamic obj2 = dynamic::object;
  EXPECT_TRUE(obj2.isObject());

  dynamic d3 = nullptr;
  EXPECT_TRUE(d3 == nullptr);
  d3 = dynamic::object;
  EXPECT_TRUE(d3.isObject());
  d3["foo"] = { 1, 2, 3 };
  EXPECT_EQ(d3.count("foo"), 1);

  d3[123] = 321;
  EXPECT_EQ(d3.at(123), 321);

  d3["123"] = 42;
  EXPECT_EQ(d3.at("123"), 42);
  EXPECT_EQ(d3.at(123), 321);

  dynamic objInsert = folly::dynamic::object();
  dynamic objA = folly::dynamic::object("1", "2");
  dynamic objB = folly::dynamic::object("1", "2");

  objInsert.insert("1", std::move(objA));
  objInsert.insert("1", std::move(objB));

  EXPECT_EQ(objInsert.find("1")->second.size(), 1);

  // We don't allow objects as keys in objects.
  EXPECT_ANY_THROW(newObject[d3] = 12);
}

TEST(Dynamic, ObjectErase) {
  dynamic obj = dynamic::object("key1", "val")
                               ("key2", "val2");
  EXPECT_EQ(obj.count("key1"), 1);
  EXPECT_EQ(obj.count("key2"), 1);
  EXPECT_EQ(obj.erase("key1"), 1);
  EXPECT_EQ(obj.count("key1"), 0);
  EXPECT_EQ(obj.count("key2"), 1);
  EXPECT_EQ(obj.erase("key1"), 0);
  obj["key1"] = 12;
  EXPECT_EQ(obj.count("key1"), 1);
  EXPECT_EQ(obj.count("key2"), 1);
  auto it = obj.find("key2");
  obj.erase(it);
  EXPECT_EQ(obj.count("key1"), 1);
  EXPECT_EQ(obj.count("key2"), 0);

  obj["asd"] = 42.0;
  obj["foo"] = 42.0;
  EXPECT_EQ(obj.size(), 3);
  auto ret = obj.erase(boost::next(obj.items().begin()), obj.items().end());
  EXPECT_TRUE(ret == obj.items().end());
  EXPECT_EQ(obj.size(), 1);
  obj.erase(obj.items().begin());
  EXPECT_TRUE(obj.empty());
}

TEST(Dynamic, ArrayErase) {
  dynamic arr = { 1, 2, 3, 4, 5, 6 };

  EXPECT_THROW(arr.erase(1), std::exception);
  EXPECT_EQ(arr.size(), 6);
  EXPECT_EQ(arr[0], 1);
  arr.erase(arr.begin());
  EXPECT_EQ(arr.size(), 5);

  arr.erase(boost::next(arr.begin()), boost::prior(arr.end()));
  EXPECT_EQ(arr.size(), 2);
  EXPECT_EQ(arr[0], 2);
  EXPECT_EQ(arr[1], 6);
}

TEST(Dynamic, StringBasics) {
  dynamic str = "hello world";
  EXPECT_EQ(11, str.size());
  EXPECT_FALSE(str.empty());
  str = "";
  EXPECT_TRUE(str.empty());
}

TEST(Dynamic, ArrayBasics) {
  dynamic array = { 1, 2, 3 };
  EXPECT_EQ(array.size(), 3);
  EXPECT_EQ(array.at(0), 1);
  EXPECT_EQ(array.at(1), 2);
  EXPECT_EQ(array.at(2), 3);

  EXPECT_ANY_THROW(array.at(3));

  array.push_back("foo");
  EXPECT_EQ(array.size(), 4);

  array.resize(12, "something");
  EXPECT_EQ(array.size(), 12);
  EXPECT_EQ(array[11], "something");
}

TEST(Dynamic, DeepCopy) {
  dynamic val = { "foo", "bar", { "foo1", "bar1" } };
  EXPECT_EQ(val.at(2).at(0), "foo1");
  EXPECT_EQ(val.at(2).at(1), "bar1");
  dynamic val2 = val;
  EXPECT_EQ(val2.at(2).at(0), "foo1");
  EXPECT_EQ(val2.at(2).at(1), "bar1");
  EXPECT_EQ(val.at(2).at(0), "foo1");
  EXPECT_EQ(val.at(2).at(1), "bar1");
  val2.at(2).at(0) = "foo3";
  val2.at(2).at(1) = "bar3";
  EXPECT_EQ(val.at(2).at(0), "foo1");
  EXPECT_EQ(val.at(2).at(1), "bar1");
  EXPECT_EQ(val2.at(2).at(0), "foo3");
  EXPECT_EQ(val2.at(2).at(1), "bar3");

  dynamic obj = dynamic::object("a", "b")
                               ("c", {"d", "e", "f"})
                               ;
  EXPECT_EQ(obj.at("a"), "b");
  dynamic obj2 = obj;
  obj2.at("a") = {1, 2, 3};
  EXPECT_EQ(obj.at("a"), "b");
  dynamic expected = {1, 2, 3};
  EXPECT_EQ(obj2.at("a"), expected);
}

TEST(Dynamic, Operator) {
  bool caught = false;
  try {
    dynamic d1 = dynamic::object;
    dynamic d2 = dynamic::object;
    auto foo = d1 < d2;
  } catch (std::exception const& e) {
    caught = true;
  }
  EXPECT_TRUE(caught);

  dynamic foo = "asd";
  dynamic bar = "bar";
  dynamic sum = foo + bar;
  EXPECT_EQ(sum, "asdbar");

  dynamic some = 12;
  dynamic nums = 4;
  dynamic math = some / nums;
  EXPECT_EQ(math, 3);
}

TEST(Dynamic, Conversions) {
  dynamic str = "12.0";
  EXPECT_EQ(str.asDouble(), 12.0);
  EXPECT_ANY_THROW(str.asInt());
  EXPECT_ANY_THROW(str.asBool());

  str = "12";
  EXPECT_EQ(str.asInt(), 12);
  EXPECT_EQ(str.asDouble(), 12.0);
  str = "0";
  EXPECT_EQ(str.asBool(), false);
  EXPECT_EQ(str.asInt(), 0);
  EXPECT_EQ(str.asDouble(), 0);
  EXPECT_EQ(str.asString(), "0");

  dynamic num = 12;
  EXPECT_EQ("12", num.asString());
  EXPECT_EQ(12.0, num.asDouble());
}

TEST(Dynamic, StringPtrs) {
  dynamic str = "12.0";
  dynamic num = 12.0;
  dynamic nullStr = folly::parseJson("\"foo\\u0000bar\"");

  EXPECT_EQ(0, strcmp(str.c_str(), "12.0"));
  EXPECT_EQ(0, strncmp(str.data(), "12.0", str.asString().length()));
  EXPECT_EQ(str.stringPiece(), "12.0");

  EXPECT_THROW(num.c_str(), TypeError);
  EXPECT_THROW(num.data(), TypeError);
  EXPECT_THROW(num.stringPiece(), TypeError);

  EXPECT_EQ(nullStr.stringPiece(), folly::StringPiece("foo\0bar", 7));

  nullStr.getString()[3] = '|';
  EXPECT_EQ(nullStr.stringPiece(), "foo|bar");
}

TEST(Dynamic, FormattedIO) {
  std::ostringstream out;
  dynamic doubl = 123.33;
  dynamic dint = 12;
  out << "0x" << std::hex << ++dint << ' ' << std::setprecision(1)
      << doubl << '\n';
  EXPECT_EQ(out.str(), "0xd 1e+02\n");

  out.str("");
  dynamic arrr = { 1, 2, 3 };
  out << arrr;
  EXPECT_EQ(out.str(), "[1,2,3]");

  out.str("");
  dynamic objy = dynamic::object("a", 12);
  out << objy;
  EXPECT_EQ(out.str(), R"({"a":12})");

  out.str("");
  dynamic objy2 = { objy, dynamic::object(12, "str"),
                          dynamic::object(true, false) };
  out << objy2;
  EXPECT_EQ(out.str(), R"([{"a":12},{12:"str"},{true:false}])");
}

TEST(Dynamic, GetSetDefaultTest) {
  dynamic d1 = dynamic::object("foo", "bar");
  EXPECT_EQ(d1.getDefault("foo", "baz"), "bar");
  EXPECT_EQ(d1.getDefault("quux", "baz"), "baz");

  dynamic d2 = dynamic::object("foo", "bar");
  EXPECT_EQ(d2.setDefault("foo", "quux"), "bar");
  d2.setDefault("bar", dynamic({})).push_back(42);
  EXPECT_EQ(d2["bar"][0], 42);

  dynamic d3 = dynamic::object, empty = dynamic::object;
  EXPECT_EQ(d3.getDefault("foo"), empty);
  d3.setDefault("foo")["bar"] = "baz";
  EXPECT_EQ(d3["foo"]["bar"], "baz");

  // we do not allow getDefault/setDefault on arrays
  dynamic d4 = dynamic({});
  EXPECT_ANY_THROW(d4.getDefault("foo", "bar"));
  EXPECT_ANY_THROW(d4.setDefault("foo", "bar"));
}

TEST(Dynamic, ObjectForwarding) {
  // Make sure dynamic::object can be constructed the same way as any
  // dynamic.
  dynamic d = dynamic::object("asd", {"foo", "bar"});
  dynamic d2 = dynamic::object("key2", {"value", "words"})
                              ("key", "value1");
}

TEST(Dynamic, GetPtr) {
  dynamic array = { 1, 2, "three" };
  EXPECT_TRUE(array.get_ptr(0));
  EXPECT_FALSE(array.get_ptr(3));
  EXPECT_EQ(dynamic("three"), *array.get_ptr(2));
  const dynamic& carray = array;
  EXPECT_EQ(dynamic("three"), *carray.get_ptr(2));

  dynamic object = dynamic::object("one", 1)("two", 2);
  EXPECT_TRUE(object.get_ptr("one"));
  EXPECT_FALSE(object.get_ptr("three"));
  EXPECT_EQ(dynamic(2), *object.get_ptr("two"));
  *object.get_ptr("one") = 11;
  EXPECT_EQ(dynamic(11), *object.get_ptr("one"));
  const dynamic& cobject = object;
  EXPECT_EQ(dynamic(2), *cobject.get_ptr("two"));
}

TEST(Dynamic, ArrayGenerator) {
  // Make sure arrays can be used with folly::gen.
  using namespace folly::gen;
  dynamic arr { 1, 2, 3, 4 };
  EXPECT_EQ(from(arr) | take(3) | member(&dynamic::asInt) | sum, 6);
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

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  if (FLAGS_benchmark) {
    folly::runBenchmarks();
  }
  return RUN_ALL_TESTS();
}
