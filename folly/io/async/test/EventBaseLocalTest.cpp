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

#include <folly/io/async/EventBaseLocal.h>

#include <folly/io/async/EventBaseAtomicNotificationQueue.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/Baton.h>

struct Foo {
  Foo(int n_, std::function<void()> dtorFn_)
      : n(n_), dtorFn(std::move(dtorFn_)) {}
  ~Foo() { dtorFn(); }

  int n;
  std::function<void()> dtorFn;
};

TEST(EventBaseLocalTest, Basic) {
  int dtorCnt = 0;
  folly::EventBase evb1;

  {
    folly::EventBaseLocal<Foo> foo;

    EXPECT_EQ(foo.get(evb1), nullptr);

    foo.emplace(evb1, 5, [&] { ++dtorCnt; });

    EXPECT_EQ(foo.get(evb1)->n, 5);

    {
      folly::EventBase evb2;
      foo.emplace(evb2, 6, [&] { ++dtorCnt; });
      EXPECT_EQ(foo.get(evb2)->n, 6);
      foo.erase(evb2);
      EXPECT_EQ(dtorCnt, 1); // should dtor a Foo when we erase
      EXPECT_EQ(foo.get(evb2), nullptr);
      foo.emplace(evb2, 7, [&] { ++dtorCnt; });
      EXPECT_EQ(foo.get(evb2)->n, 7);
    }

    EXPECT_EQ(dtorCnt, 2); // should dtor a Foo when evb2 destructs
  }
  EXPECT_EQ(dtorCnt, 2); // should schedule Foo destructor, when foo destructs
  evb1.loop();
  EXPECT_EQ(dtorCnt, 3); // Foo will be destroyed in EventBase loop
}

TEST(EventBaseLocalTest, tryEmplace) {
  folly::EventBase evb1;
  folly::EventBaseLocal<int> ints;

  EXPECT_EQ(ints.try_emplace(evb1), 0);
  EXPECT_EQ(ints.try_emplace(evb1, 5), 0);

  folly::EventBase evb2;
  EXPECT_EQ(ints.try_emplace(evb2, 5), 5);
  ints.erase(evb2);
  EXPECT_EQ(4, ints.try_emplace_with(evb2, [] { return 4; }));
}

using IntPtr = std::unique_ptr<int>;

TEST(EventBaseLocalTest, getOrCreateNoncopyable) {
  folly::EventBase evb1;
  folly::EventBaseLocal<IntPtr> ints;

  EXPECT_EQ(ints.try_emplace(evb1), IntPtr());
  EXPECT_EQ(ints.try_emplace(evb1, std::make_unique<int>(5)), IntPtr());

  folly::EventBase evb2;
  EXPECT_EQ(*ints.try_emplace(evb2, std::make_unique<int>(5)), 5);
}

TEST(EventBaseLocalTest, emplaceNoncopyable) {
  folly::EventBase evb;
  folly::EventBaseLocal<IntPtr> ints;
  ints.emplace(evb, std::make_unique<int>(42));
  EXPECT_EQ(42, **ints.get(evb));
}

TEST(EventBaseLocalTest, DestructionOrder) {
  struct Consumer {
    void operator()(int) noexcept {}
  };
  using Queue = folly::EventBaseAtomicNotificationQueue<int, Consumer>;
  folly::EventBaseLocal<std::unique_ptr<Queue>> ebl;
  {
    // Since queue binds to the underlying event loop, we must ensure
    // local storage is cleared before event loop is destroyed.
    folly::EventBase evb;
    ebl.emplace_with(
           evb,
           [&evb] {
             auto q = std::make_unique<Queue>();
             q->startConsumingInternal(&evb);
             return q;
           })
        .get();
  }
}

TEST(EventBaseLocalTest, DestructorStressTest) {
  std::atomic<folly::EventBase*> currentEvb;
  folly::Baton<> baton;
  const int kIterations = 10000;
  auto thread1 = std::thread([&] {
    for (int i = 0; i < kIterations; i++) {
      folly::EventBase evb;
      currentEvb = &evb;
      baton.post();
      evb.loopForever();
    }
  });
  auto thread2 = std::thread([&] {
    for (int i = 0; i < kIterations; i++) {
      folly::EventBaseLocal<int> local;
      baton.wait();
      baton.reset();
      auto evb = currentEvb.exchange(nullptr);
      evb->runInEventBaseThreadAndWait([&] { local.emplace(*evb, 4); });
      evb->terminateLoopSoon();
    }
  });

  thread1.join();
  thread2.join();
}
