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

#include <algorithm>
#include <atomic>
#include <folly/small_vector.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <unistd.h>
#include <folly/Memory.h>
#include <folly/Executor.h>
#include <folly/futures/Future.h>
#include <folly/futures/ManualExecutor.h>
#include <folly/futures/DrivableExecutor.h>
#include <folly/dynamic.h>
#include <folly/Baton.h>
#include <folly/MPMCQueue.h>

#include <folly/io/async/EventBase.h>
#include <folly/io/async/Request.h>

using namespace folly;
using std::pair;
using std::string;
using std::unique_ptr;
using std::vector;
using std::chrono::milliseconds;

#define EXPECT_TYPE(x, T) \
  EXPECT_TRUE((std::is_same<decltype(x), T>::value))

/// Simple executor that does work in another thread
class ThreadExecutor : public Executor {
  folly::MPMCQueue<Func> funcs;
  std::atomic<bool> done {false};
  std::thread worker;
  folly::Baton<> baton;

  void work() {
    baton.post();
    Func fn;
    while (!done) {
      while (!funcs.isEmpty()) {
        funcs.blockingRead(fn);
        fn();
      }
    }
  }

 public:
  explicit ThreadExecutor(size_t n = 1024)
    : funcs(n) {
    worker = std::thread(std::bind(&ThreadExecutor::work, this));
  }

  ~ThreadExecutor() {
    done = true;
    funcs.write([]{});
    worker.join();
  }

  void add(Func fn) override {
    funcs.blockingWrite(std::move(fn));
  }

  void waitForStartup() {
    baton.wait();
  }
};

typedef FutureException eggs_t;
static eggs_t eggs("eggs");

// Core

TEST(Future, coreSize) {
  // If this number goes down, it's fine!
  // If it goes up, please seek professional advice ;-)
  EXPECT_EQ(192, sizeof(detail::Core<void>));
}

// Future

