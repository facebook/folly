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
#include <folly/Random.h>
#include <folly/Synchronized.h>
#include <glog/logging.h>
#include <algorithm>
#include <condition_variable>
#include <functional>
#include <map>
#include <random>
#include <thread>
#include <vector>

namespace folly {
namespace sync_tests {

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

/*
 * Run a functon simultaneously in a number of different threads.
 *
 * The function will be passed the index number of the thread it is running in.
 * This function makes an attempt to synchronize the start of the threads as
 * best as possible.  It waits for all threads to be allocated and started
 * before invoking the function.
 */
template <class Function>
void runParallel(size_t numThreads, const Function& function) {
  std::vector<std::thread> threads;
  threads.reserve(numThreads);

  // Variables used to synchronize all threads to try and start them
  // as close to the same time as possible
  //
  // TODO: At the moment Synchronized doesn't work with condition variables.
  // Update this to use Synchronized once the condition_variable support lands.
  std::mutex threadsReadyMutex;
  size_t threadsReady = 0;
  std::condition_variable readyCV;
  std::mutex goMutex;
  bool go = false;
  std::condition_variable goCV;

  auto worker = [&](size_t threadIndex) {
    // Signal that we are ready
    {
      std::lock_guard<std::mutex> lock(threadsReadyMutex);
      ++threadsReady;
    }
    readyCV.notify_one();

    // Wait until we are given the signal to start
    // The purpose of this is to try and make sure all threads start
    // as close to the same time as possible.
    {
      std::unique_lock<std::mutex> lock(goMutex);
      goCV.wait(lock, [&] { return go; });
    }

    function(threadIndex);
  };

  // Start all of the threads
  for (size_t threadIndex = 0; threadIndex < numThreads; ++threadIndex) {
    threads.emplace_back([threadIndex, &worker]() { worker(threadIndex); });
  }

  // Wait for all threads to become ready
  {
    std::unique_lock<std::mutex> lock(threadsReadyMutex);
    readyCV.wait(lock, [&] { return threadsReady == numThreads; });
  }
  {
    std::lock_guard<std::mutex> lock(goMutex);
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
void testBasic() {
  folly::Synchronized<std::vector<int>, Mutex> obj;

  obj->resize(1000);

  auto obj2 = obj;
  EXPECT_EQ(1000, obj2->size());

  SYNCHRONIZED (obj) {
    obj.push_back(10);
    EXPECT_EQ(1001, obj.size());
    EXPECT_EQ(10, obj.back());
    EXPECT_EQ(1000, obj2->size());

    UNSYNCHRONIZED(obj) {
      EXPECT_EQ(1001, obj->size());
    }
  }

  SYNCHRONIZED_CONST (obj) {
    EXPECT_EQ(1001, obj.size());
    UNSYNCHRONIZED(obj) {
      EXPECT_EQ(1001, obj->size());
    }
  }

  SYNCHRONIZED (lockedObj, *&obj) {
    lockedObj.front() = 2;
  }

  EXPECT_EQ(1001, obj->size());
  EXPECT_EQ(10, obj->back());
  EXPECT_EQ(1000, obj2->size());

  EXPECT_EQ(FB_ARG_2_OR_1(1, 2), 2);
  EXPECT_EQ(FB_ARG_2_OR_1(1), 1);
}

template <class Mutex> void testConcurrency() {
  folly::Synchronized<std::vector<int>, Mutex> v;
  static const size_t numThreads = 100;
  // Note: I initially tried using itersPerThread = 1000,
  // which works fine for most lock types, but std::shared_timed_mutex
  // appears to be extraordinarily slow.  It could take around 30 seconds
  // to run this test with 1000 iterations per thread using shared_timed_mutex.
  static const size_t itersPerThread = 100;

  auto pushNumbers = [&](size_t threadIdx) {
    // Test lock()
    for (size_t n = 0; n < itersPerThread; ++n) {
      v->push_back((itersPerThread * threadIdx) + n);
      sched_yield();
    }
  };
  runParallel(numThreads, pushNumbers);

  std::vector<int> result;
  v.swap(result);

  EXPECT_EQ(numThreads * itersPerThread, result.size());
  sort(result.begin(), result.end());

  for (size_t i = 0; i < itersPerThread * numThreads; ++i) {
    EXPECT_EQ(i, result[i]);
  }
}

template <class Mutex> void testDualLocking() {
  folly::Synchronized<std::vector<int>, Mutex> v;
  folly::Synchronized<std::map<int, int>, Mutex> m;

  auto dualLockWorker = [&](size_t threadIdx) {
    if (threadIdx & 1) {
      SYNCHRONIZED_DUAL(lv, v, lm, m) {
        lv.push_back(threadIdx);
        lm[threadIdx] = threadIdx + 1;
      }
    } else {
      SYNCHRONIZED_DUAL(lm, m, lv, v) {
        lv.push_back(threadIdx);
        lm[threadIdx] = threadIdx + 1;
      }
    }
  };
  static const size_t numThreads = 100;
  runParallel(numThreads, dualLockWorker);

  std::vector<int> result;
  v.swap(result);

  EXPECT_EQ(numThreads, result.size());
  sort(result.begin(), result.end());

  for (size_t i = 0; i < numThreads; ++i) {
    EXPECT_EQ(i, result[i]);
  }
}

template <class Mutex> void testDualLockingWithConst() {
  folly::Synchronized<std::vector<int>, Mutex> v;
  folly::Synchronized<std::map<int, int>, Mutex> m;

  auto dualLockWorker = [&](size_t threadIdx) {
    const auto& cm = m;
    if (threadIdx & 1) {
      SYNCHRONIZED_DUAL(lv, v, lm, cm) {
        (void)lm.size();
        lv.push_back(threadIdx);
      }
    } else {
      SYNCHRONIZED_DUAL(lm, cm, lv, v) {
        (void)lm.size();
        lv.push_back(threadIdx);
      }
    }
  };
  static const size_t numThreads = 100;
  runParallel(numThreads, dualLockWorker);

  std::vector<int> result;
  v.swap(result);

  EXPECT_EQ(numThreads, result.size());
  sort(result.begin(), result.end());

  for (size_t i = 0; i < numThreads; ++i) {
    EXPECT_EQ(i, result[i]);
  }
}

template <class Mutex> void testTimedSynchronized() {
  folly::Synchronized<std::vector<int>, Mutex> v;
  folly::Synchronized<uint64_t, Mutex> numTimeouts;

  auto worker = [&](size_t threadIdx) {
    // Test operator->
    v->push_back(2 * threadIdx);

    // Aaand test the TIMED_SYNCHRONIZED macro
    for (;;)
      TIMED_SYNCHRONIZED(5, lv, v) {
        if (lv) {
          // Sleep for a random time to ensure we trigger timeouts
          // in other threads
          randomSleep(
              std::chrono::milliseconds(5), std::chrono::milliseconds(15));
          lv->push_back(2 * threadIdx + 1);
          return;
        }

        SYNCHRONIZED(numTimeouts) {
          ++numTimeouts;
        }
      }
  };

  static const size_t numThreads = 100;
  runParallel(numThreads, worker);

  std::vector<int> result;
  v.swap(result);

  EXPECT_EQ(2 * numThreads, result.size());
  sort(result.begin(), result.end());

  for (size_t i = 0; i < 2 * numThreads; ++i) {
    EXPECT_EQ(i, result[i]);
  }
  // We generally expect a large number of number timeouts here.
  // I'm not adding a check for it since it's theoretically possible that
  // we might get 0 timeouts depending on the CPU scheduling if our threads
  // don't get to run very often.
  uint64_t finalNumTimeouts = 0;
  SYNCHRONIZED(numTimeouts) {
    finalNumTimeouts = numTimeouts;
  }
  LOG(INFO) << "testTimedSynchronized: " << finalNumTimeouts << " timeouts";
}

template <class Mutex> void testTimedSynchronizedWithConst() {
  folly::Synchronized<std::vector<int>, Mutex> v;
  folly::Synchronized<uint64_t, Mutex> numTimeouts;

  auto worker = [&](size_t threadIdx) {
    // Test operator->
    v->push_back(threadIdx);

    // Test TIMED_SYNCHRONIZED_CONST
    for (;;) {
      TIMED_SYNCHRONIZED_CONST(10, lv, v) {
        if (lv) {
          // Sleep while holding the lock.
          //
          // This will block other threads from acquiring the write lock to add
          // their thread index to v, but it won't block threads that have
          // entered the for loop and are trying to acquire a read lock.
          //
          // For lock types that give preference to readers rather than writers,
          // this will tend to serialize all threads on the wlock() above.
          randomSleep(
              std::chrono::milliseconds(5), std::chrono::milliseconds(15));
          auto found = std::find(lv->begin(), lv->end(), threadIdx);
          CHECK(found != lv->end());
          return;
        } else {
          SYNCHRONIZED(numTimeouts) {
            ++numTimeouts;
          }
        }
      }
    }
  };

  static const size_t numThreads = 100;
  runParallel(numThreads, worker);

  std::vector<int> result;
  v.swap(result);

  EXPECT_EQ(numThreads, result.size());
  sort(result.begin(), result.end());

  for (size_t i = 0; i < numThreads; ++i) {
    EXPECT_EQ(i, result[i]);
  }
  // We generally expect a small number of timeouts here.
  // For locks that give readers preference over writers this should usually
  // be 0.  With locks that give writers preference we do see a small-ish
  // number of read timeouts.
  uint64_t finalNumTimeouts = 0;
  SYNCHRONIZED(numTimeouts) {
    finalNumTimeouts = numTimeouts;
  }
  LOG(INFO) << "testTimedSynchronizedWithConst: " << finalNumTimeouts
            << " timeouts";
}

template <class Mutex> void testConstCopy() {
  std::vector<int> input = {1, 2, 3};
  const folly::Synchronized<std::vector<int>, Mutex> v(input);

  std::vector<int> result;

  v.copy(&result);
  EXPECT_EQ(input, result);

  result = v.copy();
  EXPECT_EQ(input, result);
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
  folly::Synchronized<NotCopiableNotMovable> a(
    folly::construct_in_place, 5, "a"
  );
}
}
}
