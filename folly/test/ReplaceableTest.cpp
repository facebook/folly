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

#include <folly/Replaceable.h>

#include <folly/portability/GTest.h>

using namespace ::testing;
using namespace ::folly;

namespace {
struct Basic {};
struct alignas(128) BigAlign {};
struct HasConst final {
  bool const b1;
  HasConst() noexcept : b1(true) {}
  explicit HasConst(bool b) noexcept : b1(b) {}
  HasConst(HasConst const& b) noexcept : b1(b.b1) {}
  HasConst(HasConst&& b) noexcept : b1(b.b1) {}
  HasConst& operator=(HasConst const&) = delete;
  HasConst& operator=(HasConst&&) = delete;
};
struct HasRef final {
  int& i1;
  explicit HasRef(int& i) noexcept(false) : i1(i) {}
  HasRef(HasRef const& i) noexcept(false) : i1(i.i1) {}
  HasRef(HasRef&& i) noexcept(false) : i1(i.i1) {}
  HasRef& operator=(HasRef const&) = delete;
  HasRef& operator=(HasRef&&) = delete;
  ~HasRef() noexcept(false) { ++i1; }
};
void swap(HasRef& lhs, HasRef& rhs) noexcept(false) {
  std::swap(lhs.i1, rhs.i1);
}
struct OddA;
struct OddB {
  OddB() = delete;
  OddB(std::initializer_list<int>, int) noexcept(false) {}
  explicit OddB(OddA&&) {}
  explicit OddB(OddA const&) noexcept(false) {}
  OddB(OddB&&) = delete;
  OddB(OddB const&) = delete;
  OddB& operator=(OddB&&) = delete;
  OddB& operator=(OddB const&) = delete;
  ~OddB() = default;
};
struct OddA {
  OddA() = delete;
  explicit OddA(OddB&&) noexcept {}
  explicit OddA(OddB const&) = delete;
  OddA(OddA&&) = delete;
  OddA(OddA const&) = delete;
  OddA& operator=(OddA&&) = delete;
  OddA& operator=(OddA const&) = delete;
  ~OddA() noexcept(false) {}
};
struct Indestructible {
  ~Indestructible() = delete;
};

struct HasInt {
  explicit HasInt(int v) : value{v} {}
  int value{};
};
} // namespace

template <typename T>
struct ReplaceableStaticAttributeTest : Test {};
using StaticAttributeTypes = ::testing::Types<
    char,
    short,
    int,
    long,
    float,
    double,
    char[11],
    Basic,
    BigAlign,
    HasConst,
    HasRef,
    OddA,
    OddB,
    Indestructible>;
TYPED_TEST_SUITE(ReplaceableStaticAttributeTest, StaticAttributeTypes);

template <typename T>
struct ReplaceableStaticAttributePairTest : Test {};
using StaticAttributePairTypes = ::testing::
    Types<std::pair<int, long>, std::pair<OddA, OddB>, std::pair<OddB, OddA>>;
TYPED_TEST_SUITE(ReplaceableStaticAttributePairTest, StaticAttributePairTypes);

