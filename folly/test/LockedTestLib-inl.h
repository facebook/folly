/*
 * Copyright 2016 Facebook, Inc.
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

#pragma once

#include <gtest/gtest.h>

#include <folly/Foreach.h>
#include <folly/LockTraitsBoost.h>
#include <folly/Locked.h>
#include <folly/Random.h>
#include <glog/logging.h>
#include <algorithm>
#include <condition_variable>
#include <functional>
#include <map>
#include <random>
#include <thread>
#include <type_traits>
#include <vector>

namespace {
inline std::mt19937& getRNG() {
  static const auto seed = folly::randomNumberSeed();
  static std::mt19937 rng(seed);
  return rng;
}

void randomSleep(std::chrono::milliseconds min, std::chrono::milliseconds max) {
  std::uniform_int_distribution<> range(min.count(), max.count());
  std::chrono::milliseconds duration(range(getRNG()));
  std::this_thread::sleep_for(duration);
}
} // unnamed namespace

namespace folly {
namespace locked_tests {

template <class Mutex>
typename std::enable_if<folly::LockTraits<Mutex>::is_shared>::type
testBasicImpl() {
  folly::Locked<std::vector<int>, Mutex> obj;

  obj.wlock()->resize(1000);

  folly::Locked<std::vector<int>, Mutex> obj2{*obj.wlock()};
  EXPECT_EQ(obj2.rlock()->size(), 1000);

  {
    auto lockedObj = obj.wlock();
    lockedObj->push_back(10);
    EXPECT_EQ(lockedObj->size(), 1001);
    EXPECT_EQ(lockedObj->back(), 10);
    EXPECT_EQ(obj2.wlock()->size(), 1000);

    {
      auto unlocker = lockedObj.scopedUnlock();
      EXPECT_EQ(obj.rlock()->size(), 1001);
    }
  }

  {
    auto lockedObj = obj.rlock();
    EXPECT_EQ(lockedObj->size(), 1001);
    {
      auto unlocker = lockedObj.scopedUnlock();
      EXPECT_EQ(obj.rlock()->size(), 1001);
    }
  }

  {
    auto lockedObj = obj.wlock();
    lockedObj->front() = 2;
  }

  EXPECT_EQ(obj.rlock()->size(), 1001);
  EXPECT_EQ(obj.rlock()->back(), 10);
  EXPECT_EQ(obj2.rlock()->size(), 1000);

  auto size = obj.withWLock([](std::vector<int>& vec) {
    vec.push_back(9);
    return vec.size();
  });

  EXPECT_EQ(size, 1002);
  EXPECT_EQ(obj.rlock()->size(), 1002);
  EXPECT_EQ(obj.rlock()->back(), 9);

  auto size2 = obj.withRLock([](const std::vector<int>& vec) {
    EXPECT_EQ(vec.size(), 1002);
    return vec.size();
  });
  EXPECT_EQ(size2, 1002);

  const auto& constObj = obj;
  auto size3 = constObj.withWLock([](const std::vector<int>& vec) {
    EXPECT_EQ(vec.back(), 9);
    return vec.size();
  });
  EXPECT_EQ(size3, 1002);

  // Note: gcc 4.8 doesn't support generic lambdas, so we can't use auto&&
  // here as the argument type yet.
  //
  // However, even with generic lambdas, this still isn't quite convenient to
  // use, since withWLockPtr() has both const and non-const overloads, and we
  // can't really use an auto argument since it the compiler can't tell
  // which overload its should prefer.
  auto size4 = obj.withWLockPtr(
      [](typename folly::Locked<std::vector<int>, Mutex>::LockedPtr&& vec) {
        vec->push_back(13);
        return vec->size();
      });
  EXPECT_EQ(size4, 1003);
  EXPECT_EQ(obj.rlock()->back(), 13);
}

template <class Mutex>
typename std::enable_if<!folly::LockTraits<Mutex>::is_shared>::type
testBasicImpl() {
  folly::Locked<std::vector<int>, Mutex> obj;

  obj.lock()->resize(1000);

  folly::Locked<std::vector<int>, Mutex> obj2{*obj.lock()};
  EXPECT_EQ(obj2.lock()->size(), 1000);

  {
    auto lockedObj = obj.lock();
    lockedObj->push_back(10);
    EXPECT_EQ(lockedObj->size(), 1001);
    EXPECT_EQ(lockedObj->back(), 10);
    EXPECT_EQ(obj2.lock()->size(), 1000);

    {
      auto unlocker = lockedObj.scopedUnlock();
      EXPECT_EQ(obj.lock()->size(), 1001);
    }
  }

  {
    auto lockedObj = obj.lock();
    EXPECT_EQ(lockedObj->size(), 1001);
    {
      auto unlocker = lockedObj.scopedUnlock();
      EXPECT_EQ(obj.lock()->size(), 1001);
    }
  }

  {
    auto lockedObj = obj.lock();
    lockedObj->front() = 2;
  }

  EXPECT_EQ(obj.lock()->size(), 1001);
  EXPECT_EQ(obj.lock()->back(), 10);
  EXPECT_EQ(obj2.lock()->size(), 1000);

  auto size = obj.withLock([](std::vector<int>& vec) {
    vec.push_back(9);
    return vec.size();
  });

  EXPECT_EQ(size, 1002);
  EXPECT_EQ(obj.lock()->size(), 1002);
  EXPECT_EQ(obj.lock()->back(), 9);

  auto size2 = obj.withLock([](const std::vector<int>& vec) {
    EXPECT_EQ(vec.size(), 1002);
    return vec.size();
  });
  EXPECT_EQ(size2, 1002);

  const auto& constObj = obj;
  auto size3 = constObj.withLock([](const std::vector<int>& vec) {
    EXPECT_EQ(vec.back(), 9);
    return vec.size();
  });
  EXPECT_EQ(size3, 1002);

  auto size4 = obj.withLockPtr(
      [](typename folly::Locked<std::vector<int>, Mutex>::LockedPtr&& vec) {
        vec->push_back(13);
        return vec->size();
      });
  EXPECT_EQ(size4, 1003);
  EXPECT_EQ(obj.lock()->back(), 13);
}

template <class Mutex>
void testBasic() {
  testBasicImpl<Mutex>();
}

/*
 * Run a functon simultaneously in a number of different threads.
 *
 * The function will be passed the index number of the thread it is running in.
 * This function makes an attempt to synchronize the start of the threads as
 * best as possible.  (It waits for all threads to be allocated and started
 * before invoking the function.)
 */
