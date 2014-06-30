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
#include <folly/ExceptionWrapper.h>

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

TEST(ExceptionWrapper, try_and_catch_test) {
  std::string expected = "payload";

  // Catch rightmost matching exception type
  exception_wrapper ew = try_and_catch<std::exception, std::runtime_error>(
    [=]() {
      throw std::runtime_error(expected);
    });
  EXPECT_TRUE(ew.get());
  EXPECT_EQ(ew.get()->what(), expected);
  auto rep = dynamic_cast<std::runtime_error*>(ew.get());
  EXPECT_TRUE(rep);

  // Changing order is like catching in wrong order. Beware of this in your
  // code.
  auto ew2 = try_and_catch<std::runtime_error, std::exception>([=]() {
    throw std::runtime_error(expected);
  });
  EXPECT_TRUE(ew2.get());
  // We are catching a std::exception, not std::runtime_error.
  EXPECT_NE(ew2.get()->what(), expected);
  rep = dynamic_cast<std::runtime_error*>(ew2.get());
  EXPECT_FALSE(rep);

  // Catches even if not rightmost.
  auto ew3 = try_and_catch<std::exception, std::runtime_error>([]() {
    throw std::exception();
  });
  EXPECT_TRUE(ew3.get());
  EXPECT_NE(ew3.get()->what(), expected);
  rep = dynamic_cast<std::runtime_error*>(ew3.get());
  EXPECT_FALSE(rep);

  // If does not catch, throws.
  EXPECT_THROW(
    try_and_catch<std::runtime_error>([]() {
      throw std::exception();
    }),
    std::exception);
}