TYPED_TEST(ReplaceableStaticAttributeTest, size) {
  EXPECT_EQ(sizeof(TypeParam), sizeof(Replaceable<TypeParam>));
}
TYPED_TEST(ReplaceableStaticAttributeTest, align) {
  EXPECT_EQ(alignof(TypeParam), alignof(Replaceable<TypeParam>));
}
TYPED_TEST(ReplaceableStaticAttributeTest, destructible) {
  EXPECT_EQ(
      std::is_destructible<TypeParam>::value,
      std::is_destructible<Replaceable<TypeParam>>::value);
}
TYPED_TEST(ReplaceableStaticAttributeTest, triviallyDestructible) {
  EXPECT_EQ(
      std::is_trivially_destructible<TypeParam>::value,
      std::is_trivially_destructible<Replaceable<TypeParam>>::value);
}
TYPED_TEST(ReplaceableStaticAttributeTest, defaultConstructible) {
  EXPECT_EQ(
      std::is_default_constructible<TypeParam>::value,
      std::is_default_constructible<Replaceable<TypeParam>>::value);
}
TYPED_TEST(ReplaceableStaticAttributeTest, moveConstructible) {
  EXPECT_EQ(
      std::is_move_constructible<TypeParam>::value,
      std::is_move_constructible<Replaceable<TypeParam>>::value);
}
TYPED_TEST(ReplaceableStaticAttributeTest, copyConstructible) {
  EXPECT_EQ(
      std::is_copy_constructible<TypeParam>::value,
      std::is_copy_constructible<Replaceable<TypeParam>>::value);
}
TYPED_TEST(ReplaceableStaticAttributeTest, moveAssignable) {
  EXPECT_EQ(
      std::is_move_constructible<TypeParam>::value,
      std::is_move_assignable<Replaceable<TypeParam>>::value);
}
TYPED_TEST(ReplaceableStaticAttributeTest, copyAssignable) {
  EXPECT_EQ(
      std::is_copy_constructible<TypeParam>::value,
      std::is_copy_assignable<Replaceable<TypeParam>>::value);
}
TYPED_TEST(ReplaceableStaticAttributeTest, nothrowDestructible) {
  EXPECT_EQ(
      std::is_nothrow_destructible<TypeParam>::value,
      std::is_nothrow_destructible<Replaceable<TypeParam>>::value);
}
TYPED_TEST(ReplaceableStaticAttributeTest, nothrowDefaultConstructible) {
  EXPECT_EQ(
      std::is_nothrow_default_constructible<TypeParam>::value,
      std::is_nothrow_default_constructible<Replaceable<TypeParam>>::value);
}
TYPED_TEST(ReplaceableStaticAttributeTest, nothrowMoveConstructible) {
  EXPECT_EQ(
      std::is_nothrow_move_constructible<TypeParam>::value,
      std::is_nothrow_move_constructible<Replaceable<TypeParam>>::value);
}
TYPED_TEST(ReplaceableStaticAttributeTest, nothrowCopyConstructible) {
  EXPECT_EQ(
      std::is_nothrow_copy_constructible<TypeParam>::value,
      std::is_nothrow_copy_constructible<Replaceable<TypeParam>>::value);
}
TYPED_TEST(ReplaceableStaticAttributeTest, nothrowMoveAssignable) {
  EXPECT_EQ(
      std::is_nothrow_destructible<TypeParam>::value &&
          std::is_nothrow_copy_constructible<TypeParam>::value,
      std::is_nothrow_move_assignable<Replaceable<TypeParam>>::value);
}
TYPED_TEST(ReplaceableStaticAttributeTest, nothrowCopyAssignable) {
  EXPECT_EQ(
      std::is_nothrow_destructible<TypeParam>::value &&
          std::is_nothrow_copy_constructible<TypeParam>::value,
      std::is_nothrow_copy_assignable<Replaceable<TypeParam>>::value);
}
TYPED_TEST(ReplaceableStaticAttributeTest, replaceable) {
  EXPECT_FALSE(is_replaceable<TypeParam>::value);
  EXPECT_TRUE(is_replaceable<Replaceable<TypeParam>>::value);
}

TYPED_TEST(ReplaceableStaticAttributePairTest, copyConstruct) {
  using T = typename TypeParam::first_type;
  using U = typename TypeParam::second_type;
  EXPECT_EQ(
      (std::is_constructible<T, U const&>::value),
      (std::is_constructible<Replaceable<T>, Replaceable<U> const&>::value));
}
TYPED_TEST(ReplaceableStaticAttributePairTest, moveConstruct) {
  using T = typename TypeParam::first_type;
  using U = typename TypeParam::second_type;
  EXPECT_EQ(
      (std::is_constructible<T, U&&>::value),
      (std::is_constructible<Replaceable<T>, Replaceable<U>&&>::value));
}
TYPED_TEST(ReplaceableStaticAttributePairTest, copyAssign) {
  using T = typename TypeParam::first_type;
  using U = typename TypeParam::second_type;
  EXPECT_EQ(
      (std::is_convertible<U, T>::value && std::is_destructible<T>::value &&
       std::is_copy_constructible<T>::value),
      (std::is_assignable<Replaceable<T>, Replaceable<U> const&>::value));
}
TYPED_TEST(ReplaceableStaticAttributePairTest, moveAssign) {
  using T = typename TypeParam::first_type;
  using U = typename TypeParam::second_type;
  EXPECT_EQ(
      (std::is_convertible<U, T>::value && std::is_destructible<T>::value &&
       std::is_move_constructible<T>::value),
      (std::is_assignable<Replaceable<T>, Replaceable<U>&&>::value));
}
TYPED_TEST(ReplaceableStaticAttributePairTest, nothrowCopyConstruct) {
  using T = typename TypeParam::first_type;
  using U = typename TypeParam::second_type;
  EXPECT_EQ(
      (std::is_nothrow_constructible<T, U const&>::value &&
       std::is_nothrow_destructible<T>::value),
      (std::is_nothrow_constructible<Replaceable<T>, Replaceable<U> const&>::
           value));
}
TYPED_TEST(ReplaceableStaticAttributePairTest, nothrowMoveConstruct) {
  using T = typename TypeParam::first_type;
  using U = typename TypeParam::second_type;
  EXPECT_EQ(
      (std::is_nothrow_constructible<T, U&&>::value &&
       std::is_nothrow_destructible<T>::value),
      (std::is_nothrow_constructible<Replaceable<T>, Replaceable<U>&&>::value));
}
TYPED_TEST(ReplaceableStaticAttributePairTest, nothrowCopyAssign) {
  using T = typename TypeParam::first_type;
  using U = typename TypeParam::second_type;
  EXPECT_EQ(
      (std::is_nothrow_constructible<T, U const&>::value &&
       std::is_nothrow_destructible<T>::value),
      (std::is_nothrow_assignable<Replaceable<T>, Replaceable<U> const&>::
           value));
}
TYPED_TEST(ReplaceableStaticAttributePairTest, nothrowMoveAssign) {
  using T = typename TypeParam::first_type;
  using U = typename TypeParam::second_type;
  EXPECT_EQ(
      (std::is_nothrow_constructible<T, U&&>::value &&
       std::is_nothrow_destructible<T>::value),
      (std::is_nothrow_assignable<Replaceable<T>, Replaceable<U>&&>::value));
}

