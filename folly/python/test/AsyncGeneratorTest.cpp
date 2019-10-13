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

#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Task.h>
#include <folly/portability/GTest.h>
#include <folly/python/async_generator.h>

namespace folly {
namespace python {
namespace test {

coro::AsyncGenerator<int> make_generator_val() {
  co_yield 1;
  co_yield 2;
  co_yield 3;
  co_return;
}

std::vector<int> values{1, 2, 3};
coro::AsyncGenerator<int&> make_generator_ref() {
  co_yield values[0];
  co_yield values[1];
  co_yield values[2];
  co_return;
}

class MoveOnly : public folly::MoveOnly {
 public:
  explicit MoveOnly(int val_) : val{val_} {}
  int val;
};

coro::AsyncGenerator<MoveOnly&&> make_generator_rref() {
  co_yield MoveOnly{1};
  co_yield MoveOnly{2};
  co_yield MoveOnly{3};
  co_return;
}

TEST(AsyncGeneratorTest, IterGeneratorVal) {
  coro::blockingWait([]() -> coro::Task<void> {
    AsyncGeneratorWrapper<int> generator{make_generator_val()};
    auto v = co_await generator.getNext();
    EXPECT_EQ(*v, 1);
    v = co_await generator.getNext();
    EXPECT_EQ(*v, 2);
    v = co_await generator.getNext();
    EXPECT_EQ(*v, 3);
    v = co_await generator.getNext();
    EXPECT_FALSE(v.has_value());
  }());
}

TEST(AsyncGeneratorTest, IterGeneratorRef) {
  coro::blockingWait([]() -> coro::Task<void> {
    AsyncGeneratorWrapper<int&> generator{make_generator_ref()};
    auto v = co_await generator.getNext();
    EXPECT_EQ(&*v, &values[0]);
    v = co_await generator.getNext();
    EXPECT_EQ(&*v, &values[1]);
    v = co_await generator.getNext();
    EXPECT_EQ(&*v, &values[2]);
    v = co_await generator.getNext();
    EXPECT_FALSE(v.has_value());
  }());
}

TEST(AsyncGeneratorTest, IterGeneratorRRef) {
  coro::blockingWait([]() -> coro::Task<void> {
    AsyncGeneratorWrapper<MoveOnly&&> generator{make_generator_rref()};
    auto v = co_await generator.getNext();
    EXPECT_EQ(v->val, 1);
    v = co_await generator.getNext();
    EXPECT_EQ(v->val, 2);
    v = co_await generator.getNext();
    EXPECT_EQ(v->val, 3);
    v = co_await generator.getNext();
    EXPECT_FALSE(v.has_value());
  }());
}

} // namespace test
} // namespace python
} // namespace folly
