/*
 * Copyright 2014 Facebook, Inc.
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

#include <algorithm>
#include <atomic>
#include <folly/small_vector.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <unistd.h>
#include "folly/wangle/Executor.h"
#include "folly/wangle/Future.h"

using namespace folly::wangle;
using std::pair;
using std::string;
using std::unique_ptr;
using std::vector;

#define EXPECT_TYPE(x, T) \
  EXPECT_TRUE((std::is_same<decltype(x), T>::value))

typedef WangleException eggs_t;
static eggs_t eggs("eggs");

// Future

TEST(Future, try) {
  class A {
   public:
    A(int x) : x_(x) {}

    int x() const {
      return x_;
    }
   private:
    int x_;
  };

  A a(5);
  Try<A> t_a(std::move(a));

  Try<void> t_void;

  EXPECT_EQ(5, t_a.value().x());
}

TEST(Future, special) {
  EXPECT_FALSE(std::is_copy_constructible<Future<int>>::value);
  EXPECT_FALSE(std::is_copy_assignable<Future<int>>::value);
  EXPECT_TRUE(std::is_move_constructible<Future<int>>::value);
  EXPECT_TRUE(std::is_move_assignable<Future<int>>::value);
}

TEST(Future, then) {
  bool flag = false;

  makeFuture<int>(42).then([&](Try<int>&& t) {
                              flag = true;
                              EXPECT_EQ(42, t.value());
                            });
  EXPECT_TRUE(flag); flag = false;

  makeFuture<int>(42)
    .then([](Try<int>&& t) { return t.value(); })
    .then([&](Try<int>&& t) { flag = true; EXPECT_EQ(42, t.value()); });
  EXPECT_TRUE(flag); flag = false;

  makeFuture().then([&](Try<void>&& t) { flag = true; t.value(); });
  EXPECT_TRUE(flag); flag = false;

  Promise<void> p;
  auto f = p.getFuture().then([&](Try<void>&& t) { flag = true; });
  EXPECT_FALSE(flag);
  EXPECT_FALSE(f.isReady());
  p.setValue();
  EXPECT_TRUE(flag);
  EXPECT_TRUE(f.isReady());
}

static string doWorkStatic(Try<string>&& t) {
  return t.value() + ";static";
}

TEST(Future, thenFunction) {
  struct Worker {
    string doWork(Try<string>&& t) {
      return t.value() + ";class";
    }
    static string doWorkStatic(Try<string>&& t) {
      return t.value() + ";class-static";
    }
  } w;

  auto f = makeFuture<string>("start")
    .then(doWorkStatic)
    .then(Worker::doWorkStatic)
    .then(&w, &Worker::doWork);

  EXPECT_EQ(f.value(), "start;static;class-static;class");
}

static Future<string> doWorkStaticFuture(Try<string>&& t) {
  return makeFuture(t.value() + ";static");
}

TEST(Future, thenFunctionFuture) {
  struct Worker {
    Future<string> doWorkFuture(Try<string>&& t) {
      return makeFuture(t.value() + ";class");
    }
    static Future<string> doWorkStaticFuture(Try<string>&& t) {
      return makeFuture(t.value() + ";class-static");
    }
  } w;

  auto f = makeFuture<string>("start")
    .then(doWorkStaticFuture)
    .then(Worker::doWorkStaticFuture)
    .then(&w, &Worker::doWorkFuture);

  EXPECT_EQ(f.value(), "start;static;class-static;class");
}

TEST(Future, value) {
  auto f = makeFuture(unique_ptr<int>(new int(42)));
  auto up = std::move(f.value());
  EXPECT_EQ(42, *up);

  EXPECT_THROW(makeFuture<int>(eggs).value(), eggs_t);
}

TEST(Future, isReady) {
  Promise<int> p;
  auto f = p.getFuture();
  EXPECT_FALSE(f.isReady());
  p.setValue(42);
  EXPECT_TRUE(f.isReady());
  }

TEST(Future, futureNotReady) {
  Promise<int> p;
  Future<int> f = p.getFuture();
  EXPECT_THROW(f.value(), eggs_t);
}

TEST(Future, hasException) {
  EXPECT_TRUE(makeFuture<int>(eggs).getTry().hasException());
  EXPECT_FALSE(makeFuture(42).getTry().hasException());
}

TEST(Future, hasValue) {
  EXPECT_TRUE(makeFuture(42).getTry().hasValue());
  EXPECT_FALSE(makeFuture<int>(eggs).getTry().hasValue());
}

TEST(Future, makeFuture) {
  EXPECT_TYPE(makeFuture(42), Future<int>);
  EXPECT_EQ(42, makeFuture(42).value());

  EXPECT_TYPE(makeFuture<float>(42), Future<float>);
  EXPECT_EQ(42, makeFuture<float>(42).value());

  auto fun = [] { return 42; };
  EXPECT_TYPE(makeFutureTry(fun), Future<int>);
  EXPECT_EQ(42, makeFutureTry(fun).value());

  auto failfun = []() -> int { throw eggs; };
  EXPECT_TYPE(makeFutureTry(failfun), Future<int>);
  EXPECT_THROW(makeFutureTry(failfun).value(), eggs_t);

  EXPECT_TYPE(makeFuture(), Future<void>);
}

// Promise

TEST(Promise, special) {
  EXPECT_FALSE(std::is_copy_constructible<Promise<int>>::value);
  EXPECT_FALSE(std::is_copy_assignable<Promise<int>>::value);
  EXPECT_TRUE(std::is_move_constructible<Promise<int>>::value);
  EXPECT_TRUE(std::is_move_assignable<Promise<int>>::value);
}

TEST(Promise, getFuture) {
  Promise<int> p;
  Future<int> f = p.getFuture();
  EXPECT_FALSE(f.isReady());
}

TEST(Promise, setValue) {
  Promise<int> fund;
  auto ffund = fund.getFuture();
  fund.setValue(42);
  EXPECT_EQ(42, ffund.value());

  struct Foo {
    string name;
    int value;
  };

  Promise<Foo> pod;
  auto fpod = pod.getFuture();
  Foo f = {"the answer", 42};
  pod.setValue(f);
  Foo f2 = fpod.value();
  EXPECT_EQ(f.name, f2.name);
  EXPECT_EQ(f.value, f2.value);

  pod = Promise<Foo>();
  fpod = pod.getFuture();
  pod.setValue(std::move(f2));
  Foo f3 = fpod.value();
  EXPECT_EQ(f.name, f3.name);
  EXPECT_EQ(f.value, f3.value);

  Promise<unique_ptr<int>> mov;
  auto fmov = mov.getFuture();
  mov.setValue(unique_ptr<int>(new int(42)));
  unique_ptr<int> ptr = std::move(fmov.value());
  EXPECT_EQ(42, *ptr);

  Promise<void> v;
  auto fv = v.getFuture();
  v.setValue();
  EXPECT_TRUE(fv.isReady());
}

TEST(Promise, setException) {
  {
    Promise<void> p;
    auto f = p.getFuture();
    p.setException(eggs);
    EXPECT_THROW(f.value(), eggs_t);
  }
  {
    Promise<void> p;
    auto f = p.getFuture();
    try {
      throw eggs;
    } catch (...) {
      p.setException(std::current_exception());
    }
    EXPECT_THROW(f.value(), eggs_t);
  }
}

TEST(Promise, fulfil) {
  {
    Promise<int> p;
    auto f = p.getFuture();
    p.fulfil([] { return 42; });
    EXPECT_EQ(42, f.value());
  }
  {
    Promise<int> p;
    auto f = p.getFuture();
    p.fulfil([]() -> int { throw eggs; });
    EXPECT_THROW(f.value(), eggs_t);
  }
}

TEST(Future, finish) {
  auto x = std::make_shared<int>(0);
  Promise<int> p;
  auto f = p.getFuture().then([x](Try<int>&& t) { *x = t.value(); });

  // The callback hasn't executed
  EXPECT_EQ(0, *x);

  // The callback has a reference to x
  EXPECT_EQ(2, x.use_count());

  p.setValue(42);

  // the callback has executed
  EXPECT_EQ(42, *x);

  // the callback has been destructed
  // and has released its reference to x
  EXPECT_EQ(1, x.use_count());
}

TEST(Future, unwrap) {
  Promise<int> a;
  Promise<int> b;

  auto fa = a.getFuture();
  auto fb = b.getFuture();

  bool flag1 = false;
  bool flag2 = false;

  // do a, then do b, and get the result of a + b.
  Future<int> f = fa.then([&](Try<int>&& ta) {
    auto va = ta.value();
    flag1 = true;
    return fb.then([va, &flag2](Try<int>&& tb) {
      flag2 = true;
      return va + tb.value();
    });
  });

  EXPECT_FALSE(flag1);
  EXPECT_FALSE(flag2);
  EXPECT_FALSE(f.isReady());

  a.setValue(3);
  EXPECT_TRUE(flag1);
  EXPECT_FALSE(flag2);
  EXPECT_FALSE(f.isReady());

  b.setValue(4);
  EXPECT_TRUE(flag1);
  EXPECT_TRUE(flag2);
  EXPECT_EQ(7, f.value());
}

TEST(Future, whenAll) {
  // returns a vector variant
  {
    vector<Promise<int>> promises(10);
    vector<Future<int>> futures;

    for (auto& p : promises)
      futures.push_back(p.getFuture());

    auto allf = whenAll(futures.begin(), futures.end());

    random_shuffle(promises.begin(), promises.end());
    for (auto& p : promises) {
      EXPECT_FALSE(allf.isReady());
      p.setValue(42);
    }

    EXPECT_TRUE(allf.isReady());
    auto& results = allf.value();
    for (auto& t : results) {
      EXPECT_EQ(42, t.value());
    }
  }

  // check error semantics
  {
    vector<Promise<int>> promises(4);
    vector<Future<int>> futures;

    for (auto& p : promises)
      futures.push_back(p.getFuture());

    auto allf = whenAll(futures.begin(), futures.end());


    promises[0].setValue(42);
    promises[1].setException(eggs);

    EXPECT_FALSE(allf.isReady());

    promises[2].setValue(42);

    EXPECT_FALSE(allf.isReady());

    promises[3].setException(eggs);

    EXPECT_TRUE(allf.isReady());
    EXPECT_FALSE(allf.getTry().hasException());

    auto& results = allf.value();
    EXPECT_EQ(42, results[0].value());
    EXPECT_TRUE(results[1].hasException());
    EXPECT_EQ(42, results[2].value());
    EXPECT_TRUE(results[3].hasException());
  }

  // check that futures are ready in then()
  {
    vector<Promise<void>> promises(10);
    vector<Future<void>> futures;

    for (auto& p : promises)
      futures.push_back(p.getFuture());

    auto allf = whenAll(futures.begin(), futures.end())
      .then([](Try<vector<Try<void>>>&& ts) {
        for (auto& f : ts.value())
          f.value();
      });

    random_shuffle(promises.begin(), promises.end());
    for (auto& p : promises)
      p.setValue();
    EXPECT_TRUE(allf.isReady());
  }
}


TEST(Future, whenAny) {
  {
    vector<Promise<int>> promises(10);
    vector<Future<int>> futures;

    for (auto& p : promises)
      futures.push_back(p.getFuture());

    for (auto& f : futures) {
      EXPECT_FALSE(f.isReady());
    }

    auto anyf = whenAny(futures.begin(), futures.end());

    /* futures were moved in, so these are invalid now */
    EXPECT_FALSE(anyf.isReady());

    promises[7].setValue(42);
    EXPECT_TRUE(anyf.isReady());
    auto& idx_fut = anyf.value();

    auto i = idx_fut.first;
    EXPECT_EQ(7, i);

    auto& f = idx_fut.second;
    EXPECT_EQ(42, f.value());
  }

  // error
  {
    vector<Promise<void>> promises(10);
    vector<Future<void>> futures;

    for (auto& p : promises)
      futures.push_back(p.getFuture());

    for (auto& f : futures) {
      EXPECT_FALSE(f.isReady());
    }

    auto anyf = whenAny(futures.begin(), futures.end());

    EXPECT_FALSE(anyf.isReady());

    promises[3].setException(eggs);
    EXPECT_TRUE(anyf.isReady());
    EXPECT_TRUE(anyf.value().second.hasException());
  }

  // then()
  {
    vector<Promise<int>> promises(10);
    vector<Future<int>> futures;

    for (auto& p : promises)
      futures.push_back(p.getFuture());

    auto anyf = whenAny(futures.begin(), futures.end())
      .then([](Try<pair<size_t, Try<int>>>&& f) {
        EXPECT_EQ(42, f.value().second.value());
      });

    promises[3].setValue(42);
    EXPECT_TRUE(anyf.isReady());
  }
}


