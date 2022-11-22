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

#include <stdexcept>
#include <folly/ConstructorCallbackList.h>
#include <folly/portability/GTest.h>

namespace {
class Foo {
 public:
  int i_;
  explicit Foo(int i) : i_{i} {}

 private:
  folly::ConstructorCallbackList<Foo> constructorCallbackList_{this};
};

constexpr int kBarSize = 7;
class Bar {
 public:
  int i_;
  explicit Bar(int i) : i_{i} {}

 private:
  // same as Foo but with non-default Callback size
  folly::ConstructorCallbackList<Bar, kBarSize> constructorCallbackList_{this};
};
} // namespace

TEST(ConstructorCallbackListTest, basic) {
  int count = 0;
  int lastI = -1;
  auto callbackF = [&](Foo* f) {
    count++;
    lastI = f->i_;
  };

  Foo f1{88}; // no call back called
  EXPECT_EQ(count, 0);
  EXPECT_EQ(lastI, -1);

  // add callback, verify call
  folly::ConstructorCallbackList<Foo>::addCallback(callbackF);
  Foo f2{99};

  EXPECT_EQ(count, 1);
  EXPECT_EQ(lastI, 99);
}

TEST(ConstructorCallbackListTest, overflow) {
  int count = 0;
  int lastI = -1;
  auto callbackF = [&](Foo* f) {
    count++;
    lastI = f->i_;
  };

  // add one too many to the call backs
  for (std::size_t i = 0;
       i < folly::ConstructorCallbackList<Foo>::kMaxCallbacks + 1;
       i++) {
    // add callback multiple times
    if (i < folly::ConstructorCallbackList<Foo>::kMaxCallbacks) {
      // every other time should work without throwing the exception
      folly::ConstructorCallbackList<Foo>::addCallback(callbackF);
    } else {
      // last add should fail
      EXPECT_THROW(
          folly::ConstructorCallbackList<Foo>::addCallback(callbackF),
          std::length_error);
    }
  }
  Foo f{99};
  EXPECT_EQ(count, folly::ConstructorCallbackList<Foo>::kMaxCallbacks);
  EXPECT_EQ(lastI, 99);
}

TEST(ConstructorCallbackListTest, overflow7) {
  int count = 0;
  int lastI = -1;
  auto callbackF = [&](Bar* b) {
    count++;
    lastI = b->i_;
  };

  // same as test above, but make sure we can change the size
  // of the callback array from the default

  // add one too many to the call backs
  for (std::size_t i = 0;
       i < folly::ConstructorCallbackList<Bar, kBarSize>::kMaxCallbacks + 1;
       i++) {
    // add callback multiple times
    if (i == (folly::ConstructorCallbackList<Bar, kBarSize>::kMaxCallbacks)) {
      // last add should fail
      EXPECT_THROW(
          (folly::ConstructorCallbackList<Bar, kBarSize>::addCallback(
              callbackF)),
          std::length_error);
    } else {
      // every other time should work;
      folly::ConstructorCallbackList<Bar, kBarSize>::addCallback(callbackF);
    }
  }
  Bar b{99};
  EXPECT_EQ(
      count, (folly::ConstructorCallbackList<Bar, kBarSize>::kMaxCallbacks));
  EXPECT_EQ(lastI, 99);
}

TEST(ConstructorCallbackListTest, size) {
  // Verify that adding a ConstructorCallbackList uses at most 1 byte more
  // memory This will help ensure that this code remains 'lightweight'
  auto ccb = folly::ConstructorCallbackList<void>(nullptr);
  EXPECT_LE(sizeof(ccb), 1);
}
