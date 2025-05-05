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

#include <chrono>
#include <stdexcept>
#include <thread>

#include <utility>
#include <folly/Singleton.h>
#include <folly/experimental/observer/Observer.h>
#include <folly/experimental/observer/SimpleObservable.h>
#include <folly/experimental/observer/WithJitter.h>
#include <folly/fibers/FiberManager.h>
#include <folly/fibers/FiberManagerMap.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/Baton.h>

using namespace folly::observer;

TEST(Observer, Observable) {
  SimpleObservable<int> observable(42);
  auto observer = observable.getObserver();

  EXPECT_EQ(42, **observer);

  folly::Baton<> baton;
  auto waitingObserver = makeObserver([observer, &baton]() {
    *observer;
    baton.post();
    return folly::Unit();
  });
  baton.reset();

  observable.setValue(24);

  EXPECT_TRUE(baton.try_wait_for(std::chrono::seconds{1}));

  EXPECT_EQ(24, **observer);
}

TEST(Observer, MakeObserver) {
  SimpleObservable<int> observable(42);

  auto observer = makeObserver(
      [child = observable.getObserver()]() { return **child + 1; });

  EXPECT_EQ(43, **observer);

  folly::Baton<> baton;
  auto waitingObserver = makeObserver([observer, &baton]() {
    *observer;
    baton.post();
    return folly::Unit();
  });
  baton.reset();

  observable.setValue(24);

  EXPECT_TRUE(baton.try_wait_for(std::chrono::seconds{1}));

  EXPECT_EQ(25, **observer);
}

TEST(Observer, MakeObserverDiamond) {
  SimpleObservable<int> observable(42);

  auto observer1 = makeObserver(
      [child = observable.getObserver()]() { return **child + 1; });

  auto observer2 = makeObserver([child = observable.getObserver()]() {
    return std::make_shared<int>(**child + 2);
  });

  auto observer = makeObserver(
      [observer1, observer2]() { return (**observer1) * (**observer2); });

  EXPECT_EQ(43 * 44, *observer.getSnapshot());

  folly::Baton<> baton;
  auto waitingObserver = makeObserver([observer, &baton]() {
    *observer;
    baton.post();
    return folly::Unit();
  });
  baton.reset();

  observable.setValue(24);

  EXPECT_TRUE(baton.try_wait_for(std::chrono::seconds{1}));

  EXPECT_EQ(25 * 26, **observer);
}

TEST(Observer, CreateException) {
  struct ExpectedException {};
  EXPECT_THROW(
      auto observer = makeObserver(
          []() -> std::shared_ptr<int> { throw ExpectedException(); }),
      ExpectedException);

  EXPECT_THROW(
      auto observer =
          makeObserver([]() -> std::shared_ptr<int> { return nullptr; }),
      std::logic_error);
}

TEST(Observer, NullValue) {
  SimpleObservable<int> observable(41);
  auto oddObserver = makeObserver([innerObserver = observable.getObserver()]() {
    auto value = **innerObserver;

    if (value % 2 != 0) {
      return value * 2;
    }

    throw std::logic_error("I prefer odd numbers");
  });

  folly::Baton<> baton;
  auto waitingObserver = makeObserver([oddObserver, &baton]() {
    *oddObserver;
    baton.post();
    return folly::Unit();
  });

  baton.reset();
  EXPECT_EQ(82, **oddObserver);

  observable.setValue(2);

  // Waiting observer shouldn't be updated
  EXPECT_FALSE(baton.try_wait_for(std::chrono::seconds{1}));
  baton.reset();

  EXPECT_EQ(82, **oddObserver);

  observable.setValue(23);

  EXPECT_TRUE(baton.try_wait_for(std::chrono::seconds{1}));

  EXPECT_EQ(46, **oddObserver);
}

TEST(Observer, Cycle) {
  if (!folly::kIsDebug) {
    // Cycle detection is only available in debug builds
    return;
  }

  EXPECT_DEATH(
      [] {
        SimpleObservable<bool> observable(false);
        folly::Optional<Observer<int>> observerB;

        auto observerA =
            makeObserver([observer = observable.getObserver(), &observerB]() {
              if (**observer) {
                return ***observerB;
              }
              return 42;
            });

        observerB = makeObserver([observerA]() { return **observerA; });

        EXPECT_EQ(42, **observerA);
        EXPECT_EQ(42, ***observerB);

        observable.setValue(true);
        folly::observer_detail::ObserverManager::waitForAllUpdates();
      }(),
      "Observer cycle detected.");
}

