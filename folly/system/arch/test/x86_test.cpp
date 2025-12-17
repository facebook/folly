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

#include <folly/system/arch/x86.h>

#include <array>

#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

struct X86Test : testing::Test {};

TEST_F(X86Test, x86_cpuid) {
  unsigned info[4];
  folly::x86_cpuid(info, 0);
  if (folly::kIsArchX86 || folly::kIsArchAmd64) {
    EXPECT_NE(0, info[0]);
    EXPECT_NE(0, info[1]);
  } else {
    EXPECT_EQ(0, info[0]);
    EXPECT_EQ(0, info[1]);
  }
}

TEST_F(X86Test, x86_cpuid_max) {
  unsigned sig;
  auto res = folly::x86_cpuid_max(0, &sig);
  if (folly::kIsArchX86 || folly::kIsArchAmd64) {
    EXPECT_NE(0, res);
    EXPECT_NE(0, sig);
  } else {
    EXPECT_EQ(0, res);
    EXPECT_EQ(0, sig);
  }
}

TEST_F(X86Test, x86_cpuid_get_vendor) {
  auto vend = folly::x86_cpuid_get_vendor();
  if (folly::kIsArchX86 || folly::kIsArchAmd64) {
    constexpr auto avail = std::array{
        folly::x86_cpuid_vendor::intel,
        folly::x86_cpuid_vendor::amd,
    };
    EXPECT_THAT(avail, testing::Contains(vend));
  } else {
    EXPECT_EQ(folly::x86_cpuid_vendor::unknown, vend);
  }
}

TEST_F(X86Test, x86_cpuid_get_llc_cache_info) {
  auto info = folly::x86_cpuid_get_llc_cache_info();
  if (folly::kIsArchX86 || folly::kIsArchAmd64) {
    EXPECT_FALSE(info.cache_type_null());
    EXPECT_LT(0, info.cache_size());
  } else {
    EXPECT_TRUE(info.cache_type_null());
    EXPECT_EQ(0, info.cache_size());
  }
}
