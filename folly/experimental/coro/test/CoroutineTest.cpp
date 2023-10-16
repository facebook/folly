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

#include <folly/experimental/coro/Coroutine.h>

#include <folly/lang/Keep.h>
#include <folly/portability/GTest.h>

#if FOLLY_HAS_COROUTINES

extern "C" FOLLY_KEEP bool
check_folly_coro_detect_promise_return_object_eager_conversion() {
  return folly::coro::detect_promise_return_object_eager_conversion();
}

struct CoroutineTest : testing::Test {};

TEST_F(CoroutineTest, detect_promise_return_object_eager_conversion) {
  auto const eager =
      folly::coro::detect_promise_return_object_eager_conversion();
  if (folly::kMscVer) {
    EXPECT_EQ(folly::kMscVer < 1925, eager);
  }
  if (folly::kGnuc && !folly::kIsClang) {
    EXPECT_FALSE(eager);
  }
  if (folly::kIsClang) {
    constexpr auto ver = folly::kClangVerMajor;
    if (ver <= 14 || ver >= 17) {
      EXPECT_FALSE(eager);
    } else {
      SUCCEED(); // we sometimes use patched llvm-15/llvm-16
    }
  }
}

#endif
