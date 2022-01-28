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

#include <folly/lang/SafeAssert.h>

#include <glog/logging.h>

#include <folly/Benchmark.h>
#include <folly/Conv.h>
#include <folly/lang/Keep.h>
#include <folly/portability/GTest.h>

extern "C" FOLLY_KEEP void check_folly_safe_check(bool cond) {
  FOLLY_SAFE_CHECK(cond, "the condition failed");
  folly::detail::keep_sink();
}

extern "C" FOLLY_KEEP void check_folly_safe_pcheck(bool cond) {
  FOLLY_SAFE_PCHECK(cond, "the condition failed");
  folly::detail::keep_sink();
}

// clang-format off
[[noreturn]] void fail() {
  FOLLY_SAFE_CHECK(0 + 0, "hello");
}
// clang-format on

void succeed() {
  FOLLY_SAFE_CHECK(1, "world");
}

TEST(SafeAssert, AssertionFailure) {
  succeed();
  EXPECT_DEATH(fail(), "Assertion failure: 0 \\+ 0");
  EXPECT_DEATH(fail(), "Message: hello");
}

TEST(SafeAssert, AssertionFailureVariadicFormattedArgs) {
  FOLLY_SAFE_CHECK(true);
  EXPECT_DEATH( //
      ([] { FOLLY_SAFE_CHECK(false); }()), //
      "Assertion failure: false");
  EXPECT_DEATH( //
      ([] { FOLLY_SAFE_CHECK(false, 17ull, " < ", 18ull); }()), //
      "Message: 17 < 18");
}

TEST(SafeAssert, AssertionFailureErrno) {
  EXPECT_DEATH(
      ([] { FOLLY_SAFE_PCHECK((errno = EINVAL) && false, "hello"); }()),
      folly::to<std::string>("Error: ", EINVAL, " \\(EINVAL\\)"));
  EXPECT_DEATH(
      ([] { FOLLY_SAFE_PCHECK((errno = 999) && false, "hello"); }()),
      folly::to<std::string>("Error: 999 \\(<unknown>\\)"));
}

TEST(SafeAssert, Fatal) {
  EXPECT_DEATH( //
      ([] { FOLLY_SAFE_FATAL("hello"); })(),
      "Message: hello");
}