TEST(when, already_completed) {
  {
    vector<Future<void>> fs;
    for (int i = 0; i < 10; i++)
      fs.push_back(makeFuture());

    whenAll(fs.begin(), fs.end())
      .then([&](Try<vector<Try<void>>>&& t) {
        EXPECT_EQ(fs.size(), t.value().size());
      });
  }
  {
    vector<Future<int>> fs;
    for (int i = 0; i < 10; i++)
      fs.push_back(makeFuture(i));

    whenAny(fs.begin(), fs.end())
      .then([&](Try<pair<size_t, Try<int>>>&& t) {
        auto& p = t.value();
        EXPECT_EQ(p.first, p.second.value());
      });
  }
}

TEST(when, whenN) {
  vector<Promise<void>> promises(10);
  vector<Future<void>> futures;

  for (auto& p : promises)
    futures.push_back(p.getFuture());

  bool flag = false;
  size_t n = 3;
  whenN(futures.begin(), futures.end(), n)
    .then([&](Try<vector<pair<size_t, Try<void>>>>&& t) {
      flag = true;
      auto v = t.value();
      EXPECT_EQ(n, v.size());
      for (auto& tt : v)
        EXPECT_TRUE(tt.second.hasValue());
    });

  promises[0].setValue();
  EXPECT_FALSE(flag);
  promises[1].setValue();
  EXPECT_FALSE(flag);
  promises[2].setValue();
  EXPECT_TRUE(flag);
}

