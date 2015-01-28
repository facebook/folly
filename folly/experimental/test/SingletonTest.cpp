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

#include <thread>

#include <folly/experimental/Singleton.h>

#include <folly/Benchmark.h>

#include <glog/logging.h>
#include <gtest/gtest.h>

using namespace folly;

// A simple class that tracks how often instances of the class and
// subclasses are created, and the ordering.  Also tracks a global
// unique counter for each object.
std::atomic<size_t> global_counter(19770326);
struct Watchdog {
  static std::vector<Watchdog*> creation_order;
  Watchdog() : serial_number(++global_counter) {
    creation_order.push_back(this);
  }

  ~Watchdog() {
    if (creation_order.back() != this) {
      throw std::out_of_range("Watchdog destruction order mismatch");
    }
    creation_order.pop_back();
  }

  const size_t serial_number;
  size_t livingWatchdogCount() const { return creation_order.size(); }

  Watchdog(const Watchdog&) = delete;
  Watchdog& operator=(const Watchdog&) = delete;
  Watchdog(Watchdog&&) noexcept = default;
};

std::vector<Watchdog*> Watchdog::creation_order;

// Some basic types we use for tracking.
struct ChildWatchdog : public Watchdog {};
struct GlobalWatchdog : public Watchdog {};
struct UnregisteredWatchdog : public Watchdog {};

namespace {
Singleton<GlobalWatchdog> global_watchdog;
}

// Test basic global usage (the default way singletons will generally
// be used).
TEST(Singleton, BasicGlobalUsage) {
  EXPECT_EQ(Watchdog::creation_order.size(), 0);
  EXPECT_EQ(SingletonVault::singleton()->registeredSingletonCount(), 1);
  EXPECT_EQ(SingletonVault::singleton()->livingSingletonCount(), 0);
  auto wd1 = Singleton<GlobalWatchdog>::get();
  EXPECT_NE(wd1, nullptr);
  EXPECT_EQ(Watchdog::creation_order.size(), 1);
  auto wd2 = Singleton<GlobalWatchdog>::get();
  EXPECT_NE(wd2, nullptr);
  EXPECT_EQ(wd1, wd2);
  EXPECT_EQ(Watchdog::creation_order.size(), 1);
  SingletonVault::singleton()->destroyInstances();
  EXPECT_EQ(Watchdog::creation_order.size(), 0);
}

TEST(Singleton, MissingSingleton) {
  EXPECT_THROW([]() { auto u = Singleton<UnregisteredWatchdog>::get(); }(),
               std::out_of_range);
}

// Exercise some basic codepaths ensuring registration order and
// destruction order happen as expected, that instances are created
// when expected, etc etc.
TEST(Singleton, BasicUsage) {
  SingletonVault vault;

  EXPECT_EQ(vault.registeredSingletonCount(), 0);
  Singleton<Watchdog> watchdog_singleton(nullptr, nullptr, &vault);
  EXPECT_EQ(vault.registeredSingletonCount(), 1);

  Singleton<ChildWatchdog> child_watchdog_singleton(nullptr, nullptr, &vault);
  EXPECT_EQ(vault.registeredSingletonCount(), 2);

  vault.registrationComplete();

  Watchdog* s1 = Singleton<Watchdog>::get(&vault);
  EXPECT_NE(s1, nullptr);

  Watchdog* s2 = Singleton<Watchdog>::get(&vault);
  EXPECT_NE(s2, nullptr);

  EXPECT_EQ(s1, s2);

  auto s3 = Singleton<ChildWatchdog>::get(&vault);
  EXPECT_NE(s3, nullptr);
  EXPECT_NE(s2, s3);

  EXPECT_EQ(vault.registeredSingletonCount(), 2);
  EXPECT_EQ(vault.livingSingletonCount(), 2);

  vault.destroyInstances();
  EXPECT_EQ(vault.registeredSingletonCount(), 2);
  EXPECT_EQ(vault.livingSingletonCount(), 0);
}

TEST(Singleton, DirectUsage) {
  SingletonVault vault;

  EXPECT_EQ(vault.registeredSingletonCount(), 0);

  // Verify we can get to the underlying singletons via directly using
  // the singleton definition.
  Singleton<Watchdog> watchdog(nullptr, nullptr, &vault);
  struct TestTag {};
  Singleton<Watchdog, TestTag> named_watchdog(nullptr, nullptr, &vault);
  EXPECT_EQ(vault.registeredSingletonCount(), 2);
  vault.registrationComplete();

  EXPECT_NE(watchdog.ptr(), nullptr);
  EXPECT_EQ(watchdog.ptr(), Singleton<Watchdog>::get(&vault));
  EXPECT_NE(watchdog.ptr(), named_watchdog.ptr());
  EXPECT_EQ(watchdog->livingWatchdogCount(), 2);
  EXPECT_EQ((*watchdog).livingWatchdogCount(), 2);
}

TEST(Singleton, NamedUsage) {
  SingletonVault vault;

  EXPECT_EQ(vault.registeredSingletonCount(), 0);

  // Define two named Watchdog singletons and one unnamed singleton.
  struct Watchdog1 {};
  struct Watchdog2 {};
  typedef detail::DefaultTag Watchdog3;
  Singleton<Watchdog, Watchdog1> watchdog1_singleton(nullptr, nullptr, &vault);
  EXPECT_EQ(vault.registeredSingletonCount(), 1);
  Singleton<Watchdog, Watchdog2> watchdog2_singleton(nullptr, nullptr, &vault);
  EXPECT_EQ(vault.registeredSingletonCount(), 2);
  Singleton<Watchdog, Watchdog3> watchdog3_singleton(nullptr, nullptr, &vault);
  EXPECT_EQ(vault.registeredSingletonCount(), 3);

  vault.registrationComplete();

  // Verify our three singletons are distinct and non-nullptr.
  Watchdog* s1 = Singleton<Watchdog, Watchdog1>::get(&vault);
  EXPECT_EQ(s1, watchdog1_singleton.ptr());
  Watchdog* s2 = Singleton<Watchdog, Watchdog2>::get(&vault);
  EXPECT_EQ(s2, watchdog2_singleton.ptr());
  EXPECT_NE(s1, s2);
  Watchdog* s3 = Singleton<Watchdog, Watchdog3>::get(&vault);
  EXPECT_EQ(s3, watchdog3_singleton.ptr());
  EXPECT_NE(s3, s1);
  EXPECT_NE(s3, s2);

  // Verify the "default" singleton is the same as the DefaultTag-tagged
  // singleton.
  Watchdog* s4 = Singleton<Watchdog>::get(&vault);
  EXPECT_EQ(s4, watchdog3_singleton.ptr());
}

// Some pathological cases such as getting unregistered singletons,
// double registration, etc.
TEST(Singleton, NaughtyUsage) {
  SingletonVault vault(SingletonVault::Type::Strict);
  vault.registrationComplete();

  // Unregistered.
  EXPECT_THROW(Singleton<Watchdog>::get(), std::out_of_range);
  EXPECT_THROW(Singleton<Watchdog>::get(&vault), std::out_of_range);

  // Registring singletons after registrationComplete called.
  EXPECT_THROW([&vault]() {
                 Singleton<Watchdog> watchdog_singleton(
                     nullptr, nullptr, &vault);
               }(),
               std::logic_error);

  SingletonVault vault_2(SingletonVault::Type::Strict);
  EXPECT_THROW(Singleton<Watchdog>::get(&vault_2), std::logic_error);
  Singleton<Watchdog> watchdog_singleton(nullptr, nullptr, &vault_2);
  // double registration
  EXPECT_THROW([&vault_2]() {
                 Singleton<Watchdog> watchdog_singleton(
                     nullptr, nullptr, &vault_2);
               }(),
               std::logic_error);
  vault_2.destroyInstances();
  // double registration after destroy
  EXPECT_THROW([&vault_2]() {
                 Singleton<Watchdog> watchdog_singleton(
                     nullptr, nullptr, &vault_2);
               }(),
               std::logic_error);
}

