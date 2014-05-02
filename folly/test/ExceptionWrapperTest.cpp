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
#include <stdexcept>
#include "folly/ExceptionWrapper.h"

using namespace folly;

// Tests that when we call throwException, the proper type is thrown (derived)
TEST(ExceptionWrapper, throw_test) {
  std::runtime_error e("payload");
  auto ew = make_exception_wrapper<std::runtime_error>(e);

  std::vector<exception_wrapper> container;
  container.push_back(ew);

  try {
    container[0].throwException();
  } catch (std::runtime_error& e) {
    std::string expected = "payload";
    std::string actual = e.what();
    EXPECT_EQ(expected, actual);
  }
}

TEST(ExceptionWrapper, boolean) {
  auto ew = exception_wrapper();
  EXPECT_FALSE(bool(ew));
  ew = make_exception_wrapper<std::runtime_error>("payload");
  EXPECT_TRUE(bool(ew));
}