TEST(Observer, Stress) {
  SimpleObservable<int> observable(0);

  auto values = std::make_shared<folly::Synchronized<std::vector<int>>>();

  auto observer = makeObserver([child = observable.getObserver(), values]() {
    auto value = **child * 10;
    values->withWLock([&](std::vector<int>& vals) { vals.push_back(value); });
    return value;
  });

  EXPECT_EQ(0, **observer);
  values->withRLock([](const std::vector<int>& vals) {
    EXPECT_EQ(1, vals.size());
    EXPECT_EQ(0, vals.back());
  });

  constexpr size_t numIters = 10000;

  for (size_t i = 1; i <= numIters; ++i) {
    observable.setValue(i);
  }

  while (**observer != numIters * 10) {
    std::this_thread::yield();
  }

  values->withRLock([numIters = numIters](const std::vector<int>& vals) {
    EXPECT_EQ(numIters * 10, vals.back());
    EXPECT_LT(vals.size(), numIters / 2);

    EXPECT_EQ(0, vals[0]);
    EXPECT_EQ(numIters * 10, vals.back());

    for (auto value : vals) {
      EXPECT_EQ(0, value % 10);
    }

    for (size_t i = 0; i < vals.size() - 1; ++i) {
      EXPECT_LE(vals[i], vals[i + 1]);
    }
  });
}

TEST(Observer, StressMultipleUpdates) {
  SimpleObservable<int> observable1(0);
  SimpleObservable<int> observable2(0);

  auto observer = makeObserver(
      [o1 = observable1.getObserver(), o2 = observable2.getObserver()]() {
        return (**o1) * (**o2);
      });

  EXPECT_EQ(0, **observer);

  constexpr size_t numIters = 10000;

  for (size_t i = 1; i <= numIters; ++i) {
    observable1.setValue(i);
    observable2.setValue(i);
    folly::observer_detail::ObserverManager::waitForAllUpdates();
    EXPECT_EQ(i * i, **observer);
  }
}

TEST(Observer, TLObserver) {
  auto createTLObserver = [](int value) {
    return folly::observer::makeTLObserver([=] { return value; });
  };

  auto k =
      std::make_unique<folly::observer::TLObserver<int>>(createTLObserver(42));
  EXPECT_EQ(42, ***k);
  k = std::make_unique<folly::observer::TLObserver<int>>(createTLObserver(41));
  EXPECT_EQ(41, ***k);
}

TEST(ReadMostlyTLObserver, ReadMostlyTLObserver) {
  auto createReadMostlyTLObserver = [](int value) {
    return folly::observer::makeReadMostlyTLObserver([=] { return value; });
  };

  auto k = std::make_unique<folly::observer::ReadMostlyTLObserver<int>>(
      createReadMostlyTLObserver(42));
  EXPECT_EQ(42, *k->getShared());
  k = std::make_unique<folly::observer::ReadMostlyTLObserver<int>>(
      createReadMostlyTLObserver(41));
  EXPECT_EQ(41, *k->getShared());
}

TEST(ReadMostlyTLObserver, Update) {
  SimpleObservable<int> observable(42);
  auto observer = observable.getObserver();

  ReadMostlyTLObserver readMostlyObserver(observer);
  EXPECT_EQ(*readMostlyObserver.getShared(), 42);

  observable.setValue(24);

  folly::observer_detail::ObserverManager::waitForAllUpdates();

  EXPECT_EQ(*readMostlyObserver.getShared(), 24);
}

