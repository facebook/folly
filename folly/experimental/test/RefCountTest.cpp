/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <thread>

#include <folly/experimental/TLRefCount.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/Baton.h>
#include <folly/synchronization/test/Barrier.h>

namespace folly {

template <typename RefCount>
void basicTest() {
  constexpr size_t numIters = 100000;
  constexpr size_t numThreads = 10;

  size_t got0 = 0;

  RefCount count;

  folly::Baton<> b;

  std::vector<std::thread> ts;
  folly::Baton<> threadBatons[numThreads];
  for (size_t t = 0; t < numThreads; ++t) {
    ts.emplace_back([&count, &b, &got0, t, &threadBatons] {
      for (size_t i = 0; i < numIters; ++i) {
        auto ret = ++count;

        EXPECT_TRUE(ret > 1);
        if (i == 0) {
          threadBatons[t].post();
        }
      }

      if (t == 0) {
        b.post();
      }

      for (size_t i = 0; i < numIters; ++i) {
        auto ret = --count;

        if (ret == 0) {
          ++got0;
          EXPECT_EQ(numIters - 1, i);
        }
      }
    });
  }

  for (size_t t = 0; t < numThreads; ++t) {
    threadBatons[t].wait();
  }

  b.wait();

  count.useGlobal();
  if (--count == 0) {
    ++got0;
  }

  for (auto& t : ts) {
    t.join();
  }

  EXPECT_EQ(1, got0);

  EXPECT_EQ(0, ++count);
  EXPECT_EQ(0, ++count);
}

template <typename RefCount>
void stressTest(size_t itersCount) {
  for (size_t i = 0; i < itersCount; ++i) {
    RefCount count;
    std::mutex mutex;
    int a{1};

    std::thread t1([&]() {
      if (++count) {
        {
          std::lock_guard<std::mutex> lg(mutex);
          EXPECT_EQ(1, a);
        }
        --count;
      }
    });

    std::thread t2([&]() {
      count.useGlobal();
      if (--count == 0) {
        std::lock_guard<std::mutex> lg(mutex);
        a = 0;
      }
    });

    t1.join();
    t2.join();

    EXPECT_EQ(0, ++count);
  }
}

TEST(TLRefCount, Basic) {
  basicTest<TLRefCount>();
}

TEST(TLRefCount, Stress) {
  // This is absurdly slow, so we can't
  // do it that many times.
  stressTest<TLRefCount>(500);
}

TEST(TLRefCount, SafeToDeleteWhenReachingZero) {
  // Tests that it is safe to delete a TLRefCount when it is already in global
  // state and its ref count reaches zero. This is a reasonable assumption
  // since data structures typically embed a TLRefCount object and delete
  // themselves when the refcount reaches 0.
  TLRefCount* count = new TLRefCount();

  folly::test::Barrier done(2); // give time for TSAN to catch issues
  folly::Baton<> batonUnref;
  int times_deleted = 0;

  std::thread t1([&] {
    ++*count;
    batonUnref.post();

    if (--*count == 0) {
      times_deleted++;
      delete count;
    }
    done.wait();
  });

  std::thread t2([&] {
    // Make sure thread 1 already grabbed a reference first, otherwise we might
    // destroy it before thread 1 had a chance.
    batonUnref.wait();

    count->useGlobal();
    if (--*count == 0) {
      times_deleted++;
      delete count;
    }
    done.wait();
  });

  t1.join();
  t2.join();
  EXPECT_EQ(times_deleted, 1);
}

} // namespace folly
