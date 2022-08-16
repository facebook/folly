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

#include <folly/synchronization/RelaxedAtomic.h>

#include <folly/portability/GTest.h>
#include <folly/synchronization/AtomicUtil.h>

// gcc7.5 appears to have trouble with these deductions
#if !__GNUC__ || __GNUC__ >= 8 || __clang__

static_assert( //
    std::is_same_v< //
        int,
        folly::atomic_value_type_t<folly::relaxed_atomic<int>>>);
static_assert( //
    std::is_same_v< //
        bool,
        folly::atomic_value_type_t<folly::relaxed_atomic<bool>>>);
static_assert( //
    std::is_same_v< //
        void*,
        folly::atomic_value_type_t<folly::relaxed_atomic<void*>>>);

class RelaxedAtomicTest : public testing::Test {};

TEST_F(RelaxedAtomicTest, deduce_bool) {
  folly::relaxed_atomic v{false};
  EXPECT_EQ(false, v.load());
  EXPECT_TRUE((std::is_same_v<bool, decltype(v)::value_type>));
}

TEST_F(RelaxedAtomicTest, deduce_int) {
  folly::relaxed_atomic v{0};
  EXPECT_EQ(0, v.load());
  EXPECT_TRUE((std::is_same_v<int, decltype(v)::value_type>));
}

TEST_F(RelaxedAtomicTest, deduce_float) {
  folly::relaxed_atomic v{0.f};
  EXPECT_EQ(0, v.load());
  EXPECT_TRUE((std::is_same_v<float, decltype(v)::value_type>));
}

TEST_F(RelaxedAtomicTest, deduce_ptr) {
  struct foo {};
  foo f;
  folly::relaxed_atomic v{&f};
  EXPECT_EQ(&f, v.load());
  EXPECT_TRUE((std::is_same_v<foo*, decltype(v)::value_type>));
}

#endif

template <typename AtomicType>
struct RelaxedAtomicBooleanTest : testing::Test {};

using RelaxedAtomicBooleanTestTypes = testing::Types<
    std::atomic<bool>,
    std::atomic<bool> volatile,
    folly::relaxed_atomic<bool>,
    folly::relaxed_atomic<bool> volatile>;
TYPED_TEST_SUITE(RelaxedAtomicBooleanTest, RelaxedAtomicBooleanTestTypes);

TYPED_TEST(RelaxedAtomicBooleanTest, is_lock_free) {
  TypeParam v{true};
  EXPECT_TRUE(v.is_lock_free());
}

TYPED_TEST(RelaxedAtomicBooleanTest, operator_value) {
  TypeParam v{true};
  EXPECT_EQ(true, v);
}

TYPED_TEST(RelaxedAtomicBooleanTest, load) {
  TypeParam v{true};
  EXPECT_EQ(true, v.load());
}

TYPED_TEST(RelaxedAtomicBooleanTest, operator_assign) {
  TypeParam v{true};
  EXPECT_EQ(false, (v = false));
  EXPECT_EQ(false, v);
}

TYPED_TEST(RelaxedAtomicBooleanTest, store) {
  TypeParam v{true};
  EXPECT_EQ(false, (v.store(false), v));
}

TYPED_TEST(RelaxedAtomicBooleanTest, exchange) {
  TypeParam v{true};
  EXPECT_EQ(true, v.exchange(false));
  EXPECT_EQ(false, v);
}

TYPED_TEST(RelaxedAtomicBooleanTest, compare_exchange_weak) {
  TypeParam v{true};
  bool e = false;
  EXPECT_FALSE(v.compare_exchange_weak(e, false));
  EXPECT_TRUE(v.compare_exchange_weak(e, false));
  EXPECT_EQ(true, e);
  EXPECT_EQ(false, v);
}

TYPED_TEST(RelaxedAtomicBooleanTest, compare_exchange_strong) {
  TypeParam v{true};
  bool e = false;
  EXPECT_FALSE(v.compare_exchange_strong(e, false));
  EXPECT_TRUE(v.compare_exchange_strong(e, false));
  EXPECT_EQ(true, e);
  EXPECT_EQ(false, v);
}

template <typename AtomicType>
struct RelaxedAtomicPointerTest : testing::Test {};

using RelaxedAtomicPointerTestTypes = testing::Types<
    std::atomic<int*>,
    std::atomic<int*> volatile,
    folly::relaxed_atomic<int*>,
    folly::relaxed_atomic<int*> volatile>;
TYPED_TEST_SUITE(RelaxedAtomicPointerTest, RelaxedAtomicPointerTestTypes);

TYPED_TEST(RelaxedAtomicPointerTest, is_lock_free) {
  int n[] = {-1};
  TypeParam v{n + 0};
  EXPECT_TRUE(v.is_lock_free());
}

TYPED_TEST(RelaxedAtomicPointerTest, operator_value) {
  int n[] = {-1};
  TypeParam v{n + 0};
  EXPECT_EQ(-1, *v);
}

TYPED_TEST(RelaxedAtomicPointerTest, load) {
  int n[] = {-1};
  TypeParam v{n + 0};
  EXPECT_EQ(-1, *v.load());
}

TYPED_TEST(RelaxedAtomicPointerTest, operator_assign) {
  int n[] = {-1, -2, -3};
  TypeParam v{n + 0};
  EXPECT_EQ(-3, *(v = n + 2));
  EXPECT_EQ(-3, *v);
}

TYPED_TEST(RelaxedAtomicPointerTest, store) {
  int n[] = {-1, -2, -3};
  TypeParam v{n + 0};
  EXPECT_EQ(-3, *(v.store(n + 2), v));
}

TYPED_TEST(RelaxedAtomicPointerTest, exchange) {
  int n[] = {-1, -2, -3};
  TypeParam v{n + 0};
  EXPECT_EQ(-1, *v.exchange(n + 2));
  EXPECT_EQ(-3, *v);
}

TYPED_TEST(RelaxedAtomicPointerTest, compare_exchange_weak) {
  int n[] = {-1, -2, -3};
  TypeParam v{n + 0};
  int* e = n + 1;
  EXPECT_FALSE(v.compare_exchange_weak(e, n + 2));
  EXPECT_TRUE(v.compare_exchange_weak(e, n + 2));
  EXPECT_EQ(-1, *e);
  EXPECT_EQ(-3, *v);
}

TYPED_TEST(RelaxedAtomicPointerTest, compare_exchange_strong) {
  int n[] = {-1, -2, -3};
  TypeParam v{n + 0};
  int* e = n + 1;
  EXPECT_FALSE(v.compare_exchange_strong(e, n + 2));
  EXPECT_TRUE(v.compare_exchange_strong(e, n + 2));
  EXPECT_EQ(-1, *e);
  EXPECT_EQ(-3, *v);
}

TYPED_TEST(RelaxedAtomicPointerTest, fetch_add) {
  int n[] = {-1, -2, -3};
  TypeParam v{n + 0};
  EXPECT_EQ(-1, *v.fetch_add(2));
  EXPECT_EQ(-3, *v);
}

TYPED_TEST(RelaxedAtomicPointerTest, fetch_sub) {
  int n[] = {-1, -2, -3};
  TypeParam v{n + 2};
  EXPECT_EQ(-3, *v.fetch_sub(2));
  EXPECT_EQ(-1, *v);
}

TYPED_TEST(RelaxedAtomicPointerTest, operator_incr_pre) {
  int n[] = {-1, -2, -3};
  TypeParam v{n + 1};
  EXPECT_EQ(-3, *++v);
  EXPECT_EQ(-3, *v);
}

TYPED_TEST(RelaxedAtomicPointerTest, operator_incr_post) {
  int n[] = {-1, -2, -3};
  TypeParam v{n + 1};
  EXPECT_EQ(-2, *v++);
  EXPECT_EQ(-3, *v);
}

TYPED_TEST(RelaxedAtomicPointerTest, operator_decr_pre) {
  int n[] = {-1, -2, -3};
  TypeParam v{n + 1};
  EXPECT_EQ(-1, *--v);
  EXPECT_EQ(-1, *v);
}

TYPED_TEST(RelaxedAtomicPointerTest, operator_decr_post) {
  int n[] = {-1, -2, -3};
  TypeParam v{n + 1};
  EXPECT_EQ(-2, *v--);
  EXPECT_EQ(-1, *v);
}

TYPED_TEST(RelaxedAtomicPointerTest, operator_add_assign) {
  int n[] = {-1, -2, -3};
  TypeParam v{n + 0};
  EXPECT_EQ(-3, *(v += 2));
  EXPECT_EQ(-3, *v);
}

TYPED_TEST(RelaxedAtomicPointerTest, operator_sub_assign) {
  int n[] = {-1, -2, -3};
  TypeParam v{n + 2};
  EXPECT_EQ(-1, *(v -= 2));
  EXPECT_EQ(-1, *v);
}

template <typename AtomicType>
struct RelaxedAtomicIntegralTest : testing::Test {};

using RelaxedAtomicIntegralTestTypes = testing::Types<
    std::atomic<int>,
    std::atomic<int> volatile,
    folly::relaxed_atomic<int>,
    folly::relaxed_atomic<int> volatile>;
TYPED_TEST_SUITE(RelaxedAtomicIntegralTest, RelaxedAtomicIntegralTestTypes);

