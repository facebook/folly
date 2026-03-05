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

#include <folly/system/HardwareConcurrency.h>

#include <cstdlib>

#include <folly/portability/GTest.h>
#include <folly/system/EnvUtil.h>

TEST(HardwareConcurrency, ReturnsNonzero) {
  auto concurrency = folly::available_concurrency();
  EXPECT_GT(concurrency, 0u);
}

TEST(HardwareConcurrency, EnvVarOverride) {
  folly::test::EnvVarSaver saver;
  ::setenv(folly::available_concurrency_max_env.c_str(), "1", 1);
  EXPECT_EQ(folly::available_concurrency(), 1u);
}

TEST(HardwareConcurrency, EnvVarInvalidIgnored) {
  folly::test::EnvVarSaver saver;
  ::setenv(folly::available_concurrency_max_env.c_str(), "garbage", 1);
  EXPECT_GT(folly::available_concurrency(), 0u);
}

TEST(HardwareConcurrency, EnvVarZeroIgnored) {
  folly::test::EnvVarSaver saver;
  ::setenv(folly::available_concurrency_max_env.c_str(), "0", 1);
  EXPECT_GT(folly::available_concurrency(), 0u);
}

TEST(HardwareConcurrency, EnvVarUnsetNormalBehavior) {
  folly::test::EnvVarSaver saver;
  ::unsetenv(folly::available_concurrency_max_env.c_str());
  EXPECT_GT(folly::available_concurrency(), 0u);
}
