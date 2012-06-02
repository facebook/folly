/*
 * Copyright 2012 Facebook, Inc.
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

#include "folly/ThreadLocal.h"

#include <map>
#include <unordered_map>
#include <set>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <boost/thread/tss.hpp>
#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include "folly/Benchmark.h"

using namespace folly;

struct Widget {
  static int totalVal_;
  int val_;
  ~Widget() {
    totalVal_ += val_;
  }

  static void customDeleter(Widget* w, TLPDestructionMode mode) {
    totalVal_ += (mode == TLPDestructionMode::ALL_THREADS) * 1000;
    delete w;
  }
};
int Widget::totalVal_ = 0;

TEST(ThreadLocalPtr, BasicDestructor) {
  Widget::totalVal_ = 0;
  ThreadLocalPtr<Widget> w;
  std::thread([&w]() {
      w.reset(new Widget());
      w.get()->val_ += 10;
    }).join();
  EXPECT_EQ(10, Widget::totalVal_);
}

TEST(ThreadLocalPtr, CustomDeleter1) {
  Widget::totalVal_ = 0;
  {
    ThreadLocalPtr<Widget> w;
    std::thread([&w]() {
        w.reset(new Widget(), Widget::customDeleter);
        w.get()->val_ += 10;
      }).join();
    EXPECT_EQ(10, Widget::totalVal_);
  }
  EXPECT_EQ(10, Widget::totalVal_);
}

// Test deleting the ThreadLocalPtr object
TEST(ThreadLocalPtr, CustomDeleter2) {
  Widget::totalVal_ = 0;
  std::thread t;
  std::mutex mutex;
  std::condition_variable cv;
  enum class State {
    START,
    DONE,
    EXIT
  };
  State state = State::START;
  {
    ThreadLocalPtr<Widget> w;
    t = std::thread([&]() {
        w.reset(new Widget(), Widget::customDeleter);
        w.get()->val_ += 10;

        // Notify main thread that we're done
        {
          std::unique_lock<std::mutex> lock(mutex);
          state = State::DONE;
          cv.notify_all();
        }

        // Wait for main thread to allow us to exit
        {
          std::unique_lock<std::mutex> lock(mutex);
          while (state != State::EXIT) {
            cv.wait(lock);
          }
        }
    });

    // Wait for main thread to start (and set w.get()->val_)
    {
      std::unique_lock<std::mutex> lock(mutex);
      while (state != State::DONE) {
        cv.wait(lock);
      }
    }

    // Thread started but hasn't exited yet
    EXPECT_EQ(0, Widget::totalVal_);

    // Destroy ThreadLocalPtr<Widget> (by letting it go out of scope)
  }

  EXPECT_EQ(1010, Widget::totalVal_);

  // Allow thread to exit
  {
    std::unique_lock<std::mutex> lock(mutex);
    state = State::EXIT;
    cv.notify_all();
  }
  t.join();

  EXPECT_EQ(1010, Widget::totalVal_);
}

TEST(ThreadLocal, BasicDestructor) {
  Widget::totalVal_ = 0;
  ThreadLocal<Widget> w;
  std::thread([&w]() { w->val_ += 10; }).join();
  EXPECT_EQ(10, Widget::totalVal_);
}

TEST(ThreadLocal, SimpleRepeatDestructor) {
  Widget::totalVal_ = 0;
  {
    ThreadLocal<Widget> w;
    w->val_ += 10;
  }
  {
    ThreadLocal<Widget> w;
    w->val_ += 10;
  }
  EXPECT_EQ(20, Widget::totalVal_);
}

TEST(ThreadLocal, InterleavedDestructors) {
  Widget::totalVal_ = 0;
  ThreadLocal<Widget>* w = NULL;
  int wVersion = 0;
  const int wVersionMax = 2;
  int thIter = 0;
  std::mutex lock;
  auto th = std::thread([&]() {
    int wVersionPrev = 0;
    while (true) {
      while (true) {
        std::lock_guard<std::mutex> g(lock);
        if (wVersion > wVersionMax) {
          return;
        }
        if (wVersion > wVersionPrev) {
          // We have a new version of w, so it should be initialized to zero
          EXPECT_EQ((*w)->val_, 0);
          break;
        }
      }
      std::lock_guard<std::mutex> g(lock);
      wVersionPrev = wVersion;
      (*w)->val_ += 10;
      ++thIter;
    }
  });
  FOR_EACH_RANGE(i, 0, wVersionMax) {
    int thIterPrev = 0;
    {
      std::lock_guard<std::mutex> g(lock);
      thIterPrev = thIter;
      delete w;
      w = new ThreadLocal<Widget>();
      ++wVersion;
    }
    while (true) {
      std::lock_guard<std::mutex> g(lock);
      if (thIter > thIterPrev) {
        break;
      }
    }
  }
  {
    std::lock_guard<std::mutex> g(lock);
    wVersion = wVersionMax + 1;
  }
  th.join();
  EXPECT_EQ(wVersionMax * 10, Widget::totalVal_);
}

class SimpleThreadCachedInt {

  class NewTag;
  ThreadLocal<int,NewTag> val_;

 public:
  void add(int val) {
    *val_ += val;
  }

  int read() {
    int ret = 0;
    for (const auto& i : val_.accessAllThreads()) {
      ret += i;
    }
    return ret;
  }
};

TEST(ThreadLocalPtr, AccessAllThreadsCounter) {
  const int kNumThreads = 10;
  SimpleThreadCachedInt stci;
  std::atomic<bool> run(true);
  std::atomic<int> totalAtomic(0);
  std::vector<std::thread> threads;
  for (int i = 0; i < kNumThreads; ++i) {
    threads.push_back(std::thread([&,i]() {
      stci.add(1);
      totalAtomic.fetch_add(1);
      while (run.load()) { usleep(100); }
    }));
  }
  while (totalAtomic.load() != kNumThreads) { usleep(100); }
  EXPECT_EQ(kNumThreads, stci.read());
  run.store(false);
  for (auto& t : threads) {
    t.join();
  }
}

TEST(ThreadLocal, resetNull) {
  ThreadLocal<int> tl;
  tl.reset(new int(4));
  EXPECT_EQ(*tl.get(), 4);
  tl.reset();
  EXPECT_EQ(*tl.get(), 0);
  tl.reset(new int(5));
  EXPECT_EQ(*tl.get(), 5);
}

namespace {
struct Tag {};

struct Foo {
  folly::ThreadLocal<int, Tag> tl;
};
}  // namespace

TEST(ThreadLocal, Movable1) {
  Foo a;
  Foo b;
  EXPECT_TRUE(a.tl.get() != b.tl.get());

  a = Foo();
  b = Foo();
  EXPECT_TRUE(a.tl.get() != b.tl.get());
}

TEST(ThreadLocal, Movable2) {
  std::map<int, Foo> map;

  map[42];
  map[10];
  map[23];
  map[100];

  std::set<void*> tls;
  for (auto& m : map) {
    tls.insert(m.second.tl.get());
  }

  // Make sure that we have 4 different instances of *tl
  EXPECT_EQ(4, tls.size());
}

// Simple reference implementation using pthread_get_specific
template<typename T>
class PThreadGetSpecific {
 public:
  PThreadGetSpecific() : key_(0) {
    pthread_key_create(&key_, OnThreadExit);
  }

  T* get() const {
    return static_cast<T*>(pthread_getspecific(key_));
  }

  void reset(T* t) {
    delete get();
    pthread_setspecific(key_, t);
  }
  static void OnThreadExit(void* obj) {
    delete static_cast<T*>(obj);
  }
 private:
  pthread_key_t key_;
};

DEFINE_int32(numThreads, 8, "Number simultaneous threads for benchmarks.");

#define REG(var)                                                \
  BENCHMARK(FB_CONCATENATE(BM_mt_, var), iters) {               \
    const int itersPerThread = iters / FLAGS_numThreads;        \
    std::vector<std::thread> threads;                           \
    for (int i = 0; i < FLAGS_numThreads; ++i) {                \
      threads.push_back(std::thread([&]() {                     \
        var.reset(new int(0));                                  \
        for (int i = 0; i < itersPerThread; ++i) {              \
          ++(*var.get());                                       \
        }                                                       \
      }));                                                      \
    }                                                           \
    for (auto& t : threads) {                                   \
      t.join();                                                 \
    }                                                           \
  }

ThreadLocalPtr<int> tlp;
REG(tlp);
PThreadGetSpecific<int> pthread_get_specific;
REG(pthread_get_specific);
boost::thread_specific_ptr<int> boost_tsp;
REG(boost_tsp);
BENCHMARK_DRAW_LINE();

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::SetCommandLineOptionWithMode(
    "bm_max_iters", "100000000", google::SET_FLAG_IF_DEFAULT
  );
  if (FLAGS_benchmark) {
    folly::runBenchmarks();
  }
  return RUN_ALL_TESTS();
}

/*
Ran with 24 threads on dual 12-core Xeon(R) X5650 @ 2.67GHz with 12-MB caches

Benchmark                               Iters   Total t    t/iter iter/sec
------------------------------------------------------------------------------
*       BM_mt_tlp                   100000000  39.88 ms  398.8 ps  2.335 G
 +5.91% BM_mt_pthread_get_specific  100000000  42.23 ms  422.3 ps  2.205 G
 + 295% BM_mt_boost_tsp             100000000  157.8 ms  1.578 ns  604.5 M
------------------------------------------------------------------------------
*/
