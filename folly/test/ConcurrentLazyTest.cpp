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

#include <folly/ConcurrentLazy.h>

#include <functional>
#include <mutex>
#include <thread>

#include <folly/portability/GTest.h>

TEST(ConcurrentLazy, Simple) {
  int computeCount = 0;

  auto const val = folly::concurrent_lazy([&]() -> int {
    ++computeCount;
    EXPECT_EQ(computeCount, 1);
    return 12;
  });
  EXPECT_EQ(computeCount, 0);

  for (int i = 0; i < 100; ++i) {
    if (i > 50) {
      EXPECT_EQ(val(), 12);
      EXPECT_EQ(computeCount, 1);
    } else {
      EXPECT_EQ(computeCount, 0);
    }
  }
  EXPECT_EQ(val(), 12);
  EXPECT_EQ(computeCount, 1);
}

TEST(ConcurrentLazy, MultipleReaders) {
  std::atomic_int computeCount = 0;

  std::mutex m;

  auto const val = folly::concurrent_lazy([&]() -> int {
    ++computeCount;
    EXPECT_EQ(computeCount, 1);

    // Block here so that we can wait for threads to pile up.
    m.lock();
    m.unlock();
    return 12;
  });
  EXPECT_EQ(computeCount, 0);

  // Lock the mutex while we create the readers, which will prevent the
  // instantiation from completing until we've created all our readers.
  m.lock();
  std::vector<std::thread> readers;
  for (int i = 0; i < 10; ++i) {
    readers.push_back(std::thread([&] {
      for (int j = 0; j < 1000; ++j) {
        EXPECT_EQ(val(), 12);
      }
    }));
  }

  m.unlock();
  for (auto& reader : readers) {
    reader.join();
  }

  EXPECT_EQ(val(), 12);
  EXPECT_EQ(computeCount, 1);
}

struct CopyCount {
  CopyCount() = default;
  CopyCount(const CopyCount&) { ++count; }
  CopyCount(CopyCount&&) = default;

  CopyCount& operator=(const CopyCount&) = default;
  CopyCount& operator=(CopyCount&&) = default;

  static int count;

  bool operator()() const { return true; }
};

int CopyCount::count = 0;

TEST(ConcurrentLazy, NonLambda) {
  auto const rval = folly::concurrent_lazy(CopyCount());
  EXPECT_EQ(CopyCount::count, 0);
  EXPECT_EQ(rval(), true);
  EXPECT_EQ(CopyCount::count, 0);

  CopyCount cpy;
  auto const lval = folly::concurrent_lazy(cpy);
  EXPECT_EQ(CopyCount::count, 1);
  EXPECT_EQ(lval(), true);
  EXPECT_EQ(CopyCount::count, 1);

  std::function<bool()> f = [&] { return 12; };
  auto const lazyF = folly::concurrent_lazy(f);
  EXPECT_EQ(lazyF(), true);
}