/* Ensure that we can compile when_{all,any} with folly::small_vector */
TEST(when, small_vector) {

  static_assert(!FOLLY_IS_TRIVIALLY_COPYABLE(Future<void>),
                "Futures should not be trivially copyable");
  static_assert(!FOLLY_IS_TRIVIALLY_COPYABLE(Future<int>),
                "Futures should not be trivially copyable");

  using folly::small_vector;
  {
    small_vector<Future<void>> futures;

    for (int i = 0; i < 10; i++)
      futures.push_back(makeFuture());

    auto anyf = whenAny(futures.begin(), futures.end());
  }

  {
    small_vector<Future<void>> futures;

    for (int i = 0; i < 10; i++)
      futures.push_back(makeFuture());

    auto allf = whenAll(futures.begin(), futures.end());
  }
}

TEST(Future, whenAllVariadic) {
  Promise<bool> pb;
  Promise<int> pi;
  Future<bool> fb = pb.getFuture();
  Future<int> fi = pi.getFuture();
  bool flag = false;
  whenAll(std::move(fb), std::move(fi))
    .then([&](Try<std::tuple<Try<bool>, Try<int>>>&& t) {
      flag = true;
      EXPECT_TRUE(t.hasValue());
      EXPECT_TRUE(std::get<0>(t.value()).hasValue());
      EXPECT_EQ(std::get<0>(t.value()).value(), true);
      EXPECT_TRUE(std::get<1>(t.value()).hasValue());
      EXPECT_EQ(std::get<1>(t.value()).value(), 42);
    });
  pb.setValue(true);
  EXPECT_FALSE(flag);
  pi.setValue(42);
  EXPECT_TRUE(flag);
}

