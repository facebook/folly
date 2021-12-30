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

#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Task.h>
#include <folly/experimental/exception_tracer/SmartExceptionTracer.h>
#include <folly/portability/GTest.h>

#if FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF

using namespace folly::exception_tracer;

[[noreturn]] FOLLY_NOINLINE void testThrowException() {
  throw std::runtime_error("test exception");
}

TEST(SmartExceptionTracer, ExceptionPtr) {
  auto ew = folly::try_and_catch(testThrowException);
  auto info = getTrace(ew.to_exception_ptr());

  std::ostringstream ss;
  ss << info;
  ASSERT_TRUE(ss.str().find("testThrowException") != std::string::npos);
}

TEST(SmartExceptionTracer, ExceptionWrapper) {
  auto ew = folly::try_and_catch(testThrowException);
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
      ss.str().find("Exception type: (unknown type)") != std::string::npos);
}

TEST(SmartExceptionTracer, InvalidException) {
  try {
    throw 10;
  } catch (...) {
    auto info = getTrace(std::current_exception());

    std::ostringstream ss;
    ss << info;
    ASSERT_TRUE(
        ss.str().find("Exception type: (unknown type)") != std::string::npos);
  }
}

TEST(SmartExceptionTracer, UnthrownException) {
  std::runtime_error ex("unthrown");
  auto info = getTrace(ex);

  std::ostringstream ss;
  ss << info;
  ASSERT_TRUE(
      ss.str().find("Exception type: std::runtime_error") != std::string::npos);
}

namespace {

[[noreturn]] void funcD() {
  throw std::runtime_error("test ex");
}

FOLLY_NOINLINE folly::coro::Task<void> co_funcC() {
  funcD();
  co_return;
}

FOLLY_NOINLINE folly::coro::Task<void> co_funcB() {
  co_await co_funcC();
}

FOLLY_NOINLINE folly::coro::Task<void> co_funcA() {
  co_await co_funcB();
}

} // namespace

TEST(SmartExceptionTracer, AsyncStackTrace) {
  try {
    folly::coro::blockingWait(co_funcA());
    FAIL() << "exception should be thrown";
  } catch (const std::exception& ex) {
    ASSERT_EQ(std::string("test ex"), ex.what());
    auto info = folly::exception_tracer::getAsyncTrace(ex);
    LOG(INFO) << "async stack trace: " << info;

    std::ostringstream ss;
    ss << info;

    // Symbols should appear in this relative order
    std::vector<std::string> symbols{
        "funcD",
        "co_funcC",
        "co_funcB",
        "co_funcA",
    };
    std::vector<size_t> positions;
    for (const auto& symbol : symbols) {
      SCOPED_TRACE(symbol);
      auto pos = ss.str().find(symbol);
      ASSERT_NE(std::string::npos, pos);
      positions.push_back(pos);
    }
    for (size_t i = 0; i < positions.size() - 1; ++i) {
      ASSERT_LT(positions[i], positions[i + 1]);
    }
  }
}

#endif // FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF
