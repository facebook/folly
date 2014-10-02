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

static std::unique_ptr<Observer<int>> incrementer(int& counter) {
  return Observer<int>::create([&] (int x) {
    counter++;
  });
}

TEST(RxTest, Subscription) {
  Subject<int> subject;
  auto count = 0;
  {
    auto s = subject.subscribe(incrementer(count));
    subject.onNext(1);
  }
  // The subscription has gone out of scope so no one should get this.
  subject.onNext(2);
  EXPECT_EQ(1, count);
}

TEST(RxTest, SubscriptionMove) {
  Subject<int> subject;
  auto count = 0;
  auto s = subject.subscribe(incrementer(count));
  auto s2 = subject.subscribe(incrementer(count));
  s2 = std::move(s);
  subject.onNext(1);
  Subscription<int> s3(std::move(s2));
  subject.onNext(2);
  EXPECT_EQ(2, count);
}

TEST(RxTest, SubscriptionOutlivesSubject) {
  Subscription<int> s;
  {
    Subject<int> subject;
    s = subject.subscribe(Observer<int>::create([](int){}));
  }
  // Don't explode when s is destroyed
}

TEST(RxTest, SubscribeDuringCallback) {
  // A subscriber who was subscribed in the course of a callback should get
  // subsequent updates but not the current update.
  Subject<int> subject;
  int outerCount = 0, innerCount = 0;
  Subscription<int> s1, s2;
  s1 = subject.subscribe(Observer<int>::create([&] (int x) {
    outerCount++;
    s2 = subject.subscribe(incrementer(innerCount));
  }));
  subject.onNext(42);
  subject.onNext(0xDEADBEEF);
  EXPECT_EQ(2, outerCount);
  EXPECT_EQ(1, innerCount);
}

TEST(RxTest, UnsubscribeDuringCallback) {
  // A subscriber who was unsubscribed in the course of a callback should get
  // the current update but not subsequent ones
  Subject<int> subject;
  int count1 = 0, count2 = 0;
  auto s1 = subject.subscribe(incrementer(count1));
  auto s2 = subject.subscribe(Observer<int>::create([&] (int x) {
    count2++;
    s1.~Subscription();
  }));
  subject.onNext(1);
  subject.onNext(2);
  EXPECT_EQ(1, count1);
  EXPECT_EQ(2, count2);
}

TEST(RxTest, SubscribeUnsubscribeDuringCallback) {
  // A subscriber who was subscribed and unsubscribed in the course of a
  // callback should not get any updates
  Subject<int> subject;
  int outerCount = 0, innerCount = 0;
  auto s2 = subject.subscribe(Observer<int>::create([&] (int x) {
    outerCount++;
    auto s2 = subject.subscribe(incrementer(innerCount));
  }));
  subject.onNext(1);
  subject.onNext(2);
  EXPECT_EQ(2, outerCount);
  EXPECT_EQ(0, innerCount);
}