TEST(Future, onError) {
  bool theFlag = false;
  auto flag = [&]{ theFlag = true; };
#define EXPECT_FLAG() \
  do { \
    EXPECT_TRUE(theFlag); \
    theFlag = false; \
  } while(0);

#define EXPECT_NO_FLAG() \
  do { \
    EXPECT_FALSE(theFlag); \
    theFlag = false; \
  } while(0);

  // By reference
  {
    auto f = makeFuture()
      .then([] { throw eggs; })
      .onError([&] (eggs_t& e) { flag(); });
    EXPECT_FLAG();
    EXPECT_NO_THROW(f.value());
  }

  {
    auto f = makeFuture()
      .then([] { throw eggs; })
      .onError([&] (eggs_t& e) { flag(); return makeFuture(); });
    EXPECT_FLAG();
    EXPECT_NO_THROW(f.value());
  }

  // By value
  {
    auto f = makeFuture()
      .then([] { throw eggs; })
      .onError([&] (eggs_t e) { flag(); });
    EXPECT_FLAG();
    EXPECT_NO_THROW(f.value());
  }

  {
    auto f = makeFuture()
      .then([] { throw eggs; })
      .onError([&] (eggs_t e) { flag(); return makeFuture(); });
    EXPECT_FLAG();
    EXPECT_NO_THROW(f.value());
  }

  // Polymorphic
  {
    auto f = makeFuture()
      .then([] { throw eggs; })
      .onError([&] (std::exception& e) { flag(); });
    EXPECT_FLAG();
    EXPECT_NO_THROW(f.value());
  }

  {
    auto f = makeFuture()
      .then([] { throw eggs; })
      .onError([&] (std::exception& e) { flag(); return makeFuture(); });
    EXPECT_FLAG();
    EXPECT_NO_THROW(f.value());
  }

  // Non-exceptions
  {
    auto f = makeFuture()
      .then([] { throw -1; })
      .onError([&] (int e) { flag(); });
    EXPECT_FLAG();
    EXPECT_NO_THROW(f.value());
  }

  {
    auto f = makeFuture()
      .then([] { throw -1; })
      .onError([&] (int e) { flag(); return makeFuture(); });
    EXPECT_FLAG();
    EXPECT_NO_THROW(f.value());
  }

  // Mutable lambda
  {
    auto f = makeFuture()
      .then([] { throw eggs; })
      .onError([&] (eggs_t& e) mutable { flag(); });
    EXPECT_FLAG();
    EXPECT_NO_THROW(f.value());
  }

  {
    auto f = makeFuture()
      .then([] { throw eggs; })
      .onError([&] (eggs_t& e) mutable { flag(); return makeFuture(); });
    EXPECT_FLAG();
    EXPECT_NO_THROW(f.value());
  }

  // No throw
  {
    auto f = makeFuture()
      .then([] { return 42; })
      .onError([&] (eggs_t& e) { flag(); return -1; });
    EXPECT_NO_FLAG();
    EXPECT_EQ(42, f.value());
  }

  {
    auto f = makeFuture()
      .then([] { return 42; })
      .onError([&] (eggs_t& e) { flag(); return makeFuture<int>(-1); });
    EXPECT_NO_FLAG();
    EXPECT_EQ(42, f.value());
  }

  // Catch different exception
  {
    auto f = makeFuture()
      .then([] { throw eggs; })
      .onError([&] (std::runtime_error& e) { flag(); });
    EXPECT_NO_FLAG();
    EXPECT_THROW(f.value(), eggs_t);
  }

  {
    auto f = makeFuture()
      .then([] { throw eggs; })
      .onError([&] (std::runtime_error& e) { flag(); return makeFuture(); });
    EXPECT_NO_FLAG();
    EXPECT_THROW(f.value(), eggs_t);
  }

  // Returned value propagates
  {
    auto f = makeFuture()
      .then([] { throw eggs; return 0; })
      .onError([&] (eggs_t& e) { return 42; });
    EXPECT_EQ(42, f.value());
  }

  // Returned future propagates
  {
    auto f = makeFuture()
      .then([] { throw eggs; return 0; })
      .onError([&] (eggs_t& e) { return makeFuture<int>(42); });
    EXPECT_EQ(42, f.value());
  }

  // Throw in callback
  {
    auto f = makeFuture()
      .then([] { throw eggs; return 0; })
      .onError([&] (eggs_t& e) { throw e; return -1; });
    EXPECT_THROW(f.value(), eggs_t);
  }

  {
    auto f = makeFuture()
      .then([] { throw eggs; return 0; })
      .onError([&] (eggs_t& e) { throw e; return makeFuture<int>(-1); });
    EXPECT_THROW(f.value(), eggs_t);
  }

  // exception_wrapper, return Future<T>
  {
    auto f = makeFuture()
      .then([] { throw eggs; })
      .onError([&] (exception_wrapper e) { flag(); return makeFuture(); });
    EXPECT_FLAG();
    EXPECT_NO_THROW(f.value());
  }

  // exception_wrapper, return Future<T> but throw
  {
    auto f = makeFuture()
      .then([]{ throw eggs; return 0; })
      .onError([&] (exception_wrapper e) {
        flag();
        throw eggs;
        return makeFuture<int>(-1);
      });
    EXPECT_FLAG();
    EXPECT_THROW(f.value(), eggs_t);
  }

  // exception_wrapper, return T
  {
    auto f = makeFuture()
      .then([]{ throw eggs; return 0; })
      .onError([&] (exception_wrapper e) {
        flag();
        return -1;
      });
    EXPECT_FLAG();
    EXPECT_EQ(-1, f.value());
  }

  // exception_wrapper, return T but throw
  {
    auto f = makeFuture()
      .then([]{ throw eggs; return 0; })
      .onError([&] (exception_wrapper e) {
        flag();
        throw eggs;
        return -1;
      });
    EXPECT_FLAG();
    EXPECT_THROW(f.value(), eggs_t);
  }

  // const exception_wrapper&
  {
    auto f = makeFuture()
      .then([] { throw eggs; })
      .onError([&] (const exception_wrapper& e) {
        flag();
        return makeFuture();
      });
    EXPECT_FLAG();
    EXPECT_NO_THROW(f.value());
  }

}

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
  auto f = makeFuture<string>("0")
    .then([](){ return makeFuture<string>("1"); })
    .then([](Try<string>&& t) { return makeFuture(t.value() + ";2"); })
    .then([](const Try<string>&& t) { return makeFuture(t.value() + ";3"); })
    .then([](Try<string>& t) { return makeFuture(t.value() + ";4"); })
    .then([](const Try<string>& t) { return makeFuture(t.value() + ";5"); })
    .then([](Try<string> t) { return makeFuture(t.value() + ";6"); })
    .then([](const Try<string> t) { return makeFuture(t.value() + ";7"); })
    .then([](string&& s) { return makeFuture(s + ";8"); })
    .then([](const string&& s) { return makeFuture(s + ";9"); })
    .then([](string& s) { return makeFuture(s + ";10"); })
    .then([](const string& s) { return makeFuture(s + ";11"); })
    .then([](string s) { return makeFuture(s + ";12"); })
    .then([](const string s) { return makeFuture(s + ";13"); })
  ;
  EXPECT_EQ(f.value(), "1;2;3;4;5;6;7;8;9;10;11;12;13");
}