TYPED_TEST(RelaxedAtomicIntegralTest, is_lock_free) {
  TypeParam v{3};
  EXPECT_TRUE(v.is_lock_free());
}

TYPED_TEST(RelaxedAtomicIntegralTest, operator_value) {
  TypeParam v{3};
  EXPECT_EQ(3, v);
}

TYPED_TEST(RelaxedAtomicIntegralTest, load) {
  TypeParam v{3};
  EXPECT_EQ(3, v.load());
}

TYPED_TEST(RelaxedAtomicIntegralTest, operator_assign) {
  TypeParam v{3};
  EXPECT_EQ(4, (v = 4));
  EXPECT_EQ(4, v);
}

TYPED_TEST(RelaxedAtomicIntegralTest, store) {
  TypeParam v{3};
  EXPECT_EQ(4, (v.store(4), v));
}

TYPED_TEST(RelaxedAtomicIntegralTest, exchange) {
  TypeParam v{3};
  EXPECT_EQ(3, v.exchange(4));
  EXPECT_EQ(4, v);
}

TYPED_TEST(RelaxedAtomicIntegralTest, compare_exchange_weak) {
  TypeParam v{3};
  int e = 2;
  EXPECT_FALSE(v.compare_exchange_weak(e, 4));
  EXPECT_TRUE(v.compare_exchange_weak(e, 4));
  EXPECT_EQ(3, e);
  EXPECT_EQ(4, v);
}

TYPED_TEST(RelaxedAtomicIntegralTest, compare_exchange_strong) {
  TypeParam v{3};
  int e = 2;
  EXPECT_FALSE(v.compare_exchange_strong(e, 4));
  EXPECT_TRUE(v.compare_exchange_strong(e, 4));
  EXPECT_EQ(3, e);
  EXPECT_EQ(4, v);
}

TYPED_TEST(RelaxedAtomicIntegralTest, fetch_add) {
  TypeParam v{3};
  EXPECT_EQ(3, v.fetch_add(2));
  EXPECT_EQ(5, v);
}

TYPED_TEST(RelaxedAtomicIntegralTest, fetch_sub) {
  TypeParam v{3};
  EXPECT_EQ(3, v.fetch_sub(2));
  EXPECT_EQ(1, v);
}

TYPED_TEST(RelaxedAtomicIntegralTest, fetch_and) {
  TypeParam v{6};
  EXPECT_EQ(6, v.fetch_and(12));
  EXPECT_EQ(4, v);
}

TYPED_TEST(RelaxedAtomicIntegralTest, fetch_or) {
  TypeParam v{5};
  EXPECT_EQ(5, v.fetch_or(14));
  EXPECT_EQ(15, v);
}

TYPED_TEST(RelaxedAtomicIntegralTest, fetch_xor) {
  TypeParam v{6};
  EXPECT_EQ(6, v.fetch_xor(10));
  EXPECT_EQ(12, v);
}

TYPED_TEST(RelaxedAtomicIntegralTest, operator_incr_pre) {
  TypeParam v{3};
  EXPECT_EQ(4, ++v);
  EXPECT_EQ(4, v);
}

TYPED_TEST(RelaxedAtomicIntegralTest, operator_incr_post) {
  TypeParam v{3};
  EXPECT_EQ(3, v++);
  EXPECT_EQ(4, v);
}

TYPED_TEST(RelaxedAtomicIntegralTest, operator_decr_pre) {
  TypeParam v{3};
  EXPECT_EQ(2, --v);
  EXPECT_EQ(2, v);
}

TYPED_TEST(RelaxedAtomicIntegralTest, operator_decr_post) {
  TypeParam v{3};
  EXPECT_EQ(3, v--);
  EXPECT_EQ(2, v);
}

TYPED_TEST(RelaxedAtomicIntegralTest, operator_add_assign) {
  TypeParam v{3};
  EXPECT_EQ(5, v += 2);
  EXPECT_EQ(5, v);
}

TYPED_TEST(RelaxedAtomicIntegralTest, operator_sub_assign) {
  TypeParam v{3};
  EXPECT_EQ(1, v -= 2);
  EXPECT_EQ(1, v);
}

TYPED_TEST(RelaxedAtomicIntegralTest, operator_and_assign) {
  TypeParam v{6};
  EXPECT_EQ(4, v &= 12);
  EXPECT_EQ(4, v);
}

TYPED_TEST(RelaxedAtomicIntegralTest, operator_or_assign) {
  TypeParam v{5};
  EXPECT_EQ(15, v |= 14);
  EXPECT_EQ(15, v);
}

TYPED_TEST(RelaxedAtomicIntegralTest, operator_xor_assign) {
  TypeParam v{6};
  EXPECT_EQ(12, v ^= 10);
  EXPECT_EQ(12, v);
}