TEST(Observer, SubscribeCallback) {
  static auto mainThreadId = std::this_thread::get_id();
  static std::function<void()> updatesCob;
  static bool slowGet = false;
  static std::atomic<size_t> getCallsStart{0};
  static std::atomic<size_t> getCallsFinish{0};

  struct Observable {
    ~Observable() { EXPECT_EQ(mainThreadId, std::this_thread::get_id()); }
  };
  struct Traits {
    using element_type = int;
    static std::shared_ptr<const int> get(Observable&) {
      ++getCallsStart;
      if (slowGet) {
        /* sleep override */ std::this_thread::sleep_for(
            std::chrono::seconds{2});
      }
      ++getCallsFinish;
      return std::make_shared<const int>(42);
    }

    static void subscribe(Observable&, std::function<void()> cob) {
      updatesCob = std::move(cob);
    }

    static void unsubscribe(Observable&) {}
  };

  std::thread cobThread;
  {
    auto observer =
        folly::observer::ObserverCreator<Observable, Traits>().getObserver();

    EXPECT_TRUE(updatesCob);

    EXPECT_GE(2, getCallsStart);
    EXPECT_GE(2, getCallsFinish);

    updatesCob();

    folly::observer_detail::ObserverManager::waitForAllUpdates();

    EXPECT_EQ(3, getCallsStart);
    EXPECT_EQ(3, getCallsFinish);

    slowGet = true;
    cobThread = std::thread([] { updatesCob(); });
    /* sleep override */ std::this_thread::sleep_for(std::chrono::seconds{1});
    EXPECT_EQ(4, getCallsStart);
    EXPECT_EQ(3, getCallsFinish);

    // Observer is destroyed here
  }

  // Make sure that destroying the observer actually joined the updates callback
  EXPECT_EQ(4, getCallsStart);
  EXPECT_EQ(4, getCallsFinish);
  cobThread.join();
}

TEST(Observer, SetCallback) {
  folly::observer::SimpleObservable<int> observable(42);
  auto observer = observable.getObserver();
  folly::Baton<> baton;
  int callbackValue = 0;
  size_t callbackCallsCount = 0;

  auto callbackHandle =
      observer.addCallback([&](folly::observer::Snapshot<int> snapshot) {
        ++callbackCallsCount;
        callbackValue = *snapshot;
        baton.post();
      });
  baton.wait();
  baton.reset();
  EXPECT_EQ(42, callbackValue);
  EXPECT_EQ(1, callbackCallsCount);

  observable.setValue(43);
  baton.wait();
  baton.reset();
  EXPECT_EQ(43, callbackValue);
  EXPECT_EQ(2, callbackCallsCount);

  callbackHandle.cancel();

  observable.setValue(44);
  EXPECT_FALSE(baton.timed_wait(std::chrono::milliseconds{100}));
  EXPECT_EQ(43, callbackValue);
  EXPECT_EQ(2, callbackCallsCount);
}

TEST(Observer, CallbackCalledOncePerSnapshot) {
  SimpleObservable<folly::Unit> observable(folly::unit);
  auto observer = observable.getObserver();

  int value = 1;
  SimpleObservable<int> intObservable(value);
  auto squareObserver =
      makeObserver([o = intObservable.getObserver()] { return **o * **o; });

  folly::Baton baton;
  size_t callbackCallsCount = 0;
  auto callbackHandle = observer.addCallback([&](auto) {
    // The main point of this test is that the callback depends on
    // `squareObserver`. A refresh of `squareObserver` should not trigger the
    // callback, since the callback is associated only with `observer`.
    //
    // Note that we do not guarantee that **squareObserver necessarily reflects
    // the latest update.
    EXPECT_GE(value * value, **squareObserver);

    ++callbackCallsCount;
    baton.post();
  });

  baton.wait();
  baton.reset();
  EXPECT_EQ(1, callbackCallsCount);

  // Check that any second updates to squareObserver don't trigger the callback
  // again
  folly::observer_detail::ObserverManager::waitForAllUpdates();
  EXPECT_EQ(1, callbackCallsCount);

  value = 2;
  intObservable.setValue(value);
  observable.setValue(folly::Unit{});

  baton.wait();
  baton.reset();
  EXPECT_EQ(2, callbackCallsCount);

  value = 3;
  // Updating intObservable should not trigger the callback
  intObservable.setValue(value);
  folly::observer_detail::ObserverManager::waitForAllUpdates();
  EXPECT_EQ(9, **squareObserver);
  EXPECT_EQ(2, callbackCallsCount);
}

