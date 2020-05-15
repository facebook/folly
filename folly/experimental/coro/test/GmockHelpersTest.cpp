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

#include <folly/Portability.h>

#if FOLLY_HAS_COROUTINES

#include <folly/experimental/coro/GmockHelpers.h>

#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Task.h>

#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

using namespace ::testing;
using namespace folly::coro::gmock_helpers;

namespace {
class Foo {
 public:
  virtual ~Foo() = default;
  virtual folly::coro::Task<std::vector<std::string>> getValues() = 0;
};

class MockFoo : Foo {
 public:
  MOCK_METHOD0(getValues, folly::coro::Task<std::vector<std::string>>());
};

} // namespace

TEST(CoroLambdaGtest, CoInvokeAvoidsDanglingReferences) {
  MockFoo mock;

  const std::vector<std::string> values{"1", "2", "3"};
  EXPECT_CALL(mock, getValues())
      .WillRepeatedly(
          CoInvoke([&values]() -> folly::coro::Task<std::vector<std::string>> {
            co_return values;
          }));

  auto ret = folly::coro::blockingWait(mock.getValues());
  EXPECT_EQ(ret, values);
}

#endif // FOLLY_HAS_COROUTINES
