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
/* -*- Mode: C++; tab-width: 2; c-basic-offset: 2; indent-tabs-mode: nil -*- */

#include <atomic>
#include <thread>
#include <mutex>
#include <folly/Memory.h>
#include <condition_variable>
#include <gtest/gtest.h>

#include <folly/ReadMostlySharedPtr.h>

using folly::ReadMostlySharedPtr;

// send SIGALRM to test process after this many seconds
const unsigned int TEST_TIMEOUT = 10;

class ReadMostlySharedPtrTest : public ::testing::Test {
 public:
  ReadMostlySharedPtrTest() {
    alarm(TEST_TIMEOUT);
  }
};

struct TestObject {
  int value;
  std::atomic<int>& counter;

  TestObject(int value, std::atomic<int>& counter)
      : value(value), counter(counter) {
    ++counter;
  }

  ~TestObject() {
    assert(counter.load() > 0);
    --counter;
  }
};

// One side calls requestAndWait(), the other side calls waitForRequest(),
// does something and calls completed().
class Coordinator {
 public:
  void requestAndWait() {
    {
      std::lock_guard<std::mutex> lock(mutex);
      assert(!is_requested);
      assert(!is_completed);
      is_requested = true;
    }
    cv.notify_all();
    {
      std::unique_lock<std::mutex> lock(mutex);
      cv.wait(lock, [&] { return is_completed; });
    }
  }

  void waitForRequest() {
    std::unique_lock<std::mutex> lock(mutex);
    assert(!is_completed);
    cv.wait(lock, [&] { return is_requested; });
  }

  void completed() {
    {
      std::lock_guard<std::mutex> lock(mutex);
      assert(is_requested);
      is_completed = true;
    }
    cv.notify_all();
  }

 private:
  bool is_requested = false;
  bool is_completed = false;
  std::condition_variable cv;
  std::mutex mutex;
};

TEST_F(ReadMostlySharedPtrTest, BasicStores) {
  ReadMostlySharedPtr<TestObject> ptr;

  // Store 1.
  std::atomic<int> cnt1{0};
  ptr.store(folly::make_unique<TestObject>(1, cnt1));
  EXPECT_EQ(1, cnt1.load());

  // Store 2, check that 1 is destroyed.
  std::atomic<int> cnt2{0};
  ptr.store(folly::make_unique<TestObject>(2, cnt2));
  EXPECT_EQ(1, cnt2.load());
  EXPECT_EQ(0, cnt1.load());

  // Store nullptr, check that 2 is destroyed.
  ptr.store(nullptr);
  EXPECT_EQ(0, cnt2.load());
}

TEST_F(ReadMostlySharedPtrTest, BasicLoads) {
  std::atomic<int> cnt2{0};
  ReadMostlySharedPtr<TestObject>::ReadPtr x;

  {
    ReadMostlySharedPtr<TestObject> ptr;

    // Check that ptr is initially nullptr.
    EXPECT_EQ(ptr.load(), nullptr);

    std::atomic<int> cnt1{0};
    ptr.store(folly::make_unique<TestObject>(1, cnt1));
    EXPECT_EQ(1, cnt1.load());

    x = ptr.load();
    EXPECT_EQ(1, x->value);

    ptr.store(folly::make_unique<TestObject>(2, cnt2));
    EXPECT_EQ(1, cnt2.load());
    EXPECT_EQ(1, cnt1.load());

    x = ptr.load();
    EXPECT_EQ(2, x->value);
    EXPECT_EQ(0, cnt1.load());

    ptr.store(nullptr);
    EXPECT_EQ(1, cnt2.load());
  }

  EXPECT_EQ(1, cnt2.load());

  x.reset();
  EXPECT_EQ(0, cnt2.load());
}

TEST_F(ReadMostlySharedPtrTest, LoadsFromThreads) {
  std::atomic<int> cnt{0};

  {
    ReadMostlySharedPtr<TestObject> ptr;
    Coordinator loads[7];

    std::thread t1([&] {
      loads[0].waitForRequest();
      EXPECT_EQ(ptr.load(), nullptr);
      loads[0].completed();

      loads[3].waitForRequest();
      EXPECT_EQ(2, ptr.load()->value);
      loads[3].completed();

      loads[4].waitForRequest();
      EXPECT_EQ(4, ptr.load()->value);
      loads[4].completed();

      loads[5].waitForRequest();
      EXPECT_EQ(5, ptr.load()->value);
      loads[5].completed();
    });

    std::thread t2([&] {
      loads[1].waitForRequest();
      EXPECT_EQ(1, ptr.load()->value);
      loads[1].completed();

      loads[2].waitForRequest();
      EXPECT_EQ(2, ptr.load()->value);
      loads[2].completed();

      loads[6].waitForRequest();
      EXPECT_EQ(5, ptr.load()->value);
      loads[6].completed();
    });

    loads[0].requestAndWait();

    ptr.store(folly::make_unique<TestObject>(1, cnt));
    loads[1].requestAndWait();

    ptr.store(folly::make_unique<TestObject>(2, cnt));
    loads[2].requestAndWait();
    loads[3].requestAndWait();

    ptr.store(folly::make_unique<TestObject>(3, cnt));
    ptr.store(folly::make_unique<TestObject>(4, cnt));
    loads[4].requestAndWait();

    ptr.store(folly::make_unique<TestObject>(5, cnt));
    loads[5].requestAndWait();
    loads[6].requestAndWait();

    EXPECT_EQ(1, cnt.load());

    t1.join();
    t2.join();
  }

  EXPECT_EQ(0, cnt.load());
}

