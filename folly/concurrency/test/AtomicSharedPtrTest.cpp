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

// AtomicSharedPtr-detail.h only works with libstdc++, so skip these tests for
// other vendors
// PackedSyncPtr requires x64, ppc64 or aarch64, skip these tests for
// other arches
#include <folly/Portability.h>
#include <folly/portability/Config.h>
#if defined(__GLIBCXX__) && (FOLLY_X64 || FOLLY_PPC64 || FOLLY_AARCH64)

#include <folly/concurrency/test/AtomicSharedPtrCounted.h>

#include <atomic>
#include <memory>
#include <thread>

#include <folly/concurrency/AtomicSharedPtr.h>
#include <folly/portability/GFlags.h>
#include <folly/portability/GTest.h>

#include <folly/test/DeterministicSchedule.h>

using namespace folly;
using namespace folly::test;
using namespace std;
static int c_count{0};
static int d_count{0};

using DSched = DeterministicSchedule;

DEFINE_int64(seed, 0, "Seed for random number generators");
DEFINE_int32(num_threads, 32, "Number of threads");

struct foo {
  foo() { c_count++; }
  ~foo() { d_count++; }
};

TEST(AtomicSharedPtr, operators) {
  atomic_shared_ptr<int> fooptr;
  EXPECT_TRUE(fooptr.is_lock_free());
  auto i = new int(5);
  std::shared_ptr<int> s(i);
  fooptr.store(s);
  shared_ptr<int> bar(fooptr);
  EXPECT_TRUE(fooptr.compare_exchange_strong(s, nullptr));
  s.reset();
  bar.reset();
}

TEST(AtomicSharedPtr, exchange) {
  atomic_shared_ptr<int> fooptr;
  auto a = make_shared<int>(1);
  fooptr.store(std::move(a));
  auto b = fooptr.exchange(make_shared<int>());
  EXPECT_EQ(*b, 1);
}

TEST(AtomicSharedPtr, foo) {
  c_count = 0;
  d_count = 0;
  {
    atomic_shared_ptr<foo> fooptr;
    fooptr.store(make_shared<foo>());
    EXPECT_EQ(1, c_count);
    EXPECT_EQ(0, d_count);
    {
      auto res = fooptr.load();
      EXPECT_EQ(1, c_count);
      EXPECT_EQ(0, d_count);
    }
    EXPECT_EQ(1, c_count);
    EXPECT_EQ(0, d_count);
  }
  EXPECT_EQ(1, c_count);
  EXPECT_EQ(1, d_count);
}

TEST(AtomicSharedPtr, counted) {
  c_count = 0;
  d_count = 0;
  {
    atomic_shared_ptr<foo, std::atomic, counted_ptr_internals<std::atomic>>
        fooptr;
    fooptr.store(make_counted<std::atomic, foo>());
    EXPECT_EQ(1, c_count);
    EXPECT_EQ(0, d_count);
    {
      auto res = fooptr.load();
      EXPECT_EQ(1, c_count);
      EXPECT_EQ(0, d_count);
    }
    EXPECT_EQ(1, c_count);
    EXPECT_EQ(0, d_count);
  }
  EXPECT_EQ(1, c_count);
  EXPECT_EQ(1, d_count);
}

TEST(AtomicSharedPtr, counted2) {
  auto foo = make_counted<std::atomic, bool>();
  atomic_shared_ptr<bool, std::atomic, counted_ptr_internals<std::atomic>>
      fooptr(foo);
  fooptr.store(foo);
  fooptr.load();
}

TEST(AtomicSharedPtr, ConstTest) {
  const auto a(std::make_shared<foo>());
  atomic_shared_ptr<foo> atom;
  atom.store(a);

  atomic_shared_ptr<const foo> catom;
}

TEST(AtomicSharedPtr, AliasingConstructorTest) {
  c_count = 0;
  d_count = 0;
  auto a = std::make_shared<foo>();
  auto b = new foo;
  auto alias = std::shared_ptr<foo>(a, b);

  atomic_shared_ptr<foo> asp;
  asp.store(alias);
  a.reset();
  alias.reset();
  auto res1 = asp.load();
  auto res2 = asp.exchange(nullptr);
  EXPECT_EQ(b, res1.get());
  EXPECT_EQ(b, res2.get());
  EXPECT_EQ(2, c_count);
  EXPECT_EQ(0, d_count);
  res1.reset();
  res2.reset();
  EXPECT_EQ(2, c_count);
  EXPECT_EQ(1, d_count);
  delete b;
  EXPECT_EQ(2, c_count);
  EXPECT_EQ(2, d_count);
}