TEST(Future, thenTry) {
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

TEST(Future, thenValue) {
  bool flag = false;
  makeFuture<int>(42).then([&](int i){
    EXPECT_EQ(42, i);
    flag = true;
  });
  EXPECT_TRUE(flag); flag = false;

  makeFuture<int>(42)
    .then([](int i){ return i; })
    .then([&](int i) { flag = true; EXPECT_EQ(42, i); });
  EXPECT_TRUE(flag); flag = false;

  makeFuture().then([&]{
    flag = true;
  });
  EXPECT_TRUE(flag); flag = false;

  auto f = makeFuture<int>(eggs).then([&](int i){});
  EXPECT_THROW(f.value(), eggs_t);

  f = makeFuture<void>(eggs).then([&]{});
  EXPECT_THROW(f.value(), eggs_t);
}

TEST(Future, thenValueFuture) {
  bool flag = false;
  makeFuture<int>(42)
    .then([](int i){ return makeFuture<int>(std::move(i)); })
    .then([&](Try<int>&& t) { flag = true; EXPECT_EQ(42, t.value()); });
  EXPECT_TRUE(flag); flag = false;

  makeFuture()
    .then([]{ return makeFuture(); })
    .then([&](Try<void>&& t) { flag = true; });
  EXPECT_TRUE(flag); flag = false;
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
    .then(&Worker::doWork, &w);

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
    .then(&Worker::doWorkFuture, &w);

  EXPECT_EQ(f.value(), "start;static;class-static;class");
}

TEST(Future, thenBind) {
  auto l = []() {
    return makeFuture("bind");
  };
  auto b = std::bind(l);
  auto f = makeFuture().then(std::move(b));
  EXPECT_EQ(f.value(), "bind");
}

TEST(Future, thenBindTry) {
  auto l = [](Try<string>&& t) {
    return makeFuture(t.value() + ";bind");
  };
  auto b = std::bind(l, std::placeholders::_1);
  auto f = makeFuture<string>("start").then(std::move(b));

  EXPECT_EQ(f.value(), "start;bind");
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
  EXPECT_TYPE(makeFutureWith(fun), Future<int>);
  EXPECT_EQ(42, makeFutureWith(fun).value());

  auto failfun = []() -> int { throw eggs; };
  EXPECT_TYPE(makeFutureWith(failfun), Future<int>);
  EXPECT_THROW(makeFutureWith(failfun).value(), eggs_t);

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
      p.setException(exception_wrapper(std::current_exception()));
    }
    EXPECT_THROW(f.value(), eggs_t);
  }
}

TEST(Promise, setWith) {
  {
    Promise<int> p;
    auto f = p.getFuture();
    p.setWith([] { return 42; });
    EXPECT_EQ(42, f.value());
  }
  {
    Promise<int> p;
    auto f = p.getFuture();
    p.setWith([]() -> int { throw eggs; });
    EXPECT_THROW(f.value(), eggs_t);
  }
}