TEST(Observer, CallbackMemoryLeak) {
  folly::observer::SimpleObservable<int> observable(42);
  auto observer = observable.getObserver();
  auto callbackHandle = observer.addCallback([](auto) {});
  // should not leak
  callbackHandle = observer.addCallback([](auto) {});
}

int makeObserverRecursion(int n) {
  if (n == 0) {
    return 0;
  }
  return **makeObserver([=] { return makeObserverRecursion(n - 1) + 1; });
}

TEST(Observer, NestedMakeObserver) {
  EXPECT_EQ(32, makeObserverRecursion(32));
}

TEST(Observer, WaitForAllUpdates) {
  folly::observer::SimpleObservable<int> observable{42};

  auto observer = makeObserver([o = observable.getObserver()] {
    std::this_thread::sleep_for(std::chrono::milliseconds{100});

    return **o;
  });

  EXPECT_EQ(42, **observer);

  observable.setValue(43);
  folly::observer_detail::ObserverManager::waitForAllUpdates();

  EXPECT_EQ(43, **observer);

  folly::observer_detail::ObserverManager::waitForAllUpdates();
}

TEST(Observer, IgnoreUpdates) {
  int callbackCalled = 0;
  folly::observer::SimpleObservable<int> observable(42);
  auto observer =
      folly::observer::makeObserver([even = std::make_shared<bool>(true),
                                     odd = std::make_shared<bool>(false),
                                     observer = observable.getObserver()] {
        if (**observer % 2 == 0) {
          return even;
        }
        return odd;
      });
  auto callbackHandle = observer.addCallback([&](auto) { ++callbackCalled; });
  EXPECT_EQ(1, callbackCalled);

  observable.setValue(43);
  folly::observer_detail::ObserverManager::waitForAllUpdates();
  EXPECT_EQ(2, callbackCalled);

  observable.setValue(45);
  folly::observer_detail::ObserverManager::waitForAllUpdates();
  EXPECT_EQ(2, callbackCalled);

  observable.setValue(46);
  folly::observer_detail::ObserverManager::waitForAllUpdates();
  EXPECT_EQ(3, callbackCalled);
}

TEST(Observer, GetSnapshotOnManagerThread) {
  auto observer42 = folly::observer::makeObserver([] { return 42; });

  folly::observer::SimpleObservable<int> observable(1);

  folly::Baton<> startBaton;
  folly::Baton<> finishBaton;
  folly::Baton<> destructorBaton;

  {
    finishBaton.post();
    auto slowObserver = folly::observer::makeObserver(
        [guard = folly::makeGuard([observer42, &destructorBaton]() {
           // We expect this to be called on a ObserverManager thread, but
           // outside of processing an observer updates.
           observer42.getSnapshot();
           destructorBaton.post();
         }),
         observer = observable.getObserver(),
         &startBaton,
         &finishBaton] {
          startBaton.post();
          finishBaton.wait();
          finishBaton.reset();
          return **observer;
        });

    EXPECT_EQ(1, **slowObserver);

    startBaton.reset();
    finishBaton.post();
    observable.setValue(2);
    folly::observer_detail::ObserverManager::waitForAllUpdates();
    EXPECT_EQ(2, **slowObserver);

    startBaton.reset();
    observable.setValue(3);
    startBaton.wait();
  }
  finishBaton.post();
  destructorBaton.wait();
}

TEST(Observer, Shutdown) {
  folly::SingletonVault::singleton()->destroyInstances();
  auto observer = folly::observer::makeObserver([] { return 42; });
  EXPECT_EQ(42, **observer);
}

