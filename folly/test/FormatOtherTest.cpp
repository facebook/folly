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

#include <glog/logging.h>

#include <folly/FBVector.h>
#include <folly/FileUtil.h>
#include <folly/Format.h>
#include <folly/Portability.h>
#include <folly/dynamic.h>
#include <folly/json.h>
#include <folly/portability/GFlags.h>
#include <folly/portability/GTest.h>
#include <folly/small_vector.h>

FOLLY_GNU_DISABLE_WARNING("-Wdeprecated-declarations")

using namespace folly;

TEST(FormatOther, dynamic) {
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

namespace {

template <class T>
void testFormatSeq() {
  T v{10, 20, 30};
  EXPECT_EQ("30 10", sformat("{0[2]} {0[0]}", v));
  EXPECT_EQ("0020", sformat("{0[1]:04}", v));
  EXPECT_EQ("0020", svformat("{1:04}", v));
  EXPECT_EQ("10 20", svformat("{} {}", v));
  EXPECT_EQ("10 20 0030", svformat("{} {} {:04}", v));
}

} // namespace

TEST(FormatOther, fbvector) {
  testFormatSeq<fbvector<int>>();
}

TEST(FormatOther, smallVector) {
  testFormatSeq<small_vector<int, 2>>();
}

int main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  return RUN_ALL_TESTS();
}
