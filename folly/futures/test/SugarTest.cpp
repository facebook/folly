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

#include <folly/futures/Future.h>

using namespace folly;

TEST(Sugar, pollReady) {
  Promise<int> p;
  auto f = p.getFuture();
  p.setValue(42);
  EXPECT_EQ(42, f.poll().value().value());
}

TEST(Sugar, pollNotReady) {
  Promise<int> p;
  auto f = p.getFuture();
  EXPECT_FALSE(f.poll().hasValue());
}

TEST(Sugar, pollException) {
  Promise<void> p;
  auto f = p.getFuture();
  p.setWith([] { throw std::runtime_error("Runtime"); });
  EXPECT_TRUE(f.poll().value().hasException());
}

TEST(Sugar, filterTrue) {
  EXPECT_EQ(42, makeFuture(42).filter([](int){ return true; }).get());
}

TEST(Sugar, filterFalse) {
  EXPECT_THROW(makeFuture(42).filter([](int){ return false; }).get(),
               folly::PredicateDoesNotObtain);
}

TEST(Sugar, filterMoveonly) {
  EXPECT_EQ(42,
    *makeFuture(folly::make_unique<int>(42))
     .filter([](std::unique_ptr<int> const&) { return true; })
     .get());
}
