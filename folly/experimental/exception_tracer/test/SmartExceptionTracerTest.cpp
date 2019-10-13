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

#include <folly/experimental/exception_tracer/SmartExceptionTracer.h>
#include <folly/portability/GTest.h>

using namespace folly::exception_tracer;

[[noreturn]] FOLLY_NOINLINE void testThrowException() {
  throw std::runtime_error("test exception");
}

TEST(SmartExceptionTracer, ExceptionPtr) {
  auto ew = folly::try_and_catch<std::exception>(testThrowException);
  auto info = getTrace(ew.to_exception_ptr());

  std::ostringstream ss;
  ss << info;
  ASSERT_TRUE(ss.str().find("testThrowException") != std::string::npos);
}

TEST(SmartExceptionTracer, ExceptionWrapper) {
  auto ew = folly::try_and_catch<std::exception>(testThrowException);
  auto info = getTrace(ew);

  std::ostringstream ss;
  ss << info;
  ASSERT_TRUE(ss.str().find("testThrowException") != std::string::npos);
}

TEST(SmartExceptionTracer, StdException) {
  try {
    testThrowException();
  } catch (std::exception& ex) {
    auto info = getTrace(ex);

    std::ostringstream ss;
    ss << info;
    ASSERT_TRUE(ss.str().find("testThrowException") != std::string::npos);
  }
}

TEST(SmartExceptionTracer, EmptyExceptionWrapper) {
  auto ew = folly::exception_wrapper();
  auto info = getTrace(ew);

  std::ostringstream ss;
  ss << info;
  ASSERT_TRUE(
      ss.str().find("Exception type: (unknown type) (0 frames)") !=
      std::string::npos);
}

TEST(SmartExceptionTracer, InvalidException) {
  try {
    throw 10;
  } catch (...) {
    auto info = getTrace(std::current_exception());

    std::ostringstream ss;
    ss << info;
    ASSERT_TRUE(
        ss.str().find("Exception type: (unknown type) (0 frames)") !=
        std::string::npos);
  }
}

TEST(SmartExceptionTracer, UnthrownException) {
  std::runtime_error ex("unthrown");
  auto info = getTrace(ex);

  std::ostringstream ss;
  ss << info;
  ASSERT_TRUE(
      ss.str().find("Exception type: std::runtime_error (0 frames)") !=
      std::string::npos);
}