TEST(Future, whenAllVariadicReferences) {
  Promise<bool> pb;
  Promise<int> pi;
  Future<bool> fb = pb.getFuture();
  Future<int> fi = pi.getFuture();
  bool flag = false;
  whenAll(fb, fi)
    .then([&](Try<std::tuple<Try<bool>, Try<int>>>&& t) {
      flag = true;
      EXPECT_TRUE(t.hasValue());
      EXPECT_TRUE(std::get<0>(t.value()).hasValue());
      EXPECT_EQ(std::get<0>(t.value()).value(), true);
      EXPECT_TRUE(std::get<1>(t.value()).hasValue());
      EXPECT_EQ(std::get<1>(t.value()).value(), 42);
    });
  pb.setValue(true);
  EXPECT_FALSE(flag);
  pi.setValue(42);
  EXPECT_TRUE(flag);
}

TEST(Future, whenAll_none) {
  vector<Future<int>> fs;
  auto f = whenAll(fs.begin(), fs.end());
  EXPECT_TRUE(f.isReady());
}

TEST(Future, throwCaughtInImmediateThen) {
  // Neither of these should throw "Promise already satisfied"
  makeFuture().then(
    [=](Try<void>&&) -> int { throw std::exception(); });
  makeFuture().then(
    [=](Try<void>&&) -> Future<int> { throw std::exception(); });
}

