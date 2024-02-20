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
#include <folly/portability/GTest.h>

TEST(Json, StripComments) {
  auto testStr = folly::stripLeftMargin(R"JSON(
    {
      // comment
      "test": "foo", // comment
      "test2": "foo // bar", // more comments
      /*
      "test3": "baz"
      */
      "test4": "foo /* bar", /* comment */
      "te//": "foo",
      "te/*": "bar",
      "\\\"": "\\" /* comment */
    }
  )JSON");
  auto expectedStr = folly::stripLeftMargin(R"JSON(
    {
      
      "test": "foo", 
      "test2": "foo // bar", 
      


      "test4": "foo /* bar", 
      "te//": "foo",
      "te/*": "bar",
      "\\\"": "\\" 
    }
  )JSON");

  EXPECT_EQ(expectedStr, folly::json::stripComments(testStr));
}
