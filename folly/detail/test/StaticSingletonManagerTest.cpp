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

#include <folly/detail/StaticSingletonManager.h>

#include <folly/lang/Keep.h>
#include <folly/portability/GTest.h>

namespace folly {
namespace detail {

FOLLY_ATTR_WEAK void check_doit() {}

namespace {
template <bool Noexcept>
struct MayThrow {
  FOLLY_NOINLINE MayThrow() noexcept(Noexcept) { check_doit(); }
  FOLLY_NOINLINE ~MayThrow() { check_doit(); }
};
} // namespace

extern "C" FOLLY_KEEP int* check() {
  return &createGlobal<int, void>();
}

extern "C" FOLLY_KEEP void* check_throw() {
  return &createGlobal<MayThrow<false>, void>();
}

extern "C" FOLLY_KEEP void* check_nothrow() {
  return &createGlobal<MayThrow<true>, void>();
}

template <typename Impl>
struct StaticSingletonManagerTest : public testing::TestWithParam<Impl> {};
TYPED_TEST_SUITE_P(StaticSingletonManagerTest);

template <typename T>
struct Tag {};

template <int I>
using Int = std::integral_constant<int, I>;

TYPED_TEST_P(StaticSingletonManagerTest, example) {
  using K = TypeParam;

  using T = std::integral_constant<int, 3>;

  auto& i = K::template create<T, Tag<char>>();
  EXPECT_EQ(T::value, i);

  auto& j = K::template create<T, Tag<char>>();
  EXPECT_EQ(&i, &j);
  EXPECT_EQ(T::value, j);

  auto& k = K::template create<T, Tag<char*>>();
  EXPECT_NE(&i, &k);
  EXPECT_EQ(T::value, k);

  static typename K::template ArgCreate<true> m_arg{tag<T, Tag<int>>};
  auto& m = K::template create<T>(m_arg);
  EXPECT_NE(&i, &m);
  EXPECT_EQ(T::value, m);
}

REGISTER_TYPED_TEST_SUITE_P( //
    StaticSingletonManagerTest,
    example);

INSTANTIATE_TYPED_TEST_SUITE_P(
    sans_rtti, StaticSingletonManagerTest, StaticSingletonManagerSansRtti);
#if FOLLY_HAS_RTTI
INSTANTIATE_TYPED_TEST_SUITE_P(
    with_rtti, StaticSingletonManagerTest, StaticSingletonManagerWithRtti);
#endif
INSTANTIATE_TYPED_TEST_SUITE_P(
    selection, StaticSingletonManagerTest, StaticSingletonManager);

struct CreateGlobalTest : testing::Test {};

TEST_F(CreateGlobalTest, example) {
  using T = std::integral_constant<int, 3>;

  auto& i = createGlobal<T, Tag<char>>();
  EXPECT_EQ(T::value, i);

  auto& j = createGlobal<T, Tag<char>>();
  EXPECT_EQ(&i, &j);
  EXPECT_EQ(T::value, j);

  auto& k = createGlobal<T, Tag<char*>>();
  EXPECT_NE(&i, &k);
  EXPECT_EQ(T::value, k);
}

} // namespace detail
} // namespace folly