TEST(Future, throwIfFailed) {
  makeFuture<void>(eggs)
    .then([=](Try<void>&& t) {
      EXPECT_THROW(t.throwIfFailed(), eggs_t);
    });
  makeFuture()
    .then([=](Try<void>&& t) {
      EXPECT_NO_THROW(t.throwIfFailed());
    });

  makeFuture<int>(eggs)
    .then([=](Try<int>&& t) {
      EXPECT_THROW(t.throwIfFailed(), eggs_t);
    });
  makeFuture<int>(42)
    .then([=](Try<int>&& t) {
      EXPECT_NO_THROW(t.throwIfFailed());
    });
}

TEST(Future, waitWithSemaphoreImmediate) {
  waitWithSemaphore(makeFuture());
  auto done = waitWithSemaphore(makeFuture(42)).value();
  EXPECT_EQ(42, done);

  vector<int> v{1,2,3};
  auto done_v = waitWithSemaphore(makeFuture(v)).value();
  EXPECT_EQ(v.size(), done_v.size());
  EXPECT_EQ(v, done_v);

  vector<Future<void>> v_f;
  v_f.push_back(makeFuture());
  v_f.push_back(makeFuture());
  auto done_v_f = waitWithSemaphore(whenAll(v_f.begin(), v_f.end())).value();
  EXPECT_EQ(2, done_v_f.size());

  vector<Future<bool>> v_fb;
  v_fb.push_back(makeFuture(true));
  v_fb.push_back(makeFuture(false));
  auto fut = whenAll(v_fb.begin(), v_fb.end());
  auto done_v_fb = std::move(waitWithSemaphore(std::move(fut)).value());
  EXPECT_EQ(2, done_v_fb.size());
}

TEST(Future, waitWithSemaphore) {
  Promise<int> p;
  Future<int> f = p.getFuture();
  std::atomic<bool> flag{false};
  std::atomic<int> result{1};
  std::atomic<std::thread::id> id;

  std::thread t([&](Future<int>&& tf){
      auto n = tf.then([&](Try<int> && t) {
          id = std::this_thread::get_id();
          return t.value();
        });
      flag = true;
      result.store(waitWithSemaphore(std::move(n)).value());
      LOG(INFO) << result;
    },
    std::move(f)
    );
  while(!flag){}
  EXPECT_EQ(result.load(), 1);
  p.setValue(42);
  t.join();
  // validate that the callback ended up executing in this thread, which
  // is more to ensure that this test actually tests what it should
  EXPECT_EQ(id, std::this_thread::get_id());
  EXPECT_EQ(result.load(), 42);
}

TEST(Future, waitWithSemaphoreForTime) {
 {
  Promise<int> p;
  Future<int> f = p.getFuture();
  auto t = waitWithSemaphore(std::move(f),
    std::chrono::microseconds(1));
  EXPECT_FALSE(t.isReady());
  p.setValue(1);
  EXPECT_TRUE(t.isReady());
 }
 {
  Promise<int> p;
  Future<int> f = p.getFuture();
  p.setValue(1);
  auto t = waitWithSemaphore(std::move(f),
    std::chrono::milliseconds(1));
  EXPECT_TRUE(t.isReady());
 }
 {
  vector<Future<bool>> v_fb;
  v_fb.push_back(makeFuture(true));
  v_fb.push_back(makeFuture(false));
  auto f = whenAll(v_fb.begin(), v_fb.end());
  auto t = waitWithSemaphore(std::move(f),
    std::chrono::milliseconds(1));
  EXPECT_TRUE(t.isReady());
  EXPECT_EQ(2, t.value().size());
 }
 {
  vector<Future<bool>> v_fb;
  Promise<bool> p1;
  Promise<bool> p2;
  v_fb.push_back(p1.getFuture());
  v_fb.push_back(p2.getFuture());
  auto f = whenAll(v_fb.begin(), v_fb.end());
  auto t = waitWithSemaphore(std::move(f),
    std::chrono::milliseconds(1));
  EXPECT_FALSE(t.isReady());
  p1.setValue(true);
  EXPECT_FALSE(t.isReady());
  p2.setValue(true);
  EXPECT_TRUE(t.isReady());
 }
 {
  Promise<int> p;
  Future<int> f = p.getFuture();
  auto begin = std::chrono::system_clock::now();
  auto t = waitWithSemaphore(std::move(f),
    std::chrono::milliseconds(1));
  auto end = std::chrono::system_clock::now();
  EXPECT_TRUE( end - begin < std::chrono::milliseconds(2));
  EXPECT_FALSE(t.isReady());
 }
 {
  auto t = waitWithSemaphore(makeFuture(),
    std::chrono::milliseconds(1));
  EXPECT_TRUE(t.isReady());
 }
}
