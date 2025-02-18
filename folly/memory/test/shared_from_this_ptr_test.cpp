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

#include <folly/memory/shared_from_this_ptr.h>

#include <memory>
#include <utility>

#include <folly/portability/GTest.h>

FOLLY_GNU_DISABLE_WARNING("-Wself-move")

struct SharedFromThisPtrTest : testing::Test {};

struct jabberwocky : std::enable_shared_from_this<jabberwocky> {
  int data = 3;
};

TEST_F(SharedFromThisPtrTest, empty) {
  folly::shared_from_this_ptr<jabberwocky> ptr;
  EXPECT_FALSE(bool(ptr));
  EXPECT_EQ(nullptr, ptr.get());
}

TEST_F(SharedFromThisPtrTest, copy_shared_constructed) {
  auto shared = std::make_shared<jabberwocky>();
  folly::shared_from_this_ptr<jabberwocky> ptr{shared};
  EXPECT_TRUE(bool(ptr));
  EXPECT_EQ(shared.get(), ptr.get());
  EXPECT_EQ(&*shared, &*ptr);
  EXPECT_EQ(3, ptr->data);
  EXPECT_EQ(shared, std::shared_ptr<jabberwocky>(ptr));
}

TEST_F(SharedFromThisPtrTest, copy_shared_assigned) {
  auto shared = std::make_shared<jabberwocky>();
  folly::shared_from_this_ptr<jabberwocky> ptr;
  ptr = shared;
  EXPECT_TRUE(bool(ptr));
  EXPECT_EQ(shared.get(), ptr.get());
  EXPECT_EQ(&*shared, &*ptr);
  EXPECT_EQ(3, ptr->data);
  EXPECT_EQ(shared, std::shared_ptr<jabberwocky>(ptr));
}

TEST_F(SharedFromThisPtrTest, move_shared_constructed) {
  auto shared = std::make_shared<jabberwocky>();
  auto copy = shared;
  folly::shared_from_this_ptr<jabberwocky> ptr{std::move(copy)};
  EXPECT_FALSE(bool(copy));
  EXPECT_TRUE(bool(ptr));
  EXPECT_EQ(shared.get(), ptr.get());
  EXPECT_EQ(&*shared, &*ptr);
  EXPECT_EQ(3, ptr->data);
  EXPECT_EQ(shared, std::shared_ptr<jabberwocky>(ptr));
}

TEST_F(SharedFromThisPtrTest, move_shared_assigned) {
  auto shared = std::make_shared<jabberwocky>();
  auto copy = shared;
  folly::shared_from_this_ptr<jabberwocky> ptr;
  ptr = std::move(copy);
  EXPECT_FALSE(bool(copy));
  EXPECT_TRUE(bool(ptr));
  EXPECT_EQ(shared.get(), ptr.get());
  EXPECT_EQ(&*shared, &*ptr);
  EXPECT_EQ(3, ptr->data);
  EXPECT_EQ(shared, std::shared_ptr<jabberwocky>(ptr));
}

TEST_F(SharedFromThisPtrTest, copy_constructed) {
  auto shared = std::make_shared<jabberwocky>();
  folly::shared_from_this_ptr<jabberwocky> orig{shared};
  auto ptr = orig;
  EXPECT_TRUE(bool(orig));
  EXPECT_EQ(shared.get(), orig.get());
  EXPECT_TRUE(bool(ptr));
  EXPECT_EQ(shared.get(), ptr.get());
  EXPECT_EQ(&*shared, &*ptr);
  EXPECT_EQ(3, ptr->data);
  EXPECT_EQ(shared, std::shared_ptr<jabberwocky>(ptr));
}

TEST_F(SharedFromThisPtrTest, copy_assigned) {
  auto shared = std::make_shared<jabberwocky>();
  folly::shared_from_this_ptr<jabberwocky> orig{shared};
  folly::shared_from_this_ptr<jabberwocky> ptr;
  ptr = orig;
  EXPECT_TRUE(bool(orig));
  EXPECT_EQ(shared.get(), orig.get());
  EXPECT_TRUE(bool(ptr));
  EXPECT_EQ(shared.get(), ptr.get());
  EXPECT_EQ(&*shared, &*ptr);
  EXPECT_EQ(3, ptr->data);
  EXPECT_EQ(shared, std::shared_ptr<jabberwocky>(ptr));
}

