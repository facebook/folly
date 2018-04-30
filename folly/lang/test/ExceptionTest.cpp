/*
 * Copyright 2018-present Facebook, Inc.
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

#include <folly/lang/Exception.h>

#include <folly/portability/GTest.h>

namespace {

class MyException : public std::exception {
 private:
  char const* what_;

 public:
  explicit MyException(char const* what) : MyException(what, 0) {}
  MyException(char const* what, std::size_t strip) : what_(what + strip) {}

  char const* what() const noexcept override {
    return what_;
  }
};

} // namespace

class ThrowExceptionTest : public testing::Test {};

TEST_F(ThrowExceptionTest, example) {
  try {
    folly::throw_exception<MyException>("hello world"); // 1-arg form
    ADD_FAILURE();
  } catch (MyException const& ex) {
    EXPECT_STREQ("hello world", ex.what());
  }

  try {
    folly::throw_exception<MyException>("hello world", 6); // 2-arg form
    ADD_FAILURE();
  } catch (MyException const& ex) {
    EXPECT_STREQ("world", ex.what());
  }
}
