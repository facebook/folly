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

#include <folly/memory/SanitizeAddress.h>

#include <new>

#include <folly/portability/GTest.h>

class SanitizeAddressTest : public testing::Test {};

TEST_F(SanitizeAddressTest, asan_poison) {
  constexpr auto page = size_t(1 << 12);
  constexpr auto size = page * 4;
  constexpr auto offs = 27;
  enum class opaque : unsigned char {};

  // malloc

  auto const addr =
      static_cast<opaque*>(operator new(size, std::align_val_t{page}));

  EXPECT_EQ(nullptr, folly::asan_region_is_poisoned(addr, size));
  EXPECT_EQ(0, folly::asan_address_is_poisoned(addr + offs));

  // poison

  folly::asan_poison_memory_region(addr + page, page);
  EXPECT_EQ(
      folly::kIsSanitizeAddress ? addr + page : nullptr,
      folly::asan_region_is_poisoned(addr, size));
  EXPECT_EQ(
      nullptr,
      folly::asan_region_is_poisoned(addr + 2 * page, size - 2 * page));
  EXPECT_EQ(
      folly::kIsSanitizeAddress ? 1 : 0,
      folly::asan_address_is_poisoned(addr + page + offs));
  EXPECT_EQ(0, folly::asan_address_is_poisoned(addr + 2 * page + offs));

  // unpoison

  folly::asan_unpoison_memory_region(addr + page, page);
  EXPECT_EQ(nullptr, folly::asan_region_is_poisoned(addr, size));
  EXPECT_EQ(0, folly::asan_address_is_poisoned(addr + page + offs));

  // free

  operator delete(addr, size, std::align_val_t{page});
  EXPECT_EQ(
      folly::kIsSanitizeAddress ? addr : nullptr,
      folly::asan_region_is_poisoned(addr, size));
  EXPECT_EQ(
      folly::kIsSanitizeAddress ? 1 : 0,
      folly::asan_address_is_poisoned(addr + offs));
}
