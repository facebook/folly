/*
 * Copyright 2015 Facebook, Inc.
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

#include <gtest/gtest.h>
#include <thread>

#include <folly/futures/Future.h>

using namespace folly;

struct Widget {
  int v_, copied_, moved_;
  /* implicit */ Widget(int v) : v_(v), copied_(0), moved_(0) {}
  Widget(const Widget& other)
    : v_(other.v_), copied_(other.copied_ + 1), moved_(other.moved_) {}
  Widget(Widget&& other) noexcept
    : v_(other.v_), copied_(other.copied_), moved_(other.moved_ + 1) {}
  Widget& operator=(const Widget& other)
    { throw std::logic_error("unexpected copy assignment"); }
  Widget& operator=(Widget&& other)
    { throw std::logic_error("unexpected move assignment"); }
};

TEST(Then, tryConstructor) {
  auto t = Try<Widget>(23);
  EXPECT_EQ(t.value().v_, 23);
  EXPECT_EQ(t.value().copied_, 0);
  EXPECT_EQ(t.value().moved_, 1);
}

TEST(Then, makeFuture) {
  auto future = makeFuture<Widget>(23);
  EXPECT_EQ(future.value().v_, 23);
  EXPECT_EQ(future.value().copied_, 0);
  EXPECT_EQ(future.value().moved_, 2);
}

TEST(Then, TryConstRValueReference) {
  auto future = makeFuture<Widget>(23).then(
    [](const Try<Widget>&& t) {
      EXPECT_EQ(t.value().copied_, 0);
      EXPECT_EQ(t.value().moved_, 2);
      return t.value().v_;
    });
  EXPECT_EQ(future.value(), 23);
}

TEST(Then, TryRValueReference) {
  auto future = makeFuture<Widget>(23).then(
    [](Try<Widget>&& t) {
      EXPECT_EQ(t.value().copied_, 0);
      EXPECT_EQ(t.value().moved_, 2);
      return t.value().v_;
    });
  EXPECT_EQ(future.value(), 23);
}

TEST(Then, TryLValueReference) {
  auto future = makeFuture<Widget>(23).then(
    [](Try<Widget>& t) {
      EXPECT_EQ(t.value().copied_, 0);
      EXPECT_EQ(t.value().moved_, 2);
      return t.value().v_;
    });
  EXPECT_EQ(future.value(), 23);
}

TEST(Then, TryConstLValueReference) {
  auto future = makeFuture<Widget>(23).then(
    [](const Try<Widget>& t) {
      EXPECT_EQ(t.value().copied_, 0);
      EXPECT_EQ(t.value().moved_, 2);
      return t.value().v_;
    });
  EXPECT_EQ(future.value(), 23);
}

TEST(Then, TryValue) {
  auto future = makeFuture<Widget>(23).then(
    [](Try<Widget> t) {
      EXPECT_EQ(t.value().copied_, 0);
      EXPECT_EQ(t.value().moved_, 3);
      return t.value().v_;
    });
  EXPECT_EQ(future.value(), 23);
}

TEST(Then, TryConstValue) {
  auto future = makeFuture<Widget>(23).then(
    [](const Try<Widget> t) {
      EXPECT_EQ(t.value().copied_, 0);
      EXPECT_EQ(t.value().moved_, 3);
      return t.value().v_;
    });
  EXPECT_EQ(future.value(), 23);
}

TEST(Then, ConstRValueReference) {
  auto future = makeFuture<Widget>(23).then(
    [](const Widget&& w) {
      EXPECT_EQ(w.copied_, 0);
      EXPECT_EQ(w.moved_, 2);
      return w.v_;
    });
  EXPECT_EQ(future.value(), 23);
}

TEST(Then, RValueReference) {
  auto future = makeFuture<Widget>(23).then(
    [](Widget&& w) {
      EXPECT_EQ(w.copied_, 0);
      EXPECT_EQ(w.moved_, 2);
      return w.v_;
    });
  EXPECT_EQ(future.value(), 23);
}

TEST(Then, LValueReference) {
  auto future = makeFuture<Widget>(23).then(
    [](Widget& w) {
      EXPECT_EQ(w.copied_, 0);
      EXPECT_EQ(w.moved_, 2);
      return w.v_;
    });
  EXPECT_EQ(future.value(), 23);
}

TEST(Then, ConstLValueReference) {
  auto future = makeFuture<Widget>(23).then(
    [](const Widget& w) {
      EXPECT_EQ(w.copied_, 0);
      EXPECT_EQ(w.moved_, 2);
      return w.v_;
    });
  EXPECT_EQ(future.value(), 23);
}

TEST(Then, Value) {
  auto future = makeFuture<Widget>(23).then(
    [](Widget w) {
      EXPECT_EQ(w.copied_, 0);
      EXPECT_EQ(w.moved_, 3);
      return w.v_;
    });
  EXPECT_EQ(future.value(), 23);
}

TEST(Then, ConstValue) {
  auto future = makeFuture<Widget>(23).then(
    [](const Widget w) {
      EXPECT_EQ(w.copied_, 0);
      EXPECT_EQ(w.moved_, 3);
      return w.v_;
    });
  EXPECT_EQ(future.value(), 23);
}