TEST(Observer, MakeValueObserver) {
  struct ValueStruct {
    ValueStruct(int value, int id) : value_(value), id_(id) {}
    bool operator==(const ValueStruct& other) const {
      return value_ == other.value_;
    }

    const int value_;
    const int id_;
  };

  SimpleObservable<ValueStruct> observable(ValueStruct(1, 1));

  std::vector<int> observedIds;
  std::vector<int> observedValues;
  std::vector<int> observedValues2;

  auto ch1 = observable.getObserver().addCallback(
      [&](auto snapshot) { observedIds.push_back(snapshot->id_); });
  auto ch2 = makeValueObserver(observable.getObserver())
                 .addCallback([&](auto snapshot) {
                   observedValues.push_back(snapshot->value_);
                 });
  auto ch3 = makeValueObserver([observer = observable.getObserver()] {
               return **observer;
             }).addCallback([&](auto snapshot) {
    observedValues2.push_back(snapshot->value_);
  });
  folly::observer_detail::ObserverManager::waitForAllUpdates();

  observable.setValue(ValueStruct(1, 2));
  folly::observer_detail::ObserverManager::waitForAllUpdates();

  observable.setValue(ValueStruct(2, 3));
  folly::observer_detail::ObserverManager::waitForAllUpdates();

  observable.setValue(ValueStruct(2, 4));
  folly::observer_detail::ObserverManager::waitForAllUpdates();

  observable.setValue(ValueStruct(3, 5));
  folly::observer_detail::ObserverManager::waitForAllUpdates();

  EXPECT_EQ(observedIds, std::vector<int>({1, 2, 3, 4, 5}));
  EXPECT_EQ(observedValues, std::vector<int>({1, 2, 3}));
  EXPECT_EQ(observedValues2, std::vector<int>({1, 2, 3}));

  size_t creatorCalls = 0;
  auto o = makeValueObserver([&] {
    ++creatorCalls;
    return 42;
  });
  EXPECT_EQ(42, **o);
  EXPECT_EQ(1, creatorCalls);
}

TEST(Observer, MakeStaticObserver) {
  auto explicitStringObserver = makeStaticObserver<std::string>("hello");
  EXPECT_EQ(**explicitStringObserver, "hello");

  auto implicitIntObserver = makeStaticObserver(5);
  EXPECT_EQ(**implicitIntObserver, 5);

  auto explicitSharedPtrObserver =
      makeStaticObserver<std::shared_ptr<int>>(std::make_shared<int>(5));
  EXPECT_EQ(***explicitSharedPtrObserver, 5);

  auto implicitSharedPtrObserver = makeStaticObserver(std::make_shared<int>(5));
  EXPECT_EQ(**implicitSharedPtrObserver, 5);
}

TEST(Observer, AtomicObserver) {
  SimpleObservable<int> observable{42};
  SimpleObservable<int> observable2{12};

  AtomicObserver<int> observer{observable.getObserver()};
  AtomicObserver<int> observerCopy{observer};

  EXPECT_EQ(*observer, 42);
  EXPECT_EQ(*observerCopy, 42);
  observable.setValue(24);
  folly::observer_detail::ObserverManager::waitForAllUpdates();
  EXPECT_EQ(*observer, 24);
  EXPECT_EQ(*observerCopy, 24);

  observer = observable2.getObserver();
  EXPECT_EQ(*observer, 12);
  EXPECT_EQ(*observerCopy, 24);
  observable2.setValue(15);
  folly::observer_detail::ObserverManager::waitForAllUpdates();
  EXPECT_EQ(*observer, 15);
  EXPECT_EQ(*observerCopy, 24);

  observerCopy = observer;
  EXPECT_EQ(*observerCopy, 15);

  auto dependentObserver =
      makeAtomicObserver([o = observer] { return *o + 1; });
  EXPECT_EQ(*dependentObserver, 16);
  observable2.setValue(20);
  folly::observer_detail::ObserverManager::waitForAllUpdates();
  EXPECT_EQ(*dependentObserver, 21);
}

TEST(Observer, ReadMostlyAtomicObserver) {
  SimpleObservable<int> observable{42};

  ReadMostlyAtomicObserver<int> observer{observable.getObserver()};

  EXPECT_EQ(*observer, 42);
  observable.setValue(24);
  folly::observer_detail::ObserverManager::waitForAllUpdates();
  EXPECT_EQ(*observer, 24);

  auto dependentObserver = makeReadMostlyAtomicObserver(
      [o = observer.getUnderlyingObserver()] { return **o + 1; });
  EXPECT_EQ(*dependentObserver, 25);
  observable.setValue(20);
  folly::observer_detail::ObserverManager::waitForAllUpdates();
  EXPECT_EQ(*dependentObserver, 21);
}

