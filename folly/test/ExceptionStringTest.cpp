/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <folly/ExceptionString.h>

#include <folly/Portability.h>
#include <folly/portability/GTest.h>

class ExceptionStringTest : public testing::Test {};

TEST_F(ExceptionStringTest, exception_ptr) {
  auto ptr = std::make_exception_ptr(std::out_of_range("foo"));
  auto expected = "std::out_of_range: foo";
  auto actual = folly::exceptionStr(ptr).toStdString();
  EXPECT_EQ(expected, actual);
}

TEST_F(ExceptionStringTest, exception_ptr_unknown) {
  auto ptr = std::make_exception_ptr(7);
  auto expected = folly::kIsLibstdcpp ? "int" : "<unknown exception>";
  auto actual = folly::exceptionStr(ptr).toStdString();
  EXPECT_EQ(expected, actual);
}
