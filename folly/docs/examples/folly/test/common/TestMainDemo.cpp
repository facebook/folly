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

/**
 * This file demonstrates the usage of folly/test/common:test_main_lib,
 * which is a `main` implementation that initializes gtest & folly before
 * running tests.
 * @file
 */

#include <folly/Singleton.h>

#include <gtest/gtest.h>

using namespace ::testing;

namespace {
/// This is a singleton that demonstrates folly will be initialized by
/// `//folly/test/common:test_main_lib`.
folly::Singleton<int> DemoSingleton([]() { return new int(42); });
} // namespace

TEST(TestMainDemo, SingletonAccess) {
  ASSERT_EQ(*DemoSingleton.try_get(), 42);
}
