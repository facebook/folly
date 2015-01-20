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

#include <folly/experimental/Singleton.h>
#include <folly/experimental/SingletonVault_c.h>

#include <gtest/gtest.h>

#include <thread>

__thread long instance_counter_instances = 0;

class InstanceCounter {
 public:
  InstanceCounter() {
    instance_counter_instances++;
  }

  ~InstanceCounter() {
    instance_counter_instances--;
  }
};

TEST(SingletonVault, singletonReturnsSingletonInstance) {
  SingletonVault_t *c = SingletonVault_singleton();
  auto *cpp = folly::SingletonVault::singleton();
  EXPECT_EQ(c, cpp);
}

TEST(SingletonVault, singletonsAreCreatedAndDestroyed) {
  auto *vault = new folly::SingletonVault();
  folly::Singleton<InstanceCounter> counter_singleton(nullptr, nullptr, vault);
  SingletonVault_registrationComplete((SingletonVault_t*) vault);
  InstanceCounter *counter = folly::Singleton<InstanceCounter>::get(vault);
  EXPECT_EQ(instance_counter_instances, 1);
  delete vault;
  EXPECT_EQ(instance_counter_instances, 0);
}
