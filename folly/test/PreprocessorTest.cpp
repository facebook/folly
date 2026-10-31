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

#include <folly/Preprocessor.h>

#include <string_view>

#include <folly/portability/GTest.h>

using namespace std::literals;

// Forwarding passes a present but empty argument list, which GCC's strict modes
// handle differently from an omitted one.
#define FOLLY_PP_TEST_NARGS(...) FOLLY_PP_DETAIL_NARGS(__VA_ARGS__)

static_assert(FOLLY_PP_DETAIL_NARGS() == 0);
static_assert(FOLLY_PP_TEST_NARGS() == 0);
static_assert(FOLLY_PP_DETAIL_NARGS(a) == 1);
static_assert(FOLLY_PP_DETAIL_NARGS((a, b)) == 1);
static_assert(FOLLY_PP_DETAIL_NARGS(a, b) == 2);
static_assert(FOLLY_PP_DETAIL_NARGS(, ) == 2);
static_assert(FOLLY_PP_DETAIL_NARGS(, a) == 2);
static_assert(FOLLY_PP_DETAIL_NARGS(a, ) == 2);
static_assert(FOLLY_PP_DETAIL_NARGS(a, b, c) == 3);
static_assert(FOLLY_PP_DETAIL_NARGS(a, b, c, d) == 4);
static_assert(FOLLY_PP_DETAIL_NARGS(a, b, c, d, e) == 5);
static_assert(FOLLY_PP_DETAIL_NARGS(a, b, c, d, e, f) == 6);
static_assert(FOLLY_PP_DETAIL_NARGS(a, b, c, d, e, f, g) == 7);
static_assert(FOLLY_PP_DETAIL_NARGS(a, b, c, d, e, f, g, h) == 8);
static_assert(FOLLY_PP_DETAIL_NARGS(a, b, c, d, e, f, g, h, i) == 9);
static_assert(FOLLY_PP_DETAIL_NARGS(a, b, c, d, e, f, g, h, i, j) == 10);
static_assert(FOLLY_PP_DETAIL_NARGS(a, b, c, d, e, f, g, h, i, j, k) == 11);
static_assert(FOLLY_PP_DETAIL_NARGS(a, b, c, d, e, f, g, h, i, j, k, l) == 12);
static_assert(
    FOLLY_PP_DETAIL_NARGS(a, b, c, d, e, f, g, h, i, j, k, l, m) == 13);
static_assert(
    FOLLY_PP_DETAIL_NARGS(a, b, c, d, e, f, g, h, i, j, k, l, m, n) == 14);
static_assert(
    FOLLY_PP_DETAIL_NARGS(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o) == 15);

#define FOLLY_PP_TEST_STR(x) "<" #x ">"
#define FOLLY_PP_TEST_FOR_EACH(...) \
  "" FOLLY_PP_FOR_EACH(FOLLY_PP_TEST_STR, __VA_ARGS__)

TEST(PreprocessorTest, forEach) {
  EXPECT_EQ(""sv, "" FOLLY_PP_FOR_EACH(FOLLY_PP_TEST_STR));
  EXPECT_EQ(""sv, FOLLY_PP_TEST_FOR_EACH());
  EXPECT_EQ("<a>"sv, FOLLY_PP_TEST_FOR_EACH(a));
  EXPECT_EQ("<a><b><c>"sv, FOLLY_PP_TEST_FOR_EACH(a, b, c));
  EXPECT_EQ("<><>"sv, FOLLY_PP_TEST_FOR_EACH(, ));
  EXPECT_EQ("<><a>"sv, FOLLY_PP_TEST_FOR_EACH(, a));
  EXPECT_EQ("<a><>"sv, FOLLY_PP_TEST_FOR_EACH(a, ));
  EXPECT_EQ("<(a, b)>"sv, FOLLY_PP_TEST_FOR_EACH((a, b)));
  EXPECT_EQ(
      "<a><b><c><d><e><f><g><h><i><j><k><l><m><n><o>"sv,
      FOLLY_PP_TEST_FOR_EACH(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o));
}

#define FOLLY_PP_TEST_STR_WITH(arg, x) #arg ":" #x ";"
#define FOLLY_PP_TEST_FOR_EACH_WITH(arg, ...) \
  "" FOLLY_PP_FOR_EACH_WITH(FOLLY_PP_TEST_STR_WITH, arg, __VA_ARGS__)

TEST(PreprocessorTest, forEachWith) {
  EXPECT_EQ(""sv, "" FOLLY_PP_FOR_EACH_WITH(FOLLY_PP_TEST_STR_WITH, f));
  EXPECT_EQ(""sv, FOLLY_PP_TEST_FOR_EACH_WITH(f));
  EXPECT_EQ(""sv, FOLLY_PP_TEST_FOR_EACH_WITH(f, ));
  EXPECT_EQ("f:a;"sv, FOLLY_PP_TEST_FOR_EACH_WITH(f, a));
  EXPECT_EQ("f:::std;"sv, FOLLY_PP_TEST_FOR_EACH_WITH(f, ::std));
  EXPECT_EQ("f:a;f:b;"sv, FOLLY_PP_TEST_FOR_EACH_WITH(f, a, b));
  EXPECT_EQ("f:;f:;"sv, FOLLY_PP_TEST_FOR_EACH_WITH(f, , ));
  EXPECT_EQ("f:;f:a;"sv, FOLLY_PP_TEST_FOR_EACH_WITH(f, , a));
  EXPECT_EQ("f:a;f:;"sv, FOLLY_PP_TEST_FOR_EACH_WITH(f, a, ));
  EXPECT_EQ(
      "f:a;f:b;f:c;f:d;f:e;f:f;f:g;f:h;f:i;f:j;f:k;f:l;f:m;f:n;f:o;"sv,
      FOLLY_PP_TEST_FOR_EACH_WITH(
          f, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o));
}
