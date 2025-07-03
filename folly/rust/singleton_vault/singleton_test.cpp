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

#include <folly/rust/singleton_vault/singleton_test.h>

#include <folly/Singleton.h>

namespace facebook::folly::rust::test {

namespace {

struct EagerSingletonTag {};

struct EagerSingleton final {
  int32_t value = 12;

  EagerSingleton() noexcept { value = 14; }
};

auto eagerSingleton =
    ::folly::Singleton<EagerSingleton, EagerSingletonTag>().shouldEagerInit();

struct NormalSingletonTag {};

struct NormalSingleton final {
  int64_t value = 42;

  NormalSingleton() {
    auto bar = eagerSingleton.try_get();
    this->value = bar->value + 42;
  }
};

auto normalSingleton =
    ::folly::Singleton<NormalSingleton, NormalSingletonTag>();

} // namespace

void initNormalSingleton() {
  auto foo = normalSingleton.try_get();
  foo->value += 2;
}

int64_t getNormalSingleton() {
  auto foo = normalSingleton.try_get();
  return foo->value;
}

int32_t getEagerSingleton() {
  auto bar = eagerSingleton.try_get();
  return bar->value;
}

} // namespace facebook::folly::rust::test
