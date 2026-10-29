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

#include <folly/lang/source_location.h>

#include <string>

#include <folly/Conv.h>
#include <folly/portability/GTest.h>

TEST(SourceLocationTest, DefaultToString) {
  EXPECT_EQ(
      ":0 []", folly::source_location_to_string(folly::source_location{}));
}

TEST(SourceLocationTest, CurrentToString) {
  const auto location = folly::source_location::current();
  const auto expected = folly::to<std::string>(
      location.file_name(),
      ":",
      location.line(),
      " [",
      location.function_name(),
      "]");
  EXPECT_EQ(expected, folly::source_location_to_string(location));
}