TEST(Future, finish) {
  auto x = std::make_shared<int>(0);
  {
    Promise<int> p;
    auto f = p.getFuture().then([x](Try<int>&& t) { *x = t.value(); });

    // The callback hasn't executed
    EXPECT_EQ(0, *x);

    // The callback has a reference to x
    EXPECT_EQ(2, x.use_count());

    p.setValue(42);

    // the callback has executed
    EXPECT_EQ(42, *x);
  }
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

TEST(Future, collectAll) {
  // returns a vector variant
  {
    vector<Promise<int>> promises(10);
    vector<Future<int>> futures;

    for (auto& p : promises)
      futures.push_back(p.getFuture());

    auto allf = collectAll(futures);

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

    auto allf = collectAll(futures);


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

    auto allf = collectAll(futures)
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

TEST(Future, collect) {
  // success case
  {
    vector<Promise<int>> promises(10);
    vector<Future<int>> futures;

    for (auto& p : promises)
      futures.push_back(p.getFuture());

    auto allf = collect(futures);

    random_shuffle(promises.begin(), promises.end());
    for (auto& p : promises) {
      EXPECT_FALSE(allf.isReady());
      p.setValue(42);
    }

    EXPECT_TRUE(allf.isReady());
    for (auto i : allf.value()) {
      EXPECT_EQ(42, i);
    }
  }

  // failure case
  {
    vector<Promise<int>> promises(10);
    vector<Future<int>> futures;

    for (auto& p : promises)
      futures.push_back(p.getFuture());

    auto allf = collect(futures);

    random_shuffle(promises.begin(), promises.end());
    for (int i = 0; i < 10; i++) {
      if (i < 5) {
        // everthing goes well so far...
        EXPECT_FALSE(allf.isReady());
        promises[i].setValue(42);
      } else if (i == 5) {
        // short circuit with an exception
        EXPECT_FALSE(allf.isReady());
        promises[i].setException(eggs);
        EXPECT_TRUE(allf.isReady());
      } else if (i < 8) {
        // don't blow up on further values
        EXPECT_TRUE(allf.isReady());
        promises[i].setValue(42);
      } else {
        // don't blow up on further exceptions
        EXPECT_TRUE(allf.isReady());
        promises[i].setException(eggs);
      }
    }

    EXPECT_THROW(allf.value(), eggs_t);
  }

  // void futures success case
  {
    vector<Promise<void>> promises(10);
    vector<Future<void>> futures;

    for (auto& p : promises)
      futures.push_back(p.getFuture());

    auto allf = collect(futures);

    random_shuffle(promises.begin(), promises.end());
    for (auto& p : promises) {
      EXPECT_FALSE(allf.isReady());
      p.setValue();
    }

    EXPECT_TRUE(allf.isReady());
  }

  // void futures failure case
  {
    vector<Promise<void>> promises(10);
    vector<Future<void>> futures;

    for (auto& p : promises)
      futures.push_back(p.getFuture());

    auto allf = collect(futures);

    random_shuffle(promises.begin(), promises.end());
    for (int i = 0; i < 10; i++) {
      if (i < 5) {
        // everthing goes well so far...
        EXPECT_FALSE(allf.isReady());
        promises[i].setValue();
      } else if (i == 5) {
        // short circuit with an exception
        EXPECT_FALSE(allf.isReady());
        promises[i].setException(eggs);
        EXPECT_TRUE(allf.isReady());
      } else if (i < 8) {
        // don't blow up on further values
        EXPECT_TRUE(allf.isReady());
        promises[i].setValue();
      } else {
        // don't blow up on further exceptions
        EXPECT_TRUE(allf.isReady());
        promises[i].setException(eggs);
      }
    }

    EXPECT_THROW(allf.value(), eggs_t);
  }

  // move only compiles
  {
    vector<Promise<unique_ptr<int>>> promises(10);
    vector<Future<unique_ptr<int>>> futures;

    for (auto& p : promises)
      futures.push_back(p.getFuture());

    collect(futures);
  }

}

struct NotDefaultConstructible {
  NotDefaultConstructible() = delete;
  NotDefaultConstructible(int arg) : i(arg) {}
  int i;
};

// We have a specialized implementation for non-default-constructible objects
// Ensure that it works and preserves order
TEST(Future, collectNotDefaultConstructible) {
  vector<Promise<NotDefaultConstructible>> promises(10);
  vector<Future<NotDefaultConstructible>> futures;
  vector<int> indices(10);
  std::iota(indices.begin(), indices.end(), 0);
  random_shuffle(indices.begin(), indices.end());

  for (auto& p : promises)
    futures.push_back(p.getFuture());

  auto allf = collect(futures);

  for (auto i : indices) {
    EXPECT_FALSE(allf.isReady());
    promises[i].setValue(NotDefaultConstructible(i));
  }

  EXPECT_TRUE(allf.isReady());
  int i = 0;
  for (auto val : allf.value()) {
    EXPECT_EQ(i, val.i);
    i++;
  }
}

TEST(Future, collectAny) {
  {
    vector<Promise<int>> promises(10);
    vector<Future<int>> futures;

    for (auto& p : promises)
      futures.push_back(p.getFuture());

    for (auto& f : futures) {
      EXPECT_FALSE(f.isReady());
    }

    auto anyf = collectAny(futures);

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

    auto anyf = collectAny(futures);

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

    auto anyf = collectAny(futures)
      .then([](pair<size_t, Try<int>> p) {
        EXPECT_EQ(42, p.second.value());
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

    collectAll(fs)
      .then([&](vector<Try<void>> ts) {
        EXPECT_EQ(fs.size(), ts.size());
      });
  }
  {
    vector<Future<int>> fs;
    for (int i = 0; i < 10; i++)
      fs.push_back(makeFuture(i));

    collectAny(fs)
      .then([&](pair<size_t, Try<int>> p) {
        EXPECT_EQ(p.first, p.second.value());
      });
  }
}

TEST(when, collectN) {
  vector<Promise<void>> promises(10);
  vector<Future<void>> futures;

  for (auto& p : promises)
    futures.push_back(p.getFuture());

  bool flag = false;
  size_t n = 3;
  collectN(futures, n)
    .then([&](vector<pair<size_t, Try<void>>> v) {
      flag = true;
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

    auto anyf = collectAny(futures);
  }

  {
    small_vector<Future<void>> futures;

    for (int i = 0; i < 10; i++)
      futures.push_back(makeFuture());

    auto allf = collectAll(futures);
  }
}

TEST(Future, collectAllVariadic) {
  Promise<bool> pb;
  Promise<int> pi;
  Future<bool> fb = pb.getFuture();
  Future<int> fi = pi.getFuture();
  bool flag = false;
  collectAll(std::move(fb), std::move(fi))
    .then([&](std::tuple<Try<bool>, Try<int>> tup) {
      flag = true;
      EXPECT_TRUE(std::get<0>(tup).hasValue());
      EXPECT_EQ(std::get<0>(tup).value(), true);
      EXPECT_TRUE(std::get<1>(tup).hasValue());
      EXPECT_EQ(std::get<1>(tup).value(), 42);
    });
  pb.setValue(true);
  EXPECT_FALSE(flag);
  pi.setValue(42);
  EXPECT_TRUE(flag);
}

TEST(Future, collectAllVariadicReferences) {
  Promise<bool> pb;
  Promise<int> pi;
  Future<bool> fb = pb.getFuture();
  Future<int> fi = pi.getFuture();
  bool flag = false;
  collectAll(fb, fi)
    .then([&](std::tuple<Try<bool>, Try<int>> tup) {
      flag = true;
      EXPECT_TRUE(std::get<0>(tup).hasValue());
      EXPECT_EQ(std::get<0>(tup).value(), true);
      EXPECT_TRUE(std::get<1>(tup).hasValue());
      EXPECT_EQ(std::get<1>(tup).value(), 42);
    });
  pb.setValue(true);
  EXPECT_FALSE(flag);
  pi.setValue(42);
  EXPECT_TRUE(flag);
}

TEST(Future, collectAll_none) {
  vector<Future<int>> fs;
  auto f = collectAll(fs);
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

TEST(Future, waitImmediate) {
  makeFuture().wait();
  auto done = makeFuture(42).wait().value();
  EXPECT_EQ(42, done);

  vector<int> v{1,2,3};
  auto done_v = makeFuture(v).wait().value();
  EXPECT_EQ(v.size(), done_v.size());
  EXPECT_EQ(v, done_v);

  vector<Future<void>> v_f;
  v_f.push_back(makeFuture());
  v_f.push_back(makeFuture());
  auto done_v_f = collectAll(v_f).wait().value();
  EXPECT_EQ(2, done_v_f.size());

  vector<Future<bool>> v_fb;
  v_fb.push_back(makeFuture(true));
  v_fb.push_back(makeFuture(false));
  auto fut = collectAll(v_fb);
  auto done_v_fb = std::move(fut.wait().value());
  EXPECT_EQ(2, done_v_fb.size());
}

TEST(Future, wait) {
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
      result.store(n.wait().value());
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

struct MoveFlag {
  MoveFlag() = default;
  MoveFlag(const MoveFlag&) = delete;
  MoveFlag(MoveFlag&& other) noexcept {
    other.moved = true;
  }
  bool moved{false};
};

TEST(Future, waitReplacesSelf) {
  // wait
  {
    // lvalue
    auto f1 = makeFuture(MoveFlag());
    f1.wait();
    EXPECT_FALSE(f1.value().moved);

    // rvalue
    auto f2 = makeFuture(MoveFlag()).wait();
    EXPECT_FALSE(f2.value().moved);
  }

  // wait(Duration)
  {
    // lvalue
    auto f1 = makeFuture(MoveFlag());
    f1.wait(milliseconds(1));
    EXPECT_FALSE(f1.value().moved);

    // rvalue
    auto f2 = makeFuture(MoveFlag()).wait(milliseconds(1));
    EXPECT_FALSE(f2.value().moved);
  }

  // waitVia
  {
    folly::EventBase eb;
    // lvalue
    auto f1 = makeFuture(MoveFlag());
    f1.waitVia(&eb);
    EXPECT_FALSE(f1.value().moved);

    // rvalue
    auto f2 = makeFuture(MoveFlag()).waitVia(&eb);
    EXPECT_FALSE(f2.value().moved);
  }
}

TEST(Future, waitWithDuration) {
 {
  Promise<int> p;
  Future<int> f = p.getFuture();
  f.wait(milliseconds(1));
  EXPECT_FALSE(f.isReady());
  p.setValue(1);
  EXPECT_TRUE(f.isReady());
 }
 {
  Promise<int> p;
  Future<int> f = p.getFuture();
  p.setValue(1);
  f.wait(milliseconds(1));
  EXPECT_TRUE(f.isReady());
 }
 {
  vector<Future<bool>> v_fb;
  v_fb.push_back(makeFuture(true));
  v_fb.push_back(makeFuture(false));
  auto f = collectAll(v_fb);
  f.wait(milliseconds(1));
  EXPECT_TRUE(f.isReady());
  EXPECT_EQ(2, f.value().size());
 }
 {
  vector<Future<bool>> v_fb;
  Promise<bool> p1;
  Promise<bool> p2;
  v_fb.push_back(p1.getFuture());
  v_fb.push_back(p2.getFuture());
  auto f = collectAll(v_fb);
  f.wait(milliseconds(1));
  EXPECT_FALSE(f.isReady());
  p1.setValue(true);
  EXPECT_FALSE(f.isReady());
  p2.setValue(true);
  EXPECT_TRUE(f.isReady());
 }
 {
  auto f = makeFuture().wait(milliseconds(1));
  EXPECT_TRUE(f.isReady());
 }

 {
   Promise<void> p;
   auto start = std::chrono::steady_clock::now();
   auto f = p.getFuture().wait(milliseconds(100));
   auto elapsed = std::chrono::steady_clock::now() - start;
   EXPECT_GE(elapsed, milliseconds(100));
   EXPECT_FALSE(f.isReady());
   p.setValue();
   EXPECT_TRUE(f.isReady());
 }

 {
   // Try to trigger the race where the resultant Future is not yet complete
   // even if we didn't hit the timeout, and make sure we deal with it properly
   Promise<void> p;
   folly::Baton<> b;
   auto t = std::thread([&]{
     b.post();
     /* sleep override */ std::this_thread::sleep_for(milliseconds(100));
     p.setValue();
   });
   b.wait();
   auto f = p.getFuture().wait(std::chrono::seconds(3600));
   EXPECT_TRUE(f.isReady());
   t.join();
 }
}

class DummyDrivableExecutor : public DrivableExecutor {
 public:
  void add(Func f) override {}
  void drive() override { ran = true; }
  bool ran{false};
};

TEST(Future, getVia) {
  {
    // non-void
    ManualExecutor x;
    auto f = via(&x).then([]{ return true; });
    EXPECT_TRUE(f.getVia(&x));
  }

  {
    // void
    ManualExecutor x;
    auto f = via(&x).then();
    f.getVia(&x);
  }

  {
    DummyDrivableExecutor x;
    auto f = makeFuture(true);
    EXPECT_TRUE(f.getVia(&x));
    EXPECT_FALSE(x.ran);
  }
}

TEST(Future, waitVia) {
  {
    ManualExecutor x;
    auto f = via(&x).then();
    EXPECT_FALSE(f.isReady());
    f.waitVia(&x);
    EXPECT_TRUE(f.isReady());
  }

  {
    // try rvalue as well
    ManualExecutor x;
    auto f = via(&x).then().waitVia(&x);
    EXPECT_TRUE(f.isReady());
  }

  {
    DummyDrivableExecutor x;
    makeFuture(true).waitVia(&x);
    EXPECT_FALSE(x.ran);
  }
}

TEST(Future, viaRaces) {
  ManualExecutor x;
  Promise<void> p;
  auto tid = std::this_thread::get_id();
  bool done = false;

  std::thread t1([&] {
    p.getFuture()
      .via(&x)
      .then([&](Try<void>&&) { EXPECT_EQ(tid, std::this_thread::get_id()); })
      .then([&](Try<void>&&) { EXPECT_EQ(tid, std::this_thread::get_id()); })
      .then([&](Try<void>&&) { done = true; });
  });

  std::thread t2([&] {
    p.setValue();
  });

  while (!done) x.run();
  t1.join();
  t2.join();
}

TEST(Future, getFuture_after_setValue) {
  Promise<int> p;
  p.setValue(42);
  EXPECT_EQ(42, p.getFuture().value());
}

TEST(Future, getFuture_after_setException) {
  Promise<void> p;
  p.setWith([]() -> void { throw std::logic_error("foo"); });
  EXPECT_THROW(p.getFuture().value(), std::logic_error);
}

TEST(Future, detachRace) {
  // Task #5438209
  // This test is designed to detect a race that was in Core::detachOne()
  // where detached_ was incremented and then tested, and that
  // allowed a race where both Promise and Future would think they were the
  // second and both try to delete. This showed up at scale but was very
  // difficult to reliably repro in a test. As it is, this only fails about
  // once in every 1,000 executions. Doing this 1,000 times is going to make a
  // slow test so I won't do that but if it ever fails, take it seriously, and
  // run the test binary with "--gtest_repeat=10000 --gtest_filter=*detachRace"
  // (Don't forget to enable ASAN)
  auto p = folly::make_unique<Promise<bool>>();
  auto f = folly::make_unique<Future<bool>>(p->getFuture());
  folly::Baton<> baton;
  std::thread t1([&]{
    baton.post();
    p.reset();
  });
  baton.wait();
  f.reset();
  t1.join();
}

class TestData : public RequestData {
 public:
  explicit TestData(int data) : data_(data) {}
  virtual ~TestData() {}
  int data_;
};

TEST(Future, context) {

  // Start a new context
  RequestContext::create();

  EXPECT_EQ(nullptr, RequestContext::get()->getContextData("test"));

  // Set some test data
  RequestContext::get()->setContextData(
    "test",
    std::unique_ptr<TestData>(new TestData(10)));

  // Start a future
  Promise<void> p;
  auto future = p.getFuture().then([&]{
    // Check that the context followed the future
    EXPECT_TRUE(RequestContext::get() != nullptr);
    auto a = dynamic_cast<TestData*>(
      RequestContext::get()->getContextData("test"));
    auto data = a->data_;
    EXPECT_EQ(10, data);
  });

  // Clear the context
  RequestContext::setContext(nullptr);

  EXPECT_EQ(nullptr, RequestContext::get()->getContextData("test"));

  // Fulfill the promise
  p.setValue();
}


// This only fails about 1 in 1k times when the bug is present :(
TEST(Future, t5506504) {
  ThreadExecutor x;

  auto fn = [&x]{
    auto promises = std::make_shared<vector<Promise<void>>>(4);
    vector<Future<void>> futures;

    for (auto& p : *promises) {
      futures.emplace_back(
        p.getFuture()
        .via(&x)
        .then([](Try<void>&&){}));
    }

    x.waitForStartup();
    x.add([promises]{
      for (auto& p : *promises) p.setValue();
    });

    return collectAll(futures);
  };

  fn().wait();
}

// Test of handling of a circular dependency. It's never recommended
// to have one because of possible memory leaks. Here we test that
// we can handle freeing of the Future while it is running.
TEST(Future, CircularDependencySharedPtrSelfReset) {
  Promise<int64_t> promise;
  auto ptr = std::make_shared<Future<int64_t>>(promise.getFuture());

  ptr->then(
    [ptr] (folly::Try<int64_t>&& uid) mutable {
      EXPECT_EQ(1, ptr.use_count());

      // Leaving no references to ourselves.
      ptr.reset();
      EXPECT_EQ(0, ptr.use_count());
    }
  );

  EXPECT_EQ(2, ptr.use_count());

  ptr.reset();

  promise.setWith([]{return 1l;});
}

TEST(Future, Constructor) {
  auto f1 = []() -> Future<int> { return Future<int>(3); }();
  EXPECT_EQ(f1.value(), 3);
  auto f2 = []() -> Future<void> { return Future<void>(); }();
  EXPECT_NO_THROW(f2.value());
}

TEST(Future, ImplicitConstructor) {
  auto f1 = []() -> Future<int> { return 3; }();
  EXPECT_EQ(f1.value(), 3);
  // Unfortunately, the C++ standard does not allow the
  // following implicit conversion to work:
  //auto f2 = []() -> Future<void> { }();
}

TEST(Future, thenDynamic) {
  // folly::dynamic has a constructor that takes any T, this test makes
  // sure that we call the then lambda with folly::dynamic and not
  // Try<folly::dynamic> because that then fails to compile
  Promise<folly::dynamic> p;
  Future<folly::dynamic> f = p.getFuture().then(
      [](const folly::dynamic& d) {
        return folly::dynamic(d.asInt() + 3);
      }
  );
  p.setValue(2);
  EXPECT_EQ(f.get(), 5);
}

TEST(Future, via_then_get_was_racy) {
  ThreadExecutor x;
  std::unique_ptr<int> val = folly::via(&x)
    .then([] { return folly::make_unique<int>(42); })
    .get();
  ASSERT_TRUE(!!val);
  EXPECT_EQ(42, *val);
}

TEST(Future, ensure) {
  size_t count = 0;
  auto cob = [&]{ count++; };
  auto f = makeFuture(42)
    .ensure(cob)
    .then([](int) { throw std::runtime_error("ensure"); })
    .ensure(cob);

  EXPECT_THROW(f.get(), std::runtime_error);
  EXPECT_EQ(2, count);
}

TEST(Future, willEqual) {
    //both p1 and p2 already fulfilled
    {
    Promise<int> p1;
    Promise<int> p2;
    p1.setValue(27);
    p2.setValue(27);
    auto f1 = p1.getFuture();
    auto f2 = p2.getFuture();
    EXPECT_TRUE(f1.willEqual(f2).get());
    }{
    Promise<int> p1;
    Promise<int> p2;
    p1.setValue(27);
    p2.setValue(36);
    auto f1 = p1.getFuture();
    auto f2 = p2.getFuture();
    EXPECT_FALSE(f1.willEqual(f2).get());
    }
    //both p1 and p2 not yet fulfilled
    {
    Promise<int> p1;
    Promise<int> p2;
    auto f1 = p1.getFuture();
    auto f2 = p2.getFuture();
    auto f3 = f1.willEqual(f2);
    p1.setValue(27);
    p2.setValue(27);
    EXPECT_TRUE(f3.get());
    }{
    Promise<int> p1;
    Promise<int> p2;
    auto f1 = p1.getFuture();
    auto f2 = p2.getFuture();
    auto f3 = f1.willEqual(f2);
    p1.setValue(27);
    p2.setValue(36);
    EXPECT_FALSE(f3.get());
    }
    //p1 already fulfilled, p2 not yet fulfilled
    {
    Promise<int> p1;
    Promise<int> p2;
    p1.setValue(27);
    auto f1 = p1.getFuture();
    auto f2 = p2.getFuture();
    auto f3 = f1.willEqual(f2);
    p2.setValue(27);
    EXPECT_TRUE(f3.get());
    }{
    Promise<int> p1;
    Promise<int> p2;
    p1.setValue(27);
    auto f1 = p1.getFuture();
    auto f2 = p2.getFuture();
    auto f3 = f1.willEqual(f2);
    p2.setValue(36);
    EXPECT_FALSE(f3.get());
    }
    //p2 already fulfilled, p1 not yet fulfilled
    {
    Promise<int> p1;
    Promise<int> p2;
    p2.setValue(27);
    auto f1 = p1.getFuture();
    auto f2 = p2.getFuture();
    auto f3 = f1.willEqual(f2);
    p1.setValue(27);
    EXPECT_TRUE(f3.get());
    }{
    Promise<int> p1;
    Promise<int> p2;
    p2.setValue(36);
    auto f1 = p1.getFuture();
    auto f2 = p2.getFuture();
    auto f3 = f1.willEqual(f2);
    p1.setValue(27);
    EXPECT_FALSE(f3.get());
    }
}

// Unwrap tests.

// A simple scenario for the unwrap call, when the promise was fulfilled
// before calling to unwrap.
TEST(Future, Unwrap_SimpleScenario) {
  Future<int> encapsulated_future = makeFuture(5484);
  Future<Future<int>> future = makeFuture(std::move(encapsulated_future));
  EXPECT_EQ(5484, future.unwrap().value());
}

// Makes sure that unwrap() works when chaning Future's commands.
TEST(Future, Unwrap_ChainCommands) {
  Future<Future<int>> future = makeFuture(makeFuture(5484));
  auto unwrapped = future.unwrap().then([](int i){ return i; });
  EXPECT_EQ(5484, unwrapped.value());
}

// Makes sure that the unwrap call also works when the promise was not yet
// fulfilled, and that the returned Future<T> becomes ready once the promise
// is fulfilled.
TEST(Future, Unwrap_FutureNotReady) {
  Promise<Future<int>> p;
  Future<Future<int>> future = p.getFuture();
  Future<int> unwrapped = future.unwrap();
  // Sanity - should not be ready before the promise is fulfilled.
  ASSERT_FALSE(unwrapped.isReady());
  // Fulfill the promise and make sure the unwrapped future is now ready.
  p.setValue(makeFuture(5484));
  ASSERT_TRUE(unwrapped.isReady());
  EXPECT_EQ(5484, unwrapped.value());
}

TEST(Reduce, Basic) {
  auto makeFutures = [](int count) {
    std::vector<Future<int>> fs;
    for (int i = 1; i <= count; ++i) {
      fs.emplace_back(makeFuture(i));
    }
    return fs;
  };

  // Empty (Try)
  {
    auto fs = makeFutures(0);

    Future<double> f1 = reduce(fs, 1.2,
      [](double a, Try<int>&& b){
        return a + *b + 0.1;
      });
    EXPECT_EQ(1.2, f1.get());
  }

  // One (Try)
  {
    auto fs = makeFutures(1);

    Future<double> f1 = reduce(fs, 0.0,
      [](double a, Try<int>&& b){
        return a + *b + 0.1;
      });
    EXPECT_EQ(1.1, f1.get());
  }

  // Returning values (Try)
  {
    auto fs = makeFutures(3);

    Future<double> f1 = reduce(fs, 0.0,
      [](double a, Try<int>&& b){
        return a + *b + 0.1;
      });
    EXPECT_EQ(6.3, f1.get());
  }

  // Returning values
  {
    auto fs = makeFutures(3);

    Future<double> f1 = reduce(fs, 0.0,
      [](double a, int&& b){
        return a + b + 0.1;
      });
    EXPECT_EQ(6.3, f1.get());
  }

  // Returning futures (Try)
  {
    auto fs = makeFutures(3);

    Future<double> f2 = reduce(fs, 0.0,
      [](double a, Try<int>&& b){
        return makeFuture<double>(a + *b + 0.1);
      });
    EXPECT_EQ(6.3, f2.get());
  }

  // Returning futures
  {
    auto fs = makeFutures(3);

    Future<double> f2 = reduce(fs, 0.0,
      [](double a, int&& b){
        return makeFuture<double>(a + b + 0.1);
      });
    EXPECT_EQ(6.3, f2.get());
  }
}

TEST(Map, Basic) {
  Promise<int> p1;
  Promise<int> p2;
  Promise<int> p3;

  std::vector<Future<int>> fs;
  fs.push_back(p1.getFuture());
  fs.push_back(p2.getFuture());
  fs.push_back(p3.getFuture());

  int c = 0;
  auto fs2 = futures::map(fs, [&](int i){
    c += i;
  });

  // Ensure we call the callbacks as the futures complete regardless of order
  p2.setValue(1);
  EXPECT_EQ(1, c);
  p3.setValue(1);
  EXPECT_EQ(2, c);
  p1.setValue(1);
  EXPECT_EQ(3, c);

  EXPECT_TRUE(collect(fs2).isReady());
}

TEST(Promise, defaultConstructedUnit) {
  Promise<Unit> p;
  p.setValue();
}