void runHazptrObserverTest(bool useLocalSnapshot) {
  struct IntHolder {
    explicit IntHolder(int val) : val_(val) {}
    IntHolder(const IntHolder&) = delete;
    IntHolder& operator=(const IntHolder&) = delete;
    IntHolder(IntHolder&&) = default;
    IntHolder& operator=(IntHolder&&) = delete;
    int val_;
  };

  auto value = [=](const auto& observer) {
    if (useLocalSnapshot) {
      return observer.getSnapshot()->val_;
    } else {
      return observer.getLocalSnapshot()->val_;
    }
  };

  SimpleObservable<IntHolder> observable{IntHolder{42}};

  HazptrObserver<IntHolder> observer{observable.getObserver()};
  HazptrObserver<IntHolder> observerCopy{observer};
  EXPECT_EQ(value(observer), 42);
  EXPECT_EQ(value(observerCopy), 42);

  observable.setValue(IntHolder{24});
  folly::observer_detail::ObserverManager::waitForAllUpdates();
  EXPECT_EQ(value(observer), 24);
  EXPECT_EQ(value(observerCopy), 24);

  auto dependentObserver = makeHazptrObserver([o = observable.getObserver()] {
    return IntHolder{o.getSnapshot()->val_ + 1};
  });
  EXPECT_EQ(value(dependentObserver), 25);

  observable.setValue(IntHolder{20});
  folly::observer_detail::ObserverManager::waitForAllUpdates();
  EXPECT_EQ(value(dependentObserver), 21);
}

TEST(Observer, HazptrObserver) {
  runHazptrObserverTest(/* useLocalSnapshot */ false);
}

TEST(Observer, HazptrObserverLocalSnapshot) {
  runHazptrObserverTest(/* useLocalSnapshot */ true);
}

TEST(Observer, Unwrap) {
  SimpleObservable<bool> selectorObservable{true};
  SimpleObservable<int> trueObservable{1};
  SimpleObservable<int> falseObservable{2};

  auto observer = makeObserver([selectorO = selectorObservable.getObserver(),
                                trueO = trueObservable.getObserver(),
                                falseO = falseObservable.getObserver()] {
    if (**selectorO) {
      return trueO;
    }
    return falseO;
  });

  EXPECT_EQ(**observer, 1);

  selectorObservable.setValue(false);
  folly::observer_detail::ObserverManager::waitForAllUpdates();

  EXPECT_EQ(**observer, 2);

  falseObservable.setValue(3);
  folly::observer_detail::ObserverManager::waitForAllUpdates();

  EXPECT_EQ(**observer, 3);

  trueObservable.setValue(4);
  selectorObservable.setValue(true);
  folly::observer_detail::ObserverManager::waitForAllUpdates();
  EXPECT_EQ(**observer, 4);
}

TEST(Observer, UnwrapSimpleObservable) {
  SimpleObservable<int> a{1};
  SimpleObservable<int> b{2};
  SimpleObservable<Observer<int>> observable{a.getObserver()};
  auto o = observable.getObserver();

  EXPECT_EQ(1, **o);

  a.setValue(3);
  folly::observer_detail::ObserverManager::waitForAllUpdates();

  EXPECT_EQ(3, **o);

  observable.setValue(b.getObserver());
  folly::observer_detail::ObserverManager::waitForAllUpdates();

  EXPECT_EQ(2, **o);

  b.setValue(4);
  folly::observer_detail::ObserverManager::waitForAllUpdates();

  EXPECT_EQ(4, **o);
}

TEST(Observer, WithJitterMonotoneProgress) {
  SimpleObservable<int> observable(0);
  auto observer = observable.getObserver();
  EXPECT_EQ(0, **observer);

  auto laggingObserver = withJitter(
      std::move(observer),
      std::chrono::milliseconds{100},
      std::chrono::milliseconds{100});
  EXPECT_EQ(0, **laggingObserver);

  // Updates should never propagate out of order. E.g., if update 1 arrives and
  // is delayed by 100 milliseconds, followed immediately by the arrival of
  // update 2 with 1 millisecond delay, then update 1 should never overwrite
  // update 2.
  for (int i = 1, lastSeen = 0; i <= 50; ++i) {
    auto curr = **laggingObserver;
    EXPECT_LE(lastSeen, curr);
    lastSeen = curr;
    observable.setValue(i);
    /* sleep override */ std::this_thread::sleep_for(
        std::chrono::milliseconds{10});
  }

  /* sleep override */ std::this_thread::sleep_for(std::chrono::seconds{2});
  // The latest update is eventually propagated
  EXPECT_EQ(50, **laggingObserver);
}

