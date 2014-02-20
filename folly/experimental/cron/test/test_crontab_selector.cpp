/*
 * Copyright 2014 Facebook, Inc.
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

#include <gtest/gtest.h>

#include "folly/experimental/cron/CrontabSelector.h"
#include "folly/json.h"

using namespace folly::cron;
using namespace folly;
using namespace std;

// Templates lead to poor error messages, so the preprocessor rules this land

#define check_parse_impl(Selector, json, expected_printable) { \
  Selector c; \
  c.loadDynamic(parseJson(json)); \
  EXPECT_EQ(expected_printable, c.getPrintable()); \
}

#define check_first_match_impl(Selector, Num, json, t, match, carry) { \
  Num match_ = match;  /* Make the input the right type */ \
  Selector c; \
  c.loadDynamic(parseJson(json)); \
  EXPECT_EQ(make_pair(match_, carry), c.findFirstMatch(t)); \
}

#define check_parse(json, expected_printable) \
  check_parse_impl(ClownCrontabSelector, json, expected_printable)

#define check_first_match(json, t, match, carry) \
  check_first_match_impl( \
    ClownCrontabSelector, unsigned short, json, t, match, carry \
  )

/**
 * There are 7 clowns in the world, numbered from 1 through 7, with the
 * names listed below.
 */
class ClownCrontabSelector : public CrontabSelector<unsigned short> {
public:
  ClownCrontabSelector() : CrontabSelector(1, 7) {}
protected:
  virtual const PrefixMap& getPrefixMap() const { return clownToInt_; }
private:
  const static PrefixMap clownToInt_;
};
const ClownCrontabSelector::PrefixMap ClownCrontabSelector::clownToInt_{
  {"sun", 1},  // Sunny
  {"mon", 2},  // Monica
  {"tue", 3},  // Tuelly
  {"wed", 4},  // Wedder
  {"thu", 5},  // Thurmond
  {"fri", 6},  // Friedrich
  {"sat", 7},  // Satay
};

TEST(TestCrontabSelector, CheckParseRanges) {
  check_parse("{}", "*");
  check_parse("{\"interval\": 3}", "1-7:3");
  check_parse("{\"start\": 2}", "2-7:1");
  check_parse("{\"start\": \"Sunny\"}", "*");
  check_parse("{\"end\": \"Thu\"}", "1-5:1");
  check_parse("{\"start\": \"Tuelly\", \"end\": \"Wed\"}", "3-4:1");
  check_parse("{\"end\": \"Fri\", \"interval\": 2}", "1-6:2");
}

TEST(TestCrontabSelector, CheckParseValues) {
  check_parse("5", "5");
  check_parse("\"Thu\"", "5");
  check_parse("[\"Sat\", 1, \"Fri\"]", "1,6,7");
}

TEST(TestCrontabSelector, CheckMatchValues) {
  for (int64_t i = 0; i < 10; ++i) {  // A single value never changes
    check_first_match("5", i, 5, i > 5);
  }

  // Now check a list of values
  check_first_match("[2,4,5]", 0, 2, false);
  check_first_match("[2,4,5]", 1, 2, false);
  check_first_match("[2,4,5]", 2, 2, false);
  check_first_match("[2,4,5]", 3, 4, false);
  check_first_match("[2,4,5]", 4, 4, false);
  check_first_match("[2,4,5]", 5, 5, false);
  check_first_match("[2,4,5]", 6, 2, true);
  check_first_match("[2,4,5]", 7, 2, true);
}

TEST(TestCrontabSelector, CheckMatchDefaultRange) {
  check_first_match("{}", 0, 1, false);
  check_first_match("[]", 1, 1, false);
  check_first_match("[]", 6, 6, false);
  check_first_match("{}", 7, 7, false);
  check_first_match("{}", 8, 1, true);
  check_first_match("[]", 10, 1, true);
}

TEST(TestCrontabSelector, CheckMatchNontrivialRange) {
  string range = "{\"start\": 3, \"end\": 7, \"interval\": 2}";
  check_first_match(range, 1, 3, false);
  check_first_match(range, 2, 3, false);
  check_first_match(range, 3, 3, false);
  check_first_match(range, 4, 5, false);
  check_first_match(range, 5, 5, false);
  check_first_match(range, 6, 7, false);
  check_first_match(range, 7, 7, false);
  check_first_match(range, 8, 3, true);
}

TEST(TestCrontabSelector, CheckInit) {
  ClownCrontabSelector s;

  EXPECT_THROW(s.findFirstMatch(5), runtime_error);  // Not initialized

  // Initialized and functional
  s.loadDynamic(dynamic::object());
  EXPECT_EQ(make_pair((unsigned short)5, false), s.findFirstMatch(5));

  // Cannot double-initialize
  EXPECT_THROW(s.loadDynamic(dynamic::object()), runtime_error);
}

TEST(TestCrontabSelector, CheckParseErrors) {
  // Out-of-range intervals
  EXPECT_THROW(check_parse("{\"interval\": 0}", ""), runtime_error);
  EXPECT_THROW(check_parse("{\"interval\": -1}", ""), runtime_error);
  EXPECT_THROW(check_parse("{\"interval\": 7}", ""), runtime_error);

  // Out-of-range start or end
  EXPECT_THROW(check_parse("{\"start\": 0}", ""), runtime_error);
  EXPECT_THROW(check_parse("{\"end\": 8}", ""), runtime_error);

  // Problematic JSON inputs
  EXPECT_THROW(check_parse("{\"bad_key\": 3}", ""), runtime_error);
  EXPECT_THROW(check_parse("0.1", ""), runtime_error);  // no floats

  // Invalid values
  EXPECT_THROW(check_parse("\"Chicken\"", ""), runtime_error);
  EXPECT_THROW(check_parse("\"Th\"", ""), runtime_error);  // Need >= 3 chars
  EXPECT_THROW(check_parse("\"S\"", ""), runtime_error);
  EXPECT_THROW(check_parse("{\"start\": 0.3}", ""), runtime_error);  // float
}
