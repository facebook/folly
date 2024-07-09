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

#if defined(__linux__)
#include <sys/auxv.h> // @manual
#endif

#include <folly/system/AuxVector.h>

#include <folly/portability/GTest.h>

using namespace folly;

TEST(ElfHwCaps, Simple) {
  ElfHwCaps caps;
#if defined(__linux__)
  // All aarch64 CPUs should support floating point
  EXPECT_EQ(kIsArchAArch64, caps.aarch64_fp());
#else
  GTEST_SKIP();
#endif
}

TEST(ElfHwCaps, SimpleStatic) {
  static ElfHwCaps caps;
#if defined(__linux__)
  // All aarch64 CPUs should support floating point
  EXPECT_EQ(kIsArchAArch64, caps.aarch64_fp());
#else
  GTEST_SKIP();
#endif
}

TEST(ElfHwCaps, SimpleStaticWithArgs) {
#if defined(__linux__)
  static ElfHwCaps caps{getauxval(AT_HWCAP), getauxval(AT_HWCAP2)};
  // All aarch64 CPUs should support floating point
  EXPECT_EQ(kIsArchAArch64, caps.aarch64_fp());
#else
  GTEST_SKIP();
#endif
}
