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

#include <algorithm>
#include <random>
#include <functional>
#include <thread>
#include <vector>
#include <glog/logging.h>
#include <folly/Foreach.h>
#include <folly/Random.h>
#include <folly/Synchronized.h>


inline std::mt19937& getRNG() {
  static const auto seed = folly::randomNumberSeed();
  static std::mt19937 rng(seed);
  return rng;
}

template <class Integral1, class Integral2>
Integral2 random(Integral1 low, Integral2 up) {
  std::uniform_int_distribution<> range(low, up);
  return range(getRNG());
}

template <class Mutex>
void testBasic() {
  folly::Synchronized<std::vector<int>, Mutex> obj;

  obj->resize(1000);

  auto obj2 = obj;
  EXPECT_EQ(obj2->size(), 1000);

  SYNCHRONIZED (obj) {
    obj.push_back(10);
    EXPECT_EQ(obj.size(), 1001);
    EXPECT_EQ(obj.back(), 10);
    EXPECT_EQ(obj2->size(), 1000);

    UNSYNCHRONIZED(obj) {
      EXPECT_EQ(obj->size(), 1001);
    }
  }

  SYNCHRONIZED_CONST (obj) {
    EXPECT_EQ(obj.size(), 1001);
    UNSYNCHRONIZED(obj) {
      EXPECT_EQ(obj->size(), 1001);
    }
  }

  SYNCHRONIZED (lockedObj, *&obj) {
    lockedObj.front() = 2;
  }

  EXPECT_EQ(obj->size(), 1001);
  EXPECT_EQ(obj->back(), 10);
  EXPECT_EQ(obj2->size(), 1000);

  EXPECT_EQ(FB_ARG_2_OR_1(1, 2), 2);
  EXPECT_EQ(FB_ARG_2_OR_1(1), 1);
}

template <class Mutex> void testConcurrency() {
  folly::Synchronized<std::vector<int>, Mutex> v;

  struct Local {
    static bool threadMain(int i,
                           folly::Synchronized<std::vector<int>, Mutex>& pv) {
      usleep(::random(100 * 1000, 1000 * 1000));

      // Test operator->
      pv->push_back(2 * i);

      // Aaand test the SYNCHRONIZED macro
      SYNCHRONIZED (v, pv) {
        v.push_back(2 * i + 1);
      }

      return true;
    }
  };

  std::vector<std::thread> results;

  static const size_t threads = 100;
  FOR_EACH_RANGE (i, 0, threads) {
    results.push_back(std::thread([&, i]() { Local::threadMain(i, v); }));
  }

  FOR_EACH (i, results) {
    i->join();
  }

  std::vector<int> result;
  v.swap(result);

  EXPECT_EQ(result.size(), 2 * threads);
  sort(result.begin(), result.end());

  FOR_EACH_RANGE (i, 0, 2 * threads) {
    EXPECT_EQ(result[i], i);
  }
}

template <class Mutex> void testDualLocking() {
  folly::Synchronized<std::vector<int>, Mutex> v;
  folly::Synchronized<std::map<int, int>, Mutex> m;

  struct Local {
    static bool threadMain(
      int i,
      folly::Synchronized<std::vector<int>, Mutex>& pv,
      folly::Synchronized<std::map<int, int>, Mutex>& pm) {

      usleep(::random(100 * 1000, 1000 * 1000));

      if (i & 1) {
        SYNCHRONIZED_DUAL (v, pv, m, pm) {
          v.push_back(i);
          m[i] = i + 1;
        }
      } else {
        SYNCHRONIZED_DUAL (m, pm, v, pv) {
          v.push_back(i);
          m[i] = i + 1;
        }
      }

      return true;
    }
  };

  std::vector<std::thread> results;

  static const size_t threads = 100;
  FOR_EACH_RANGE (i, 0, threads) {
    results.push_back(
      std::thread([&, i]() { Local::threadMain(i, v, m); }));
  }

  FOR_EACH (i, results) {
    i->join();
  }

  std::vector<int> result;
  v.swap(result);

  EXPECT_EQ(result.size(), threads);
  sort(result.begin(), result.end());

  FOR_EACH_RANGE (i, 0, threads) {
    EXPECT_EQ(result[i], i);
  }
}

