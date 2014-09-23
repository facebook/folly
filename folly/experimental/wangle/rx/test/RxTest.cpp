/*
 * Copyright 2014 Facebook, Inc.
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

#include <folly/experimental/wangle/rx/Observer.h>
#include <folly/experimental/wangle/rx/Subject.h>
#include <gtest/gtest.h>

using namespace folly::wangle;

TEST(RxTest, SubscribeDuringCallback) {
  // A subscriber who was subscribed in the course of a callback should get
  // subsequent updates but not the current update.
  Subject<int> subject;
  int outerCount = 0;
  int innerCount = 0;
  subject.subscribe(Observer<int>::create([&] (int x) {
    outerCount++;
    subject.subscribe(Observer<int>::create([&] (int y) {
      innerCount++;
    }));
  }));
  subject.onNext(42);
  subject.onNext(0xDEADBEEF);
  EXPECT_EQ(2, outerCount);
  EXPECT_EQ(1, innerCount);
}