TEST_F(ReadMostlySharedPtrTest, Ctor) {
  std::atomic<int> cnt1{0};
  {
    ReadMostlySharedPtr<TestObject> ptr(
      folly::make_unique<TestObject>(1, cnt1));

    EXPECT_EQ(1, ptr.load()->value);
  }

  EXPECT_EQ(0, cnt1.load());
}

TEST_F(ReadMostlySharedPtrTest, ClearingCache) {
  ReadMostlySharedPtr<TestObject> ptr;

  // Store 1.
  std::atomic<int> cnt1{0};
  ptr.store(folly::make_unique<TestObject>(1, cnt1));

  Coordinator c;

  std::thread t([&] {
    // Cache the pointer for this thread.
    ptr.load();
    c.requestAndWait();
  });

  // Wait for the thread to cache pointer.
  c.waitForRequest();
  EXPECT_EQ(1, cnt1.load());

  // Store 2 and check that 1 is destroyed.
  std::atomic<int> cnt2{0};
  ptr.store(folly::make_unique<TestObject>(2, cnt2));
  EXPECT_EQ(0, cnt1.load());

  // Unblock thread.
  c.completed();
  t.join();
}

TEST_F(ReadMostlySharedPtrTest, SlowDestructor) {
  struct Thingy {
    Coordinator* dtor;

    Thingy(Coordinator* dtor = nullptr) : dtor(dtor) {}

    ~Thingy() {
      if (dtor) {
        dtor->requestAndWait();
      }
    }
  };

  Coordinator dtor;

  ReadMostlySharedPtr<Thingy> ptr;
  ptr.store(folly::make_unique<Thingy>(&dtor));

  std::thread t([&] {
    // This will block in ~Thingy().
    ptr.store(folly::make_unique<Thingy>());
  });

  // Wait until store() in thread calls ~T().
  dtor.waitForRequest();
  // Do a store while another store() is stuck in ~T().
  ptr.store(folly::make_unique<Thingy>());
  // Let the other store() go.
  dtor.completed();

  t.join();
}

TEST_F(ReadMostlySharedPtrTest, StressTest) {
  const int ptr_count = 2;
  const int thread_count = 5;
  const std::chrono::milliseconds duration(100);
  const std::chrono::milliseconds upd_delay(1);
  const std::chrono::milliseconds respawn_delay(1);

  struct Instance {
    std::atomic<int> value{0};
    std::atomic<int> prev_value{0};
    ReadMostlySharedPtr<TestObject> ptr;
  };

  struct Thread {
    std::thread t;
    std::atomic<bool> shutdown{false};
  };

  std::atomic<int> counter(0);
  std::vector<Instance> instances(ptr_count);
  std::vector<Thread> threads(thread_count);
  std::atomic<int> seed(0);

  // Threads that call load() and checking value.
  auto thread_func = [&](int t) {
    pthread_setname_np(pthread_self(),
                       ("load" + folly::to<std::string>(t)).c_str());
    std::mt19937 rnd(++seed);
    while (!threads[t].shutdown.load()) {
      Instance& instance = instances[rnd() % instances.size()];
      int val1 = instance.prev_value.load();
      auto p = instance.ptr.load();
      int val = p ? p->value : 0;
      int val2 = instance.value.load();
      EXPECT_LE(val1, val);
      EXPECT_LE(val, val2);
    }
  };

  for (size_t t = 0; t < threads.size(); ++t) {
    threads[t].t = std::thread(thread_func, t);
  }

  std::atomic<bool> shutdown(false);

  // Thread that calls store() occasionally.
  std::thread update_thread([&] {
    pthread_setname_np(pthread_self(), "store");
    std::mt19937 rnd(++seed);
    while (!shutdown.load()) {
      Instance& instance = instances[rnd() % instances.size()];
      int val = ++instance.value;
      instance.ptr.store(folly::make_unique<TestObject>(val, counter));
      ++instance.prev_value;
      /* sleep override */
      std::this_thread::sleep_for(upd_delay);
    }
  });

  // Thread that joins and spawns load() threads occasionally.
  std::thread respawn_thread([&] {
    pthread_setname_np(pthread_self(), "respawn");
    std::mt19937 rnd(++seed);
    while (!shutdown.load()) {
      int t = rnd() % threads.size();
      threads[t].shutdown.store(true);
      threads[t].t.join();
      threads[t].shutdown.store(false);
      threads[t].t = std::thread(thread_func, t);

      /* sleep override */
      std::this_thread::sleep_for(respawn_delay);
    }
  });

  // Let all of this run for some time.
  /* sleep override */
  std::this_thread::sleep_for(duration);

  // Shut all of this down.
  shutdown.store(true);

  update_thread.join();
  respawn_thread.join();
  for (auto& t: threads) {
    t.shutdown.store(true);
    t.t.join();
  }

  for (auto& instance: instances) {
    instance.ptr.store(nullptr);
    EXPECT_EQ(instance.value.load(), instance.prev_value.load());
  }

  EXPECT_EQ(0, counter.load());
}