TEST(Observer, WithJitterActuallyInducesLag) {
  SimpleObservable<int> observable(0);
  auto observer = observable.getObserver();
  EXPECT_EQ(0, **observer);

  auto laggingObserver = withJitter(
      observer, std::chrono::seconds{10}, std::chrono::milliseconds::zero());
  EXPECT_EQ(0, **laggingObserver);

  observable.setValue(42);
  /* sleep override */ std::this_thread::sleep_for(std::chrono::seconds{1});
  EXPECT_EQ(0, **laggingObserver);
}

TEST(Observer, WithJitterNoEarlyRefresh) {
  SimpleObservable<int> observable(0);
  auto base = observable.getObserver();
  auto copy = makeObserver([base] { return **base; });
  auto laggingObserver = withJitter(
      base, std::chrono::seconds{10}, std::chrono::milliseconds::zero());
  auto delta = makeObserver(
      [copy, laggingObserver] { return **copy - **laggingObserver; });

  EXPECT_EQ(0, **base);
  EXPECT_EQ(0, **copy);
  EXPECT_EQ(0, **laggingObserver);
  EXPECT_EQ(0, **delta);

  observable.setValue(42);
  /* sleep override */ std::this_thread::sleep_for(std::chrono::seconds{1});

  // Updates along the base -> copy -> delta path should not trigger an early
  // refresh of laggingObserver
  EXPECT_EQ(42, **base);
  EXPECT_EQ(42, **copy);
  EXPECT_EQ(0, **laggingObserver);
  EXPECT_EQ(42, **delta);
}

TEST(SimpleObservable, DefaultConstructible) {
  struct Data {
    int i = 42;
  };
  static_assert(std::is_default_constructible<Data>::value);
  static_assert(std::is_default_constructible<SimpleObservable<Data>>::value);

  SimpleObservable<Data> observable;
  EXPECT_EQ((**observable.getObserver()).i, 42);
}

TEST(Observer, MakeObserverUpdatesTracking) {
  SimpleObservable<int> observable(0);
  auto slowObserver = makeObserver([o = observable.getObserver()] {
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    return **o;
  });

  auto tlObserver = makeTLObserver(slowObserver);
  auto rmtlObserver = makeReadMostlyTLObserver(slowObserver);
  auto atomicObserver = makeAtomicObserver(slowObserver);
  auto rmatomicObserver = makeReadMostlyAtomicObserver(slowObserver);
  auto hazptrObserver = makeHazptrObserver(slowObserver);
  EXPECT_EQ(0, **tlObserver);
  EXPECT_EQ(0, *(rmtlObserver.getShared()));
  EXPECT_EQ(0, *atomicObserver);
  EXPECT_EQ(0, *rmatomicObserver);
  EXPECT_EQ(0, *(hazptrObserver.getSnapshot()));
  EXPECT_EQ(0, *(hazptrObserver.getLocalSnapshot()));

  auto tlObserverCheck = makeObserver([&]() mutable { return **tlObserver; });

  auto rmtlObserverCheck =
      makeObserver([&]() mutable { return *(rmtlObserver.getShared()); });

  auto atomicObserverCheck =
      makeObserver([&]() mutable { return *atomicObserver; });

  auto rmatomicObserverCheck =
      makeObserver([&]() mutable { return *rmatomicObserver; });

  auto hazptrObserverGetSnapshotCheck =
      makeObserver([&]() mutable { return *(hazptrObserver.getSnapshot()); });

  auto hazptrObserverGetLocalSnapshotCheck = makeObserver(
      [&]() mutable { return *(hazptrObserver.getLocalSnapshot()); });

  for (size_t i = 1; i <= 10; ++i) {
    observable.setValue(i);
    folly::observer_detail::ObserverManager::waitForAllUpdates();
    EXPECT_EQ(i, **tlObserverCheck);
    EXPECT_EQ(i, **rmtlObserverCheck);
    EXPECT_EQ(i, **atomicObserverCheck);
    EXPECT_EQ(i, **rmatomicObserverCheck);
    EXPECT_EQ(i, **hazptrObserverGetSnapshotCheck);
    EXPECT_EQ(i, **hazptrObserverGetLocalSnapshotCheck);
  }
}