TEST(AtomicSharedPtr, AliasingWithNoControlBlockConstructorTest) {
  long value = 0;
  atomic_shared_ptr<long> ptr{shared_ptr<long>{shared_ptr<long>{}, &value}};
  EXPECT_EQ(ptr.load().get(), &value);
}

TEST(AtomicSharedPtr, AliasingWithNullptrConstructorTest) {
  atomic_shared_ptr<foo> ptr{shared_ptr<foo>{std::make_shared<foo>(), nullptr}};
  EXPECT_EQ(ptr.load().get(), nullptr);
  // Verify that atomic_shared_ptr is holding the underlying object.
  EXPECT_EQ(d_count, 0);
  ptr.store({});
  EXPECT_EQ(d_count, 1);
}

TEST(AtomicSharedPtr, MaxPtrs) {
  shared_ptr<long> p(new long);
  int max_atomic_shared_ptrs = 262144;
  atomic_shared_ptr<long> ptrs[max_atomic_shared_ptrs];
  for (int i = 0; i < max_atomic_shared_ptrs - 1; i++) {
    ptrs[i].store(p);
  }
  atomic_shared_ptr<long> fail;
  EXPECT_DEATH(fail.store(p), "");
}

TEST(AtomicSharedPtr, DeterministicTest) {
  DSched sched(DSched::uniform(FLAGS_seed));

  auto foo = make_counted<DeterministicAtomic, bool>();
  atomic_shared_ptr<
      bool,
      DeterministicAtomic,
      counted_ptr_internals<DeterministicAtomic>>
      fooptr(foo);
  std::vector<std::thread> threads(FLAGS_num_threads);
  for (int tid = 0; tid < FLAGS_num_threads; ++tid) {
    threads[tid] = DSched::thread([&]() {
      for (int i = 0; i < 1000; i++) {
        auto l = fooptr.load();
        EXPECT_TRUE(l.get() != nullptr);
        fooptr.compare_exchange_strong(l, l);
        fooptr.store(make_counted<DeterministicAtomic, bool>());
        EXPECT_FALSE(fooptr.compare_exchange_strong(
            l, make_counted<DeterministicAtomic, bool>()));
      }
    });
  }
  for (auto& t : threads) {
    DSched::join(t);
  }
}

TEST(AtomicSharedPtr, StressTest) {
  constexpr size_t kExternalOffset = 0x2000;

  atomic_shared_ptr<bool> ptr;
  std::atomic<size_t> num_loads = 0;
  std::atomic<size_t> num_stores = 0;

  // DeterministicSchedule is too slow for the number of iterations required
  // here, and we need a test that exercises the native atomics.
  std::vector<std::thread> threads;
  for (int tid = 0; tid < FLAGS_num_threads; ++tid) {
    threads.emplace_back([&] {
      shared_ptr<bool> v;
      for (size_t i = 0; i < 16 * kExternalOffset; ++i) {
        // Each time we've gone through a few local -> global batches, replace
        // the pointer to contend with other load()s. This also does the first
        // initialization, and a few threads may see nullptr for a while.
        if (num_loads++ % (kExternalOffset * 2) == 0) {
          auto newv = std::make_shared<bool>();
          // Alternate between store and CAS.
          if (num_stores++ % 2 == 0) {
            ptr.store(std::move(newv), std::memory_order_release);
          } else {
            v = ptr.load(std::memory_order_relaxed);
            ptr.compare_exchange_strong(
                v,
                std::move(newv),
                std::memory_order_acq_rel,
                std::memory_order_relaxed);
          }
        }
        // Increments the local count and decrements the external one,
        // eventually forcing a batch transfer.
        v = ptr.load(std::memory_order_acquire);
      }
    });
  }
  for (auto& t : threads) {
    t.join();
  }
}

TEST(AtomicSharedPtr, Leak) {
  static auto& ptr = *new atomic_shared_ptr<int>();
  ptr.store(std::make_shared<int>(3), std::memory_order_relaxed);
  EXPECT_EQ(3, *ptr.load(std::memory_order_relaxed));
}

#endif // defined(__GLIBCXX__)
