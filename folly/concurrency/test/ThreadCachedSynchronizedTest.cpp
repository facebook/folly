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

#include <folly/concurrency/ThreadCachedSynchronized.h>

#include <folly/lang/Keep.h>
#include <folly/portability/GTest.h>

class ThreadCachedSynchronizedTest : public testing::Test {};

TEST_F(ThreadCachedSynchronizedTest, load_store) {
  folly::thread_cached_synchronized obj{0};
  EXPECT_EQ(0, obj);
  EXPECT_EQ(0, obj.load());

  obj = 7;
  EXPECT_EQ(7, obj);

  obj.store();
  EXPECT_EQ(0, obj);

  obj.store(7);
  EXPECT_EQ(7, obj);

  int const& ref = obj.operator*();
  EXPECT_EQ(7, ref);

  int const* ptr = obj.operator->();
  EXPECT_EQ(7, *ptr);
}

TEST_F(ThreadCachedSynchronizedTest, exchange) {
  folly::thread_cached_synchronized obj{9};
  EXPECT_EQ(9, obj.exchange(3));
  EXPECT_EQ(3, obj);
}

TEST_F(ThreadCachedSynchronizedTest, compare_exchange_failure) {
  folly::thread_cached_synchronized obj{12};
  int val = 4;
  EXPECT_FALSE(obj.compare_exchange(val, 32));
  EXPECT_EQ(12, val);
  EXPECT_EQ(12, obj);
}

TEST_F(ThreadCachedSynchronizedTest, compare_exchange_success) {
  folly::thread_cached_synchronized obj{12};
  int val = 12;
  EXPECT_TRUE(obj.compare_exchange(val, 32));
  EXPECT_EQ(12, val);
  EXPECT_EQ(32, obj);
}

TEST_F(ThreadCachedSynchronizedTest, swap_member) {
  folly::thread_cached_synchronized obj{3};
  int val = 9;
  obj.swap(val);
  EXPECT_EQ(3, val);
  EXPECT_EQ(9, obj);
}

TEST_F(ThreadCachedSynchronizedTest, swap_free) {
  folly::thread_cached_synchronized obj{9};
  int val = 3;
  swap(obj, val);
  EXPECT_EQ(9, val);
  EXPECT_EQ(3, obj);
}

TEST_F(ThreadCachedSynchronizedTest, not_default_constructible) {
  struct foo {
    int value;
    foo() = delete;
    explicit foo(int value_) noexcept : value{value_} {}
    foo(foo const&) = default;
    foo& operator=(foo const&) = default;
  };
  folly::thread_cached_synchronized obj{foo{3}};
  EXPECT_EQ(3, foo(obj).value);
}