TEST(Singleton, SharedPtrUsage) {
  SingletonVault vault;

  EXPECT_EQ(vault.registeredSingletonCount(), 0);
  Singleton<Watchdog> watchdog_singleton(nullptr, nullptr, &vault);
  EXPECT_EQ(vault.registeredSingletonCount(), 1);

  Singleton<ChildWatchdog> child_watchdog_singleton(nullptr, nullptr, &vault);
  EXPECT_EQ(vault.registeredSingletonCount(), 2);

  struct ATag {};
  Singleton<Watchdog, ATag> named_watchdog_singleton(nullptr, nullptr, &vault);
  vault.registrationComplete();

  Watchdog* s1 = Singleton<Watchdog>::get(&vault);
  EXPECT_NE(s1, nullptr);

  Watchdog* s2 = Singleton<Watchdog>::get(&vault);
  EXPECT_NE(s2, nullptr);

  EXPECT_EQ(s1, s2);

  auto weak_s1 = Singleton<Watchdog>::get_weak(&vault);
  auto shared_s1 = weak_s1.lock();
  EXPECT_EQ(shared_s1.get(), s1);
  EXPECT_EQ(shared_s1.use_count(), 2);

  {
    auto named_weak_s1 = Singleton<Watchdog, ATag>::get_weak(&vault);
    auto locked = named_weak_s1.lock();
    EXPECT_NE(locked.get(), shared_s1.get());
  }

  LOG(ERROR) << "The following log message regarding shared_ptr is expected";
  {
    auto start_time = std::chrono::steady_clock::now();
    vault.destroyInstances();
    auto duration = std::chrono::steady_clock::now() - start_time;
    EXPECT_TRUE(duration > std::chrono::seconds{4} &&
                duration < std::chrono::seconds{6});
  }
  EXPECT_EQ(vault.registeredSingletonCount(), 3);
  EXPECT_EQ(vault.livingSingletonCount(), 0);

  EXPECT_EQ(shared_s1.use_count(), 1);
  EXPECT_EQ(shared_s1.get(), s1);

  auto locked_s1 = weak_s1.lock();
  EXPECT_EQ(locked_s1.get(), s1);
  EXPECT_EQ(shared_s1.use_count(), 2);
  locked_s1.reset();
  EXPECT_EQ(shared_s1.use_count(), 1);

  // Track serial number rather than pointer since the memory could be
  // re-used when we create new_s1.
  auto old_serial = shared_s1->serial_number;
  shared_s1.reset();
  locked_s1 = weak_s1.lock();
  EXPECT_TRUE(weak_s1.expired());

  auto empty_s1 = Singleton<Watchdog>::get_weak(&vault);
  EXPECT_FALSE(empty_s1.lock());

  vault.reenableInstances();

  // Singleton should be re-created only after reenableInstances() was called.
  Watchdog* new_s1 = Singleton<Watchdog>::get(&vault);
  EXPECT_NE(new_s1->serial_number, old_serial);

  auto new_s1_weak = Singleton<Watchdog>::get_weak(&vault);
  auto new_s1_shared = new_s1_weak.lock();
  std::thread t([new_s1_shared]() mutable {
      std::this_thread::sleep_for(std::chrono::seconds{2});
      new_s1_shared.reset();
    });
  new_s1_shared.reset();
  {
    auto start_time = std::chrono::steady_clock::now();
    vault.destroyInstances();
    auto duration = std::chrono::steady_clock::now() - start_time;
    EXPECT_TRUE(duration > std::chrono::seconds{1} &&
                duration < std::chrono::seconds{3});
  }
  EXPECT_TRUE(new_s1_weak.expired());
  t.join();
}

// Some classes to test singleton dependencies.  NeedySingleton has a
// dependency on NeededSingleton, which happens during its
// construction.
SingletonVault needy_vault;

struct NeededSingleton {};
struct NeedySingleton {
  NeedySingleton() {
    auto unused = Singleton<NeededSingleton>::get(&needy_vault);
    EXPECT_NE(unused, nullptr);
  }
};

// Ensure circular dependencies fail -- a singleton that needs itself, whoops.
SingletonVault self_needy_vault;
struct SelfNeedySingleton {
  SelfNeedySingleton() {
    auto unused = Singleton<SelfNeedySingleton>::get(&self_needy_vault);
    EXPECT_NE(unused, nullptr);
  }
};

TEST(Singleton, SingletonDependencies) {
  Singleton<NeededSingleton> needed_singleton(nullptr, nullptr, &needy_vault);
  Singleton<NeedySingleton> needy_singleton(nullptr, nullptr, &needy_vault);
  needy_vault.registrationComplete();

  EXPECT_EQ(needy_vault.registeredSingletonCount(), 2);
  EXPECT_EQ(needy_vault.livingSingletonCount(), 0);

  auto needy = Singleton<NeedySingleton>::get(&needy_vault);
  EXPECT_EQ(needy_vault.livingSingletonCount(), 2);

  Singleton<SelfNeedySingleton> self_needy_singleton(
      nullptr, nullptr, &self_needy_vault);
  self_needy_vault.registrationComplete();
  EXPECT_THROW([]() {
                 Singleton<SelfNeedySingleton>::get(&self_needy_vault);
               }(),
               std::out_of_range);
}