template <class Mutex> void testDualLockingWithConst() {
  folly::Synchronized<std::vector<int>, Mutex> v;
  folly::Synchronized<std::map<int, int>, Mutex> m;

  struct Local {
    static bool threadMain(
      int i,
      folly::Synchronized<std::vector<int>, Mutex>& pv,
      const folly::Synchronized<std::map<int, int>, Mutex>& pm) {

      usleep(::random(100 * 1000, 1000 * 1000));

      if (i & 1) {
        SYNCHRONIZED_DUAL (v, pv, m, pm) {
          (void)m.size();
          v.push_back(i);
        }
      } else {
        SYNCHRONIZED_DUAL (m, pm, v, pv) {
          (void)m.size();
          v.push_back(i);
        }
      }

      return true;
    }
  };

  std::vector<std::thread> results;

  static const size_t threads = 100;
  FOR_EACH_RANGE (i, 0, threads) {
    results.push_back(
      std::thread([&, i]() { Local::threadMain(i, v, m); }));
  }

  FOR_EACH (i, results) {
    i->join();
  }

  std::vector<int> result;
  v.swap(result);

  EXPECT_EQ(result.size(), threads);
  sort(result.begin(), result.end());

  FOR_EACH_RANGE (i, 0, threads) {
    EXPECT_EQ(result[i], i);
  }
}

template <class Mutex> void testTimedSynchronized() {
  folly::Synchronized<std::vector<int>, Mutex> v;

  struct Local {
    static bool threadMain(int i,
                           folly::Synchronized<std::vector<int>, Mutex>& pv) {
      usleep(::random(100 * 1000, 1000 * 1000));

      // Test operator->
      pv->push_back(2 * i);

      // Aaand test the TIMED_SYNCHRONIZED macro
      for (;;)
        TIMED_SYNCHRONIZED (10, v, pv) {
          if (v) {
            usleep(::random(15 * 1000, 150 * 1000));
            v->push_back(2 * i + 1);
            return true;
          }
          else {
            // do nothing
            usleep(::random(10 * 1000, 100 * 1000));
          }
        }

      return true;
    }
  };

  std::vector<std::thread> results;

  static const size_t threads = 100;
  FOR_EACH_RANGE (i, 0, threads) {
    results.push_back(std::thread([&, i]() { Local::threadMain(i, v); }));
  }

  FOR_EACH (i, results) {
    i->join();
  }

  std::vector<int> result;
  v.swap(result);

  EXPECT_EQ(result.size(), 2 * threads);
  sort(result.begin(), result.end());

  FOR_EACH_RANGE (i, 0, 2 * threads) {
    EXPECT_EQ(result[i], i);
  }
}

template <class Mutex> void testTimedSynchronizedWithConst() {
  folly::Synchronized<std::vector<int>, Mutex> v;

  struct Local {
    static bool threadMain(int i,
                           folly::Synchronized<std::vector<int>, Mutex>& pv) {
      usleep(::random(100 * 1000, 1000 * 1000));

      // Test operator->
      pv->push_back(i);

      usleep(::random(5 * 1000, 1000 * 1000));
      // Test TIMED_SYNCHRONIZED_CONST
      for (;;) {
        TIMED_SYNCHRONIZED_CONST (10, v, pv) {
          if (v) {
            auto found = std::find(v->begin(), v->end(),  i);
            CHECK(found != v->end());
            return true;
          } else {
            // do nothing
            usleep(::random(10 * 1000, 100 * 1000));
          }
        }
      }
    }
  };

  std::vector<std::thread> results;

  static const size_t threads = 100;
  FOR_EACH_RANGE (i, 0, threads) {
    results.push_back(std::thread([&, i]() { Local::threadMain(i, v); }));
  }

  FOR_EACH (i, results) {
    i->join();
  }

  std::vector<int> result;
  v.swap(result);

  EXPECT_EQ(result.size(), threads);
  sort(result.begin(), result.end());

  FOR_EACH_RANGE (i, 0, threads) {
    EXPECT_EQ(result[i], i);
  }
}

template <class Mutex> void testConstCopy() {
  std::vector<int> input = {1, 2, 3};
  const folly::Synchronized<std::vector<int>, Mutex> v(input);

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
  folly::Synchronized<NotCopiableNotMovable> a(
    folly::construct_in_place, 5, "a"
  );
}