TEST(Observer, Fibers) {
  folly::EventBase evb;
  auto& fm = folly::fibers::getFiberManager(evb);

  auto f1 = fm.addTaskFuture([] {
    auto o = makeObserver([] {
      folly::futures::sleep(std::chrono::milliseconds{10}).get();
      return 1;
    });
    EXPECT_EQ(1, **o);
  });
  auto f2 = fm.addTaskFuture([] {
    auto o = makeObserver([] {
      folly::futures::sleep(std::chrono::milliseconds{20}).get();
      return 2;
    });
    EXPECT_EQ(2, **o);
  });

  std::move(f1).getVia(&evb);
  std::move(f2).getVia(&evb);
}

std::mutex lockingObservableLock;
std::atomic<size_t> lockingObservableValue{0};
folly::Function<void()> lockingObservableCallback;

TEST(Observer, ObservableLockInversion) {
  struct LockingObservable {
    using element_type = size_t;

    std::shared_ptr<const size_t> get() {
      std::lock_guard<std::mutex> lg(lockingObservableLock);
      return std::make_shared<const size_t>(lockingObservableValue.load());
    }

    void subscribe(folly::Function<void()> cb) {
      lockingObservableCallback = std::move(cb);
    }

    void unsubscribe() { lockingObservableCallback = nullptr; }
  };

  auto observer =
      folly::observer::ObserverCreator<LockingObservable>().getObserver();

  EXPECT_EQ(0, **observer);

  constexpr size_t kNumIters = 1000;

  std::thread updater([&] {
    for (size_t i = 1; i <= kNumIters; ++i) {
      lockingObservableValue = i;
      lockingObservableCallback();
    }
  });

  while (true) {
    std::lock_guard<std::mutex> lg(lockingObservableLock);
    if (**makeObserver([o = observer] { return **o; }) == kNumIters) {
      break;
    }
  }

  updater.join();
}

folly::Function<void()> throwingObservableCallback;

TEST(Observer, ObservableGetThrow) {
  struct ThrowingObservable {
    using element_type = size_t;

    std::shared_ptr<const size_t> get() {
      if (getCalled_.exchange(true)) {
        throw std::logic_error("Transient error");
      }

      return std::make_shared<const size_t>(42);
    }

    void subscribe(folly::Function<void()> cb) {
      throwingObservableCallback = std::move(cb);
    }

    void unsubscribe() { throwingObservableCallback = nullptr; }

   private:
    std::atomic<bool> getCalled_{false};
  };

  auto observer =
      folly::observer::ObserverCreator<ThrowingObservable>().getObserver();

  EXPECT_EQ(42, **observer);
  folly::observer_detail::ObserverManager::waitForAllUpdates();
  EXPECT_EQ(42, **observer);
  throwingObservableCallback();
  folly::observer_detail::ObserverManager::waitForAllUpdates();
  EXPECT_EQ(42, **observer);

  struct ExpectedException {};
  struct AlwaysThrowingObservable {
    using element_type = size_t;

    std::shared_ptr<const size_t> get() { throw ExpectedException(); }

    void subscribe(folly::Function<void()>) {}

    void unsubscribe() {}
  };

  EXPECT_THROW(
      folly::observer::ObserverCreator<AlwaysThrowingObservable>()
          .getObserver(),
      ExpectedException);
}

TEST(Observer, ReenableSingletons) {
  folly::observer::SimpleObservable<size_t> observable(0);
  constexpr size_t kMaxValue = 10000;
  std::mutex forkMutex;
  std::thread publishThread([&] {
    for (size_t i = 1; i <= kMaxValue; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
      {
        std::lock_guard<std::mutex> lg(forkMutex);
        observable.setValue(i);
      }
    }
  });
  auto observer = observable.getObserver();
  while (**observer < kMaxValue) {
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    folly::SingletonVault::singleton()->destroyInstances();
    {
      std::lock_guard<std::mutex> lg(forkMutex);
      folly::SingletonVault::singleton()->reenableInstances();
    }
    folly::observer_detail::ObserverManager::vivify();
  }
  publishThread.join();
}