template <class Function>
void runParallel(size_t numThreads, const Function& function) {
  std::vector<std::thread> threads;

  // Variables used to synchronize all threads to try and start them
  // as close to the same time as possible
  folly::Locked<size_t> threadsReady(0);
  std::condition_variable readyCV;
  folly::Locked<bool> go(false);
  std::condition_variable goCV;

  auto worker = [&](size_t threadIndex) {
    // Signal that we are ready
    ++(*threadsReady.lock());
    readyCV.notify_one();

    // Wait until we are given the signal to start
    // The purpose of this is to try and make sure all threads start
    // as close to the same time as possible.
    {
      auto lockedGo = go.lock();
      while (!*lockedGo) {
        goCV.wait(lockedGo.getUniqueLock());
      }
    }

    function(threadIndex);
  };

  // Start all of the threads
  for (size_t threadIndex = 0; threadIndex < numThreads; ++threadIndex) {
    threads.push_back(
        std::thread([threadIndex, &worker]() { worker(threadIndex); }));
  }

  // Wait for all threads to become ready
  {
    auto readyLocked = threadsReady.lock();
    while (*readyLocked < numThreads) {
      readyCV.wait(readyLocked.getUniqueLock());
    }
    go = true;
  }
  // Now signal the threads that they can go
  goCV.notify_all();

  // Wait for all threads to finish
  for (auto& thread : threads) {
    thread.join();
  }
}

template <class Mutex>
void testConcurrency() {
  folly::Locked<std::vector<int>, Mutex> v;
  static const size_t numThreads = 100;
  // Note: I initially tried using itersPerThread = 1000,
  // which works fine for most lock types, but std::shared_timed_mutex
  // appears to be extraordinarily slow.  It could take around 30 seconds
  // to run this test with 1000 iterations per thread using shared_timed_mutex.
  static const size_t itersPerThread = 100;

  auto pushNumbers = [&](size_t threadIdx) {
    // Test lock()
    for (size_t n = 0; n < itersPerThread; ++n) {
      v.contextualLock()->push_back((itersPerThread * threadIdx) + n);
      sched_yield();
    }
  };
  runParallel(numThreads, pushNumbers);

  std::vector<int> result;
  v.swapData(result);

  EXPECT_EQ(result.size(), itersPerThread * numThreads);
  sort(result.begin(), result.end());

  for (size_t i = 0; i < itersPerThread * numThreads; ++i) {
    EXPECT_EQ(result[i], i);
  }
}

template <class Mutex>
void testDualLocking() {
  folly::Locked<std::vector<int>, Mutex> v;
  folly::Locked<std::map<int, int>, Mutex> m;

  auto dualLockWorker = [&](size_t threadIdx) {
    if (threadIdx & 1) {
      auto ret = folly::acquireLocked(v, m);
      std::get<0>(ret)->push_back(threadIdx);
      (*std::get<1>(ret))[threadIdx] = threadIdx + 1;
    } else {
      auto ret = folly::acquireLocked(m, v);
      std::get<1>(ret)->push_back(threadIdx);
      (*std::get<0>(ret))[threadIdx] = threadIdx + 1;
    }
  };

  static const size_t numThreads = 100;
  runParallel(numThreads, dualLockWorker);

  std::vector<int> result;
  v.swapData(result);

  EXPECT_EQ(result.size(), numThreads);
  sort(result.begin(), result.end());

  for (size_t i = 0; i < numThreads; ++i) {
    EXPECT_EQ(result[i], i);
  }
}