// A test to ensure multiple threads contending on singleton creation
// properly wait for creation rather than thinking it is a circular
// dependency.
class Slowpoke : public Watchdog {
 public:
  Slowpoke() { std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
};

TEST(Singleton, SingletonConcurrency) {
  SingletonVault vault;
  Singleton<Slowpoke> slowpoke_singleton(nullptr, nullptr, &vault);
  vault.registrationComplete();

  std::mutex gatekeeper;
  gatekeeper.lock();
  auto func = [&vault, &gatekeeper]() {
    gatekeeper.lock();
    gatekeeper.unlock();
    auto unused = Singleton<Slowpoke>::get(&vault);
  };

  EXPECT_EQ(vault.livingSingletonCount(), 0);
  std::vector<std::thread> threads;
  for (int i = 0; i < 100; ++i) {
    threads.emplace_back(func);
  }
  // If circular dependency checks fail, the unlock would trigger a
  // crash.  Instead, it succeeds, and we have exactly one living
  // singleton.
  gatekeeper.unlock();
  for (auto& t : threads) {
    t.join();
  }
  EXPECT_EQ(vault.livingSingletonCount(), 1);
}

TEST(Singleton, SingletonConcurrencyStress) {
  SingletonVault vault;
  Singleton<Slowpoke> slowpoke_singleton(nullptr, nullptr, &vault);

  std::vector<std::thread> ts;
  for (size_t i = 0; i < 100; ++i) {
    ts.emplace_back([&]() {
        slowpoke_singleton.get_weak(&vault).lock();
      });
  }

  for (size_t i = 0; i < 100; ++i) {
    std::chrono::milliseconds d(20);

    std::this_thread::sleep_for(d);
    vault.destroyInstances();
    std::this_thread::sleep_for(d);
    vault.destroyInstances();
  }

  for (auto& t : ts) {
    t.join();
  }
}

// Benchmarking a normal singleton vs a Meyers singleton vs a Folly
// singleton.  Meyers are insanely fast, but (hopefully) Folly
// singletons are fast "enough."
int* getMeyersSingleton() {
  static auto ret = new int(0);
  return ret;
}

int normal_singleton_value = 0;
int* getNormalSingleton() {
  doNotOptimizeAway(&normal_singleton_value);
  return &normal_singleton_value;
}

// Verify that existing Singleton's can be overridden
// using the make_mock functionality.
TEST(Singleton, make_mock) {
  SingletonVault vault(SingletonVault::Type::Strict);
  Singleton<Watchdog> watchdog_singleton(nullptr, nullptr, &vault);
  vault.registrationComplete();

  // Registring singletons after registrationComplete called works
  // with make_mock (but not with Singleton ctor).
  EXPECT_EQ(vault.registeredSingletonCount(), 1);
  int serial_count_first = Singleton<Watchdog>::get(&vault)->serial_number;

  // Override existing mock using make_mock.
  Singleton<Watchdog>::make_mock(nullptr, nullptr, &vault);

  EXPECT_EQ(vault.registeredSingletonCount(), 1);
  int serial_count_mock = Singleton<Watchdog>::get(&vault)->serial_number;

  // If serial_count value is the same, then singleton was not replaced.
  EXPECT_NE(serial_count_first, serial_count_mock);
}

struct BenchmarkSingleton {
  int val = 0;
};

BENCHMARK(NormalSingleton, n) {
  for (size_t i = 0; i < n; ++i) {
    doNotOptimizeAway(getNormalSingleton());
  }
}

BENCHMARK_RELATIVE(MeyersSingleton, n) {
  for (size_t i = 0; i < n; ++i) {
    doNotOptimizeAway(getMeyersSingleton());
  }
}

BENCHMARK_RELATIVE(FollySingletonSlow, n) {
  SingletonVault benchmark_vault;
  Singleton<BenchmarkSingleton> benchmark_singleton(
      nullptr, nullptr, &benchmark_vault);
  benchmark_vault.registrationComplete();

  for (size_t i = 0; i < n; ++i) {
    doNotOptimizeAway(Singleton<BenchmarkSingleton>::get(&benchmark_vault));
  }
}

BENCHMARK_RELATIVE(FollySingletonFast, n) {
  SingletonVault benchmark_vault;
  Singleton<BenchmarkSingleton> benchmark_singleton(
      nullptr, nullptr, &benchmark_vault);
  benchmark_vault.registrationComplete();

  for (size_t i = 0; i < n; ++i) {
    doNotOptimizeAway(benchmark_singleton.get_fast());
  }
}

BENCHMARK_RELATIVE(FollySingletonFastWeak, n) {
  SingletonVault benchmark_vault;
  Singleton<BenchmarkSingleton> benchmark_singleton(
      nullptr, nullptr, &benchmark_vault);
  benchmark_vault.registrationComplete();

  for (size_t i = 0; i < n; ++i) {
    benchmark_singleton.get_weak_fast();
  }
}

int main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);
  google::InitGoogleLogging(argv[0]);
  google::ParseCommandLineFlags(&argc, &argv, true);

  SingletonVault::singleton()->registrationComplete();

  auto ret = RUN_ALL_TESTS();
  if (!ret) {
    folly::runBenchmarksOnFlag();
  }
  return ret;
}
