/*
 * Copyright 2019-present Facebook, Inc.
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

#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Task.h>
#include <folly/portability/GTest.h>
#include <folly/python/async_generator.h>

namespace folly {
namespace python {
namespace test {

coro::AsyncGenerator<int> make_generator() {
  co_yield 1;
  co_yield 2;
  co_yield 3;
  co_return;
}

using IntGeneratorWrapper = AsyncGeneratorWrapper<int>;

TEST(AsyncGeneratorTest, IterGenerator) {
  coro::blockingWait([]() -> coro::Task<void> {
    IntGeneratorWrapper generator{make_generator()};
    auto v = co_await generator.getNext();
    EXPECT_EQ(v, 1);
    v = co_await generator.getNext();
    EXPECT_EQ(v, 2);
    v = co_await generator.getNext();
    EXPECT_EQ(v, 3);
    v = co_await generator.getNext();
    EXPECT_EQ(v, none);
  }());
}

} // namespace test
} // namespace python
} // namespace folly
