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

#pragma once

#include <folly/experimental/coro/Task.h>
#include <folly/portability/GMock.h>

namespace folly {
namespace coro {
namespace gmock_helpers {

// This helper function is intended for use in GMock implementations where the
// implementation of the method is a coroutine lambda.
//
// The GMock framework internally always takes a copy of an action/lambda
// before invoking it to prevent cases where invoking the method might end
// up destroying itself.
//
// However, this is problematic for coroutine-lambdas-with-captures as the
// return-value from invoking a coroutine lambda will typically capture a
// reference to the copy of the lambda which will immediately become a dangling
// reference as soon as the mocking framework returns that value to the caller.
//
// Use this action-factory instead of Invoke() when passing coroutine-lambdas
// to mock definitions to ensure that a copy of the lambda is kept alive until
// the coroutine completes. It does this by invoking the lambda using the
// folly::coro::co_invoke() helper instead of directly invoking the lambda.
//
//
// Example:
//   using namespace ::testing
//   using namespace folly::coro::gmock_helpers;
//
//   MockFoo mock;
//   int fooCallCount = 0;
//
//   EXPECT_CALL(mock, foo(_))
//     .WillRepeatedly(CoInvoke([&](int x) -> folly::coro::Task<int> {
//                                ++fooCallCount;
//                                co_return x + 1;
//                              }));
//
template <typename Lambda>
auto CoInvoke(Lambda&& lambda) {
  return ::testing::Invoke(
      [capturedLambda = static_cast<Lambda&&>(lambda)](auto&&... args) {
        return folly::coro::co_invoke(
            capturedLambda, static_cast<decltype(args)>(args)...);
      });
}

} // namespace gmock_helpers
} // namespace coro
} // namespace folly
