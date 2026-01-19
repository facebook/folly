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

#include <folly/hash/UniqueHashKey.h>

#include <folly/container/F14Map.h>
#include <folly/container/MapUtil.h>
#include <folly/portability/GTest.h>

template <typename Algo>
struct UniqueHashKeyTest : testing::TestWithParam<Algo> {};
TYPED_TEST_SUITE_P(UniqueHashKeyTest);

TYPED_TEST_P(UniqueHashKeyTest, Example) {
  constexpr TypeParam algo{};
  constexpr auto size = folly::unique_hash_key_algo_size_v<algo>;
  using Key = folly::unique_hash_key<size>;
  ASSERT_EQ(size, sizeof(Key));

  using Map = folly::F14FastMap<Key, int>;

  auto const key0 = Key(algo, "hello ", "world");
  auto const key1 = Key(algo, "hello", " world");

  EXPECT_EQ(key0, key0);
  EXPECT_EQ(key1, key1);
  EXPECT_NE(key0, key1);
  EXPECT_NE(key1, key0);

  auto const key0_sp16 = std::span<uint16_t const>(key0);
  auto const key1_sp16 = std::span<uint16_t const>(key1);
  EXPECT_TRUE(std::ranges::equal(key0_sp16, key0_sp16));
  EXPECT_TRUE(std::ranges::equal(key1_sp16, key1_sp16));
  EXPECT_FALSE(std::ranges::equal(key0_sp16, key1_sp16));
  EXPECT_FALSE(std::ranges::equal(key1_sp16, key0_sp16));

  Map map;
  map[key0] = 3;
  map[key1] = 4;

  EXPECT_EQ(3, folly::get_default(map, key0));
  EXPECT_EQ(4, folly::get_default(map, key1));
}

REGISTER_TYPED_TEST_SUITE_P(UniqueHashKeyTest, Example);

INSTANTIATE_TYPED_TEST_SUITE_P(
    SHA256_8, //
    UniqueHashKeyTest,
    folly::unique_hash_key_algo_strong_sha256_fn<8>);
INSTANTIATE_TYPED_TEST_SUITE_P(
    SHA256_16, //
    UniqueHashKeyTest,
    folly::unique_hash_key_algo_strong_sha256_fn<16>);
INSTANTIATE_TYPED_TEST_SUITE_P(
    SHA256_24, //
    UniqueHashKeyTest,
    folly::unique_hash_key_algo_strong_sha256_fn<24>);
INSTANTIATE_TYPED_TEST_SUITE_P(
    SHA256_32, //
    UniqueHashKeyTest,
    folly::unique_hash_key_algo_strong_sha256_fn<32>);

#if __has_include(<blake3.h>)

INSTANTIATE_TYPED_TEST_SUITE_P(
    BLAKE3_8, //
    UniqueHashKeyTest,
    folly::unique_hash_key_algo_strong_blake3_fn<8>);
INSTANTIATE_TYPED_TEST_SUITE_P(
    BLAKE3_16, //
    UniqueHashKeyTest,
    folly::unique_hash_key_algo_strong_blake3_fn<16>);
INSTANTIATE_TYPED_TEST_SUITE_P(
    BLAKE3_24, //
    UniqueHashKeyTest,
    folly::unique_hash_key_algo_strong_blake3_fn<24>);
INSTANTIATE_TYPED_TEST_SUITE_P(
    BLAKE3_32, //
    UniqueHashKeyTest,
    folly::unique_hash_key_algo_strong_blake3_fn<32>);

#if __has_include(<xxh3.h>)

INSTANTIATE_TYPED_TEST_SUITE_P(
    XXH3_8, //
    UniqueHashKeyTest,
    folly::unique_hash_key_algo_fast_xxh3_fn<8>);
INSTANTIATE_TYPED_TEST_SUITE_P(
    XXH3_16, //
    UniqueHashKeyTest,
    folly::unique_hash_key_algo_fast_xxh3_fn<16>);

#endif // __has_include(<xxh3.h>)

#endif // __has_include(<blake3.h>)