template <class Mutex>
void testDualLockingShared() {
  folly::Locked<std::vector<int>, Mutex> v;
  folly::Locked<std::map<int, int>, Mutex> m;

  auto dualLockWorker = [&](size_t threadIdx) {
    if (threadIdx & 1) {
      auto ret = folly::acquireLocked(v, m);
      (void)std::get<1>(ret)->size();
      std::get<0>(ret)->push_back(threadIdx);
    } else {
      auto ret = folly::acquireLocked(m, v);
      (void)std::get<0>(ret)->size();
      std::get<1>(ret)->push_back(threadIdx);
    }
  };

  static const size_t numThreads = 100;
  runParallel(numThreads, dualLockWorker);

  std::vector<int> result;
  v.swapData(result);

  EXPECT_EQ(result.size(), numThreads);
  sort(result.begin(), result.end());

  for (size_t i = 0; i < numThreads; ++i) {
    EXPECT_EQ(result[i], i);
  }
}

template <class Mutex>
void testTimed() {
  folly::Locked<std::vector<int>, Mutex> v;
  folly::Locked<uint64_t, Mutex> numTimeouts;

  auto worker = [&](size_t threadIdx) {
    // Test directly using operator-> on the lock result
    v.contextualLock()->push_back(2 * threadIdx);

    // Test using lock with a timeout
    for (;;) {
      auto lv = v.contextualLock(std::chrono::milliseconds(5));
      if (!lv) {
        ++(*numTimeouts.contextualLock());
        continue;
      }

      // Sleep for a random time to ensure we trigger timeouts in other threads
      randomSleep(std::chrono::milliseconds(5), std::chrono::milliseconds(15));
      lv->push_back(2 * threadIdx + 1);
      break;
    }
  };

  static const size_t numThreads = 100;
  runParallel(numThreads, worker);

  std::vector<int> result;
  v.swapData(result);

  EXPECT_EQ(result.size(), 2 * numThreads);
  sort(result.begin(), result.end());

  for (size_t i = 0; i < 2 * numThreads; ++i) {
    EXPECT_EQ(result[i], i);
  }
  // We generally expect a large number of number timeouts here.
  // I'm not adding a check for it since it's theoretically possible that
  // we might get 0 timeouts depending on the CPU scheduling if our threads
  // don't get to run very often.
  LOG(INFO) << "testTimedSynchronized: " << *(numTimeouts.contextualRLock())
            << " timeouts";
}

template <class Mutex>
void testTimedSynchronizedRW() {
  folly::Locked<std::vector<int>, Mutex> v;
  folly::Locked<uint64_t, Mutex> numTimeouts(0);

  auto worker = [&](size_t threadIdx) {
    // Test directly using operator-> on the lock result
    v.wlock()->push_back(threadIdx);

    // Test lock() with a timeout
    for (;;) {
      auto lv = v.rlock(std::chrono::milliseconds(10));
      if (!lv) {
        ++(*numTimeouts.wlock());
        continue;
      }

      // Sleep while holding the lock.
      //
      // This will block other threads from acquiring the write lock to add
      // their thread index to v, but it won't block threads that have entered
      // the for loop and are trying to acquire a read lock.
      //
      // For lock types that give preference to readers rather than writers,
      // this will tend to serialize all threads on the wlock() above.
      randomSleep(std::chrono::milliseconds(5), std::chrono::milliseconds(15));
      auto found = std::find(lv->begin(), lv->end(), threadIdx);
      CHECK(found != lv->end());
      break;
    }
  };

  static const size_t numThreads = 100;
  runParallel(numThreads, worker);

  std::vector<int> result;
  v.swapData(result);

  EXPECT_EQ(result.size(), numThreads);
  sort(result.begin(), result.end());

  for (size_t i = 0; i < numThreads; ++i) {
    EXPECT_EQ(result[i], i);
  }
  // We generally expect a small number of timeouts here.
  // For locks that give readers preference over writers this should usually
  // be 0.  With locks that give writers preference we do see a small-ish
  // number of read timeouts.
  LOG(INFO) << "testTimedSynchronizedRW: " << *numTimeouts.rlock()
            << " timeouts";
}

template <class Mutex> void testConstCopy() {
  std::vector<int> input = {1, 2, 3};
  const folly::Locked<std::vector<int>, Mutex> v(input);

  std::vector<int> result;

  v.copy(&result);
  EXPECT_EQ(result, input);

  result = v.copy();
  EXPECT_EQ(result, input);
}

struct NotCopiableNotMovable {
  NotCopiableNotMovable(int, const char*) {}
  NotCopiableNotMovable(const NotCopiableNotMovable&) = delete;
  NotCopiableNotMovable& operator=(const NotCopiableNotMovable&) = delete;
  NotCopiableNotMovable(NotCopiableNotMovable&&) = delete;
  NotCopiableNotMovable& operator=(NotCopiableNotMovable&&) = delete;
};

template <class Mutex> void testInPlaceConstruction() {
  // This won't compile without construct_in_place
  folly::Locked<NotCopiableNotMovable> a(5, "a");
}
} // locked_tests
} // folly
