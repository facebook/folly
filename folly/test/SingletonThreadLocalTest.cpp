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

#ifndef _WIN32
#include <dlfcn.h>
#endif

#include <thread>
#include <unordered_set>
#include <vector>

#include <boost/thread/barrier.hpp>

#include <folly/SingletonThreadLocal.h>
#include <folly/String.h>
#include <folly/Synchronized.h>
#include <folly/experimental/TestUtil.h>
#include <folly/experimental/io/FsUtil.h>
#include <folly/lang/Keep.h>
#include <folly/portability/GTest.h>

using namespace folly;

extern "C" FOLLY_KEEP int* check() {
  return &SingletonThreadLocal<int>::get();
}

namespace {
static std::atomic<std::size_t> fooCreatedCount{0};
static std::atomic<std::size_t> fooDeletedCount{0};
struct Foo {
  Foo() { ++fooCreatedCount; }
  ~Foo() { ++fooDeletedCount; }
};
using FooSingletonTL = SingletonThreadLocal<Foo>;
} // namespace

TEST(SingletonThreadLocalTest, TryGet) {
  struct Foo {};
  using FooTL = SingletonThreadLocal<Foo>;
  EXPECT_EQ(nullptr, FooTL::try_get());
  FooTL::get();
  EXPECT_NE(nullptr, FooTL::try_get());
  EXPECT_EQ(&FooTL::get(), FooTL::try_get());
}

TEST(SingletonThreadLocalTest, OneSingletonPerThread) {
  static constexpr std::size_t targetThreadCount{64};
  std::atomic<std::size_t> completedThreadCount{0};
  Synchronized<std::unordered_set<Foo*>> fooAddresses{};
  std::vector<std::thread> threads{};
  auto threadFunction = [&fooAddresses, &completedThreadCount] {
    fooAddresses.wlock()->emplace(&FooSingletonTL::get());
    ++completedThreadCount;
    while (completedThreadCount < targetThreadCount) {
      std::this_thread::yield();
    }
  };
  {
    for (std::size_t threadCount{0}; threadCount < targetThreadCount;
         ++threadCount) {
      threads.emplace_back(threadFunction);
    }
  }
  for (auto& thread : threads) {
    thread.join();
  }
  EXPECT_EQ(threads.size(), fooAddresses.rlock()->size());
  EXPECT_EQ(threads.size(), fooCreatedCount);
  EXPECT_EQ(threads.size(), fooDeletedCount);
}

TEST(SingletonThreadLocalTest, DefaultMake) {
  struct Foo {
    int a = 4;
    Foo() = default;
    Foo(Foo&&) = delete;
    Foo& operator=(Foo&&) = delete;
  };
  struct Tag {};
  auto& single = SingletonThreadLocal<Foo, Tag>::get();
  EXPECT_EQ(4, single.a);
}

TEST(SingletonThreadLocalTest, SameTypeMake) {
  struct Foo {
    int a, b;
    Foo(int a_, int b_) : a(a_), b(b_) {}
    Foo(Foo&&) = default;
    Foo& operator=(Foo&&) = default;
  };
  struct Tag {};
  struct Make {
    Foo operator()() const { return Foo(3, 4); }
  };
  auto& single = SingletonThreadLocal<Foo, Tag, Make>::get();
  EXPECT_EQ(4, single.b);
}

TEST(SingletonThreadLocalTest, ReferenceConvertibleTypeMake) {
  struct Foo {
    int b = 3;
  };
  struct A {
    int a = 2;
  };
  struct C {
    int c = 4;
  };
  struct Bar : A, Foo, C {};
  struct Tag {};
  using Make = detail::DefaultMake<Bar>;
  auto& foo = SingletonThreadLocal<Foo, Tag, Make>::get();
  EXPECT_EQ(3, foo.b);
  auto& bar = static_cast<Bar&>(foo);
  EXPECT_EQ(2, bar.a);
}

TEST(SingletonThreadLocalTest, AccessAfterFastPathDestruction) {
  static std::atomic<int> counter{};
  struct Foo {
    int i = 3;
  };
  struct Bar {
    ~Bar() { counter += SingletonThreadLocal<Foo>::get().i; }
  };
  auto th = std::thread([] {
    SingletonThreadLocal<Bar>::get();
    counter += SingletonThreadLocal<Foo>::get().i;
  });
  th.join();
  EXPECT_EQ(6, counter);
}

TEST(ThreadLocal, DependencyTest) {
  typedef folly::ThreadLocalPtr<int> Data;

  struct mytag {};

  typedef SingletonThreadLocal<int> SingletonInt;
  struct barstruct {
    ~barstruct() {
      SingletonInt::get()++;
      Data data;
      data.reset(new int(0));
    }
  };
  typedef SingletonThreadLocal<barstruct, mytag> BarSingleton;

  std::thread([&]() {
    Data data;
    data.reset(new int(0));
    SingletonInt::get();
    BarSingleton::get();
  }).join();
}

TEST(SingletonThreadLocalTest, Reused) {
  for (auto i = 0u; i < 2u; ++i) {
    FOLLY_DECLARE_REUSED(data, std::string);
    if (i == 0u) {
      data = "hello";
    }
    EXPECT_EQ(i == 0u ? "hello" : "", data);
  }
}

TEST(SingletonThreadLocalTest, AccessAllThreads) {
  struct Tag {};
  struct Obj {
    size_t value;
  };
  using STL = SingletonThreadLocal<Obj, Tag>;
  constexpr size_t n_threads = 4;
  boost::barrier barrier{n_threads + 1};
  std::vector<std::thread> threads{n_threads};

  // setup
  std::atomic<size_t> count{0};
  for (auto& thread : threads) {
    thread = std::thread([&] {
      STL::get().value = ++count;
      barrier.wait();
      barrier.wait();
    });
  }
  barrier.wait();
  EXPECT_EQ(n_threads, count) << "sanity";

  // expectations
  size_t sum = 0;
  for (auto& obj : STL::accessAllThreads()) { // explicitly auto
    sum += obj.value;
  }
  EXPECT_EQ((n_threads * (n_threads + 1) / 2), sum);

  // cleanup
  barrier.wait();
  for (auto& thread : threads) {
    thread.join();
  }
}

#ifndef _WIN32
TEST(SingletonThreadLocalDeathTest, Overload) {
  auto const lib = folly::test::find_resource(
      "folly/test/singleton_thread_local_overload.so");

  auto message = stripLeftMargin(R"MESSAGE(
    Overloaded unique instance over <int, DeathTag, ...> with differing trailing arguments:
      folly::SingletonThreadLocal<int, DeathTag, folly::detail::DefaultMake<int>, TLTag1>
      folly::SingletonThreadLocal<int, DeathTag, folly::detail::DefaultMake<int>, TLTag2>
  )MESSAGE");
  EXPECT_DEATH(dlopen(lib.string().c_str(), RTLD_LAZY), message);
}
#endif