TEST_F(SharedFromThisPtrTest, move_constructed) {
  auto shared = std::make_shared<jabberwocky>();
  folly::shared_from_this_ptr<jabberwocky> orig{shared};
  auto ptr = static_cast<decltype(orig)&&>(orig);
  EXPECT_FALSE(bool(orig));
  EXPECT_EQ(nullptr, orig.get());
  EXPECT_TRUE(bool(ptr));
  EXPECT_EQ(shared.get(), ptr.get());
  EXPECT_EQ(&*shared, &*ptr);
  EXPECT_EQ(3, ptr->data);
  EXPECT_EQ(shared, std::shared_ptr<jabberwocky>(ptr));
}

TEST_F(SharedFromThisPtrTest, move_assigned) {
  auto shared = std::make_shared<jabberwocky>();
  folly::shared_from_this_ptr<jabberwocky> orig{shared};
  folly::shared_from_this_ptr<jabberwocky> ptr;
  ptr = static_cast<decltype(orig)&&>(orig);
  EXPECT_FALSE(bool(orig));
  EXPECT_EQ(nullptr, orig.get());
  EXPECT_TRUE(bool(ptr));
  EXPECT_EQ(shared.get(), ptr.get());
  EXPECT_EQ(&*shared, &*ptr);
  EXPECT_EQ(3, ptr->data);
  EXPECT_EQ(shared, std::shared_ptr<jabberwocky>(ptr));
}

TEST_F(SharedFromThisPtrTest, copy_assign_self_empty) {
  folly::shared_from_this_ptr<jabberwocky> ptr;
  ptr = std::as_const(ptr);
  EXPECT_FALSE(bool(ptr));
  EXPECT_EQ(nullptr, ptr.get());
}

TEST_F(SharedFromThisPtrTest, copy_assign_self) {
  auto shared = std::make_shared<jabberwocky>();
  folly::shared_from_this_ptr<jabberwocky> ptr{shared};
  ptr = std::as_const(ptr);
  EXPECT_TRUE(bool(ptr));
  EXPECT_EQ(shared.get(), ptr.get());
  EXPECT_EQ(&*shared, &*ptr);
  EXPECT_EQ(3, ptr->data);
  EXPECT_EQ(shared, std::shared_ptr<jabberwocky>(ptr));
}

TEST_F(SharedFromThisPtrTest, move_assign_self_empty) {
  folly::shared_from_this_ptr<jabberwocky> ptr;
  ptr = static_cast<decltype(ptr)&&>(ptr);
  EXPECT_FALSE(bool(ptr));
  EXPECT_EQ(nullptr, ptr.get());
}

TEST_F(SharedFromThisPtrTest, move_assign_self) {
  auto shared = std::make_shared<jabberwocky>();
  folly::shared_from_this_ptr<jabberwocky> ptr{shared};
  ptr = static_cast<decltype(ptr)&&>(ptr);
  EXPECT_TRUE(bool(ptr));
  EXPECT_EQ(shared.get(), ptr.get());
  EXPECT_EQ(&*shared, &*ptr);
  EXPECT_EQ(3, ptr->data);
  EXPECT_EQ(shared, std::shared_ptr<jabberwocky>(ptr));
}

TEST_F(SharedFromThisPtrTest, empty_equality) {
  folly::shared_from_this_ptr<jabberwocky> ptr;
  EXPECT_TRUE(nullptr == ptr);
  EXPECT_TRUE(ptr == nullptr);
  EXPECT_FALSE(nullptr != ptr);
  EXPECT_FALSE(ptr != nullptr);
}

TEST_F(SharedFromThisPtrTest, equality) {
  auto shared = std::make_shared<jabberwocky>();
  folly::shared_from_this_ptr<jabberwocky> ptr{shared};
  EXPECT_FALSE(nullptr == ptr);
  EXPECT_FALSE(ptr == nullptr);
  EXPECT_TRUE(nullptr != ptr);
  EXPECT_TRUE(ptr != nullptr);
}
