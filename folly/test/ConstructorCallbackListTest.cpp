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

constexpr std::size_t kMaxCallbacksDefault =
    folly::ConstructorCallbackList<void>::kMaxCallbacks;
constexpr std::size_t kExpandedOverflowSize = kMaxCallbacksDefault + 3;

template <class T, std::size_t MaxCallbacks = kMaxCallbacksDefault>
class ConstructorCallbackListTestStruct {
 public:
  int i_;
  explicit ConstructorCallbackListTestStruct(int i) : i_{i} {}

 private:
  folly::ConstructorCallbackList<
      ConstructorCallbackListTestStruct<T, MaxCallbacks>,
      MaxCallbacks>
      constructorCallbackList_{this};
};

} // namespace

TEST(ConstructorCallbackListTest, basic) {
  struct Tag {};
  using Object = ConstructorCallbackListTestStruct<Tag>;
  int count = 0;
  int lastI = -1;
  auto callbackF = [&](Object* f) {
    count++;
    lastI = f->i_;
  };

  Object f1{88}; // no call back called
  EXPECT_EQ(count, 0);
  EXPECT_EQ(lastI, -1);

  // add callback, verify call
  folly::ConstructorCallbackList<Object>::addCallback(callbackF);
  Object f2{99};

  EXPECT_EQ(count, 1);
  EXPECT_EQ(lastI, 99);
}

TEST(ConstructorCallbackListTest, overflow) {
  struct Tag {};
  using Object = ConstructorCallbackListTestStruct<Tag>;
  int count = 0;
  int lastI = -1;
  auto callbackF = [&](Object* f) {
    count++;
    lastI = f->i_;
  };

  // add one too many to the call backs
  for (std::size_t i = 0;
       i < folly::ConstructorCallbackList<Object>::kMaxCallbacks + 1;
       i++) {
    // add callback multiple times
    if (i < folly::ConstructorCallbackList<Object>::kMaxCallbacks) {
      // every other time should work without throwing the exception
      folly::ConstructorCallbackList<Object>::addCallback(callbackF);
    } else {
      // last add should fail
      EXPECT_THROW(
          folly::ConstructorCallbackList<Object>::addCallback(callbackF),
          std::length_error);
    }
  }
  Object f{99};
  EXPECT_EQ(count, folly::ConstructorCallbackList<Object>::kMaxCallbacks);
  EXPECT_EQ(lastI, 99);
}

TEST(ConstructorCallbackListTest, overflow7) {
  struct Tag {};
  using Object = ConstructorCallbackListTestStruct<Tag, kExpandedOverflowSize>;
  int count = 0;
  int lastI = -1;
  auto callbackF = [&](Object* b) {
    count++;
    lastI = b->i_;
  };

  // same as test above, but make sure we can change the size
  // of the callback array from the default

  // add one too many to the call backs
  for (std::size_t i = 0;
       i < folly::ConstructorCallbackList<Object, kExpandedOverflowSize>::
               kMaxCallbacks +
           1;
       i++) {
    // add callback multiple times
    if (i ==
        (folly::ConstructorCallbackList<Object, kExpandedOverflowSize>::
             kMaxCallbacks)) {
      // last add should fail
      EXPECT_THROW(
          (folly::ConstructorCallbackList<Object, kExpandedOverflowSize>::
               addCallback(callbackF)),
          std::length_error);
    } else {
      // every other time should work;
      folly::ConstructorCallbackList<Object, kExpandedOverflowSize>::
          addCallback(callbackF);
    }
  }
  Object b{99};
  EXPECT_EQ(
      count,
      (folly::ConstructorCallbackList<Object, kExpandedOverflowSize>::
           kMaxCallbacks));
  EXPECT_EQ(lastI, 99);
}

TEST(ConstructorCallbackListTest, size) {
  // Verify that adding a ConstructorCallbackList uses at most 1 byte more
  // memory This will help ensure that this code remains 'lightweight'
  auto ccb = folly::ConstructorCallbackList<void>(nullptr);
  EXPECT_LE(sizeof(ccb), 1);
}
