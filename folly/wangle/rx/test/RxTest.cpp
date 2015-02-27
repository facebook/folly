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

#include <folly/wangle/rx/Observer.h>
#include <folly/wangle/rx/Subject.h>
#include <gtest/gtest.h>

using namespace folly::wangle;

static std::unique_ptr<Observer<int>> incrementer(int& counter) {
  return Observer<int>::create([&] (int x) {
    counter++;
  });
}

TEST(RxTest, Observe) {
  Subject<int> subject;
  auto count = 0;
  subject.observe(incrementer(count));
  subject.onNext(1);
  EXPECT_EQ(1, count);
}

TEST(RxTest, ObserveInline) {
  Subject<int> subject;
  auto count = 0;
  auto o = incrementer(count).release();
  subject.observe(o);
  subject.onNext(1);
  EXPECT_EQ(1, count);
  delete o;
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

TEST(RxTest, ObserveDuringCallback) {
  Subject<int> subject;
  int outerCount = 0, innerCount = 0;
  subject.observe(Observer<int>::create([&] (int x) {
    outerCount++;
    subject.observe(incrementer(innerCount));
  }));
  subject.onNext(42);
  subject.onNext(0xDEADBEEF);
  EXPECT_EQ(2, outerCount);
  EXPECT_EQ(1, innerCount);
}

TEST(RxTest, ObserveInlineDuringCallback) {
  Subject<int> subject;
  int outerCount = 0, innerCount = 0;
  auto innerO = incrementer(innerCount).release();
  auto outerO = Observer<int>::create([&] (int x) {
    outerCount++;
    subject.observe(innerO);
  }).release();
  subject.observe(outerO);
  subject.onNext(42);
  subject.onNext(0xDEADBEEF);
  EXPECT_EQ(2, outerCount);
  EXPECT_EQ(1, innerCount);
  delete innerO;
  delete outerO;
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

// Move only type
typedef std::unique_ptr<int> MO;
static MO makeMO() { return folly::make_unique<int>(1); }
template <typename T>
static ObserverPtr<T> makeMOObserver() {
  return Observer<T>::create([](const T& mo) {
    EXPECT_EQ(1, *mo);
  });
}

TEST(RxTest, MoveOnlyRvalue) {
  Subject<MO> subject;
  auto s1 = subject.subscribe(makeMOObserver<MO>());
  auto s2 = subject.subscribe(makeMOObserver<MO>());
  auto mo = makeMO();
  // Can't bind lvalues to rvalue references
  // subject.onNext(mo);
  subject.onNext(std::move(mo));
  subject.onNext(makeMO());
}

// Copy only type
struct CO {
  CO() = default;
  CO(const CO&) = default;
  CO(CO&&) = delete;
};

template <typename T>
static ObserverPtr<T> makeCOObserver() {
  return Observer<T>::create([](const T& mo) {});
}

TEST(RxTest, CopyOnly) {
  Subject<CO> subject;
  auto s1 = subject.subscribe(makeCOObserver<CO>());
  CO co;
  subject.onNext(co);
}
