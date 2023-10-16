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

#include <folly/io/SocketOptionValue.h>

#include <limits>

#include <glog/logging.h>

#include <folly/Conv.h>
#include <folly/portability/GTest.h>

namespace folly {
namespace test {

TEST(SocketOptionValueTest, IntValue) {
  SocketOptionValue i1(13);
  int i = 13;
  SocketOptionValue i2(i);
  // from uint32_t, int64_t for backward compatibility
  uint32_t u = 15;
  SocketOptionValue i3(u);
  int64_t b = 15;
  SocketOptionValue i4(b);

  EXPECT_TRUE(i1.hasInt());
  EXPECT_FALSE(i1.hasString());
  EXPECT_EQ(i1.asInt(), 13);

  EXPECT_TRUE(i1 == i2);
  EXPECT_FALSE(i1 == i3);
  EXPECT_TRUE(i1 != i3);
  EXPECT_TRUE(i3 == i4);

  EXPECT_TRUE(i1 == 13);
  EXPECT_FALSE(i1 == 15);
  EXPECT_FALSE(i1 == "15");
  EXPECT_TRUE(i1 != 0);
  EXPECT_FALSE(i1 != 13);
}

TEST(ShutdownSocketSetTest, StringValue) {
  SocketOptionValue s1("folly");
  SocketOptionValue s2("folly");
  SocketOptionValue s3("yllof");

  EXPECT_TRUE(s1.hasString());
  EXPECT_FALSE(s1.hasInt());
  EXPECT_EQ(s1.asString(), "folly");

  EXPECT_TRUE(s1 == s2);
  EXPECT_FALSE(s1 == s3);

  EXPECT_TRUE(s1 == "folly");
  EXPECT_FALSE(s1 == "yllof");
  EXPECT_FALSE(s1 == 15);
}

TEST(ShutdownSocketSetTest, StringVsInt) {
  SocketOptionValue i1(13);
  SocketOptionValue s1("folly");

  EXPECT_FALSE(i1 == s1);
}

TEST(ShutdownSocketSetTest, ToString) {
  SocketOptionValue i1(13);
  SocketOptionValue iMin(std::numeric_limits<int>::min());
  SocketOptionValue iMax(std::numeric_limits<int>::max());
  SocketOptionValue s1("folly");

  EXPECT_EQ(folly::to<std::string>(i1), "13");
  EXPECT_EQ(
      folly::to<std::string>(iMin),
      folly::to<std::string>(std::numeric_limits<int>::min()));
  EXPECT_EQ(
      folly::to<std::string>(iMax),
      folly::to<std::string>(std::numeric_limits<int>::max()));
  EXPECT_EQ(folly::to<std::string>(s1), "folly");

  LOG(INFO) << "SocketOptionValue test: " << i1;
}

TEST(ShutdownSocketSetTest, Maps) {
  SocketOptionValue i1(13);
  SocketOptionValue i2(13);

  std::map<std::string, SocketOptionValue> m;
  m["key1"] = i1;
  m["key2"] = i2;
  i1 = i2;
  m["key3"] = std::move(i1);
}

} // namespace test
} // namespace folly