TEST(ReplaceableTest, Basics) {
  auto rHasConstA = make_replaceable<HasConst>();
  auto rHasConstB = make_replaceable<HasConst>(false);
  EXPECT_TRUE(rHasConstA->b1);
  EXPECT_FALSE(rHasConstB->b1);
  rHasConstA = rHasConstB;
  EXPECT_FALSE(rHasConstA->b1);
  EXPECT_FALSE(rHasConstB->b1);
  rHasConstB.emplace(true);
  EXPECT_FALSE(rHasConstA->b1);
  EXPECT_TRUE(rHasConstB->b1);
  rHasConstA = std::move(rHasConstB);
  EXPECT_TRUE(rHasConstA->b1);
  EXPECT_TRUE(rHasConstB->b1);
}

TEST(ReplaceableTest, Constructors) {
  Basic b{};
  // From existing `T`
  auto rBasicCopy1 = Replaceable<Basic>(b);
  auto rBasicMove1 = Replaceable<Basic>(std::move(b));
  // From existing `Replaceable<T>`
  auto rBasicCopy2 = Replaceable<Basic>(rBasicCopy1);
  auto rBasicMove2 = Replaceable<Basic>(std::move(rBasicMove1));
  (void)rBasicCopy2;
  (void)rBasicMove2;
}

TEST(ReplaceableTest, DestructsWhenExpected) {
  int i{0};
  {
    Replaceable<HasRef> rHasRefA{i};
    Replaceable<HasRef> rHasRefB{i};
    EXPECT_EQ(0, i);
    rHasRefA = rHasRefB;
    EXPECT_EQ(1, i);
    rHasRefB.emplace(i);
    EXPECT_EQ(2, i);
    rHasRefA = std::move(rHasRefB);
    EXPECT_EQ(3, i);
  }
  EXPECT_EQ(5, i);
}

TEST(ReplaceableTest, Conversions) {
  Replaceable<OddB> rOddB{in_place, {1, 2, 3}, 4};
  Replaceable<OddA> rOddA{std::move(rOddB)};
  Replaceable<OddB> rOddB2{rOddA};
}

TEST(ReplaceableTest, swapMemberFunctionIsNoexcept) {
  int v1{1};
  int v2{2};
  auto r1 = Replaceable<HasInt>{v1};
  auto r2 = Replaceable<HasInt>{v2};
  EXPECT_TRUE(noexcept(r1.swap(r2)));
  r1.swap(r2);
  EXPECT_EQ(v2, r1->value);
  EXPECT_EQ(v1, r2->value);
}

TEST(ReplaceableTest, swapMemberFunctionIsNotNoexcept) {
  int v1{1};
  int v2{2};
  auto r1 = Replaceable<HasRef>{v1};
  auto r2 = Replaceable<HasRef>{v2};
  EXPECT_FALSE(noexcept(r1.swap(r2)));
  r1.swap(r2);
  EXPECT_EQ(v1, r1->i1);
  EXPECT_EQ(v2, r2->i1);
}

namespace adl_test {
struct UserDefinedSwap {
  bool calledSwap{};
};
void swap(UserDefinedSwap& lhs, UserDefinedSwap&) noexcept(false) {
  lhs.calledSwap = true;
}
} // namespace adl_test

TEST(ReplaceableTest, swapMemberFunctionDelegatesToUserSwap) {
  auto r1 = Replaceable<adl_test::UserDefinedSwap>{};
  auto r2 = Replaceable<adl_test::UserDefinedSwap>{};
  r1.swap(r2);
  EXPECT_TRUE(r1->calledSwap);
}

#if __cpp_deduction_guides >= 201703
TEST(ReplaceableTest, DeductionGuide) {
  Basic b{};
  Replaceable r{b};
  EXPECT_TRUE((std::is_same_v<Replaceable<Basic>, decltype(r)>));
}
#endif
