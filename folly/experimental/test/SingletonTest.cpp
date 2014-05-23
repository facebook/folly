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

#include <folly/experimental/Singleton.h>

#include <folly/Benchmark.h>

#include <glog/logging.h>
#include <gtest/gtest.h>

using namespace folly;

// A simple class that tracks how often instances of the class and
// subclasses are created, and the ordering.
struct Watchdog {
  static std::vector<Watchdog*> creation_order;
  Watchdog() { creation_order.push_back(this); }

  ~Watchdog() {
    if (creation_order.back() != this) {
      throw std::out_of_range("Watchdog destruction order mismatch");
    }
    creation_order.pop_back();
  }

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

// Some pathological cases such as getting unregistered singletons,
// double registration, etc.
TEST(Singleton, NaughtyUsage) {
  SingletonVault vault;
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

  EXPECT_THROW([]() { Singleton<Watchdog> watchdog_singleton; }(),
               std::logic_error);

  SingletonVault vault_2;
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

  LOG(ERROR) << "The following log message regarding ref counts is expected";
  vault.destroyInstances();
  EXPECT_EQ(vault.registeredSingletonCount(), 2);
  EXPECT_EQ(vault.livingSingletonCount(), 0);

  EXPECT_EQ(shared_s1.use_count(), 1);
  EXPECT_EQ(shared_s1.get(), s1);

  auto locked_s1 = weak_s1.lock();
  EXPECT_EQ(locked_s1.get(), s1);
  EXPECT_EQ(shared_s1.use_count(), 2);
  locked_s1.reset();
  EXPECT_EQ(shared_s1.use_count(), 1);
  shared_s1.reset();
  locked_s1 = weak_s1.lock();
  EXPECT_TRUE(weak_s1.expired());

  Watchdog* new_s1 = Singleton<Watchdog>::get(&vault);
  EXPECT_NE(new_s1, s1);
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

struct BenchmarkSingleton {
  int val = 0;
};

BENCHMARK(NormalSingleton, n) {
  for (int i = 0; i < n; ++i) {
    doNotOptimizeAway(getNormalSingleton());
  }
}

BENCHMARK_RELATIVE(MeyersSingleton, n) {
  for (int i = 0; i < n; ++i) {
    doNotOptimizeAway(getMeyersSingleton());
  }
}

BENCHMARK_RELATIVE(FollySingleton, n) {
  SingletonVault benchmark_vault;
  Singleton<BenchmarkSingleton> benchmark_singleton(
      nullptr, nullptr, &benchmark_vault);
  benchmark_vault.registrationComplete();

  for (int i = 0; i < n; ++i) {
    doNotOptimizeAway(Singleton<BenchmarkSingleton>::get(&benchmark_vault));
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
