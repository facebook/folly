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

#include <folly/Format.h>
#include <folly/portability/GTest.h>

TEST(Format, demo) {
  std::string s1 = fmt::format("The answer to {} is {}", "life", 42);
  EXPECT_EQ(s1, "The answer to life is 42");

  std::string s2 = folly::sformat("{0}{0}{0}{0} Batman!", "na");
  EXPECT_EQ(s2, "nananana Batman!");

  EXPECT_EQ(folly::sformat("{:.2f}", 3.1415926535), "3.14");
}
