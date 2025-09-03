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

#include <folly/Traits.h>
#include <folly/lang/MustUseImmediately.h>

#include <folly/portability/GTest.h>

// READ before you copy-paste `unsafe_mover` implementations from below:
//
// Most of the implementations use concrete types in `unsafe_mover` and the
// uncurry ctor. This is not necessarily the right choice for you:
//   - It is fine if the type is final (like `CompositeImmediate`).
//   - If you have derived classes, concrete types will force any derived class
//     to provide its own `unsafe_mover` + uncurry ctor.
//   - Alternatively, you can let derived classes to reuse your curry/uncurry
//     implementation -- but be careful of object slicing.  See
//     `DerivedAddsMember` for an example.

using namespace folly::ext;

// Define a must-use-immediately type using a CRTP mixin
struct CustomImmediate : must_use_immediately_crtp<CustomImmediate> {
  int value_ = 42;

  explicit CustomImmediate(int v) : value_{v} {}

  // When a must-use-immediately type cannot use `wrap_must_use_immediately_t`,
  // it usually has to provide an `unsafe_mover` impl.  The most typical
  // pattern is `curried_unsafe_mover_from_bases_and_members`.
  static auto unsafe_mover(
      must_use_immediately_private_t, CustomImmediate&& me) noexcept {
    return curried_unsafe_mover_from_bases_and_members<CustomImmediate>(
        folly::tag</*no bases*/>,
        folly::vtag<&CustomImmediate::value_>,
        std::move(me));
  }
  // `noexcept` per the `unsafe_mover` contract
  CustomImmediate(
      curried_unsafe_mover_private_t,
      curried_unsafe_mover_t<CustomImmediate, default_unsafe_mover_t<int>>&&
          mover) noexcept
      : value_{std::move(mover).get<0>()()} {}
};

// Wrap an existing type to make an immediate one
struct Regular {
  int value_;
  explicit Regular(int v) : value_(v) {}
  // Needed since `DerivedAddsMember` assumes its base has such a ctor.
  explicit Regular(
      curried_unsafe_mover_private_t,
      default_unsafe_mover_t<Regular>&& m) noexcept
      : Regular{std::move(m)()} {}
};
// Gets an automatic `unsafe_mover` using the `Regular` move ctor.
using WrappedImmediate = wrap_must_use_immediately_t<Regular>;

struct CompositeImmediate final // why `final`? see top-of-file doc
    : must_use_immediately_crtp<CompositeImmediate> {
  CustomImmediate custom_;
  WrappedImmediate wrapped_;

  // Composing must-use-immediately prvalues requires unsafe movers
  explicit CompositeImmediate(CustomImmediate a, WrappedImmediate b)
      : custom_{must_use_immediately_unsafe_mover(std::move(a))()},
        wrapped_{must_use_immediately_unsafe_mover(std::move(b))()} {}

  using test_unsafe_mover_t = curried_unsafe_mover_t<
      CompositeImmediate,
      curried_unsafe_mover_t<CustomImmediate, default_unsafe_mover_t<int>>,
      curried_unsafe_mover_t<
          WrappedImmediate,
          default_unsafe_mover_t<Regular>>>;

  static auto unsafe_mover(
      must_use_immediately_private_t, CompositeImmediate&& me) noexcept {
    return curried_unsafe_mover_from_bases_and_members<CompositeImmediate>(
        folly::tag</*no bases*/>,
        folly::vtag< //
            &CompositeImmediate::custom_,
            &CompositeImmediate::wrapped_>,
        std::move(me));
  }
  explicit CompositeImmediate(
      curried_unsafe_mover_private_t, test_unsafe_mover_t&& mover) noexcept
      : custom_{std::move(mover.template get<0>())()},
        wrapped_{std::move(mover.template get<1>())()} {}
};

constexpr bool static_type_checks() {
  // Test must_use_immediately trait detection
  static_assert(!must_use_immediately_v<void>);
  static_assert(must_use_immediately_v<CustomImmediate>);
  static_assert(must_use_immediately_v<WrappedImmediate>);
  static_assert(must_use_immediately_v<CompositeImmediate>);
  static_assert(!must_use_immediately_v<Regular>);
  static_assert(!must_use_immediately_v<int>);

  // Test immovability
  static_assert(!std::is_move_constructible_v<CustomImmediate>);
  static_assert(!std::is_copy_constructible_v<CustomImmediate>);
  static_assert(!std::is_move_assignable_v<CustomImmediate>);
  static_assert(!std::is_copy_assignable_v<CustomImmediate>);
  static_assert(!std::is_move_constructible_v<WrappedImmediate>);
  static_assert(!std::is_copy_constructible_v<WrappedImmediate>);
  static_assert(!std::is_move_assignable_v<WrappedImmediate>);
  static_assert(!std::is_copy_assignable_v<WrappedImmediate>);

  // The original type is movable, but the wrapped type is not
  static_assert(std::is_move_constructible_v<Regular>);
  static_assert(!std::is_move_constructible_v<WrappedImmediate>);

  // Test empty base optimization -- neither the wrapper nor the crtp
  // mixin should increase the class size.
  static_assert(sizeof(CustomImmediate) == sizeof(int));
  static_assert(sizeof(WrappedImmediate) == sizeof(Regular));
  static_assert(
      sizeof(CompositeImmediate) ==
      sizeof(CustomImmediate) + sizeof(WrappedImmediate));

  return true;
}
static_assert(static_type_checks());

TEST(MustUseImmediatelyTest, BasicUsage) {
  CustomImmediate custom{42};
  EXPECT_EQ(custom.value_, 42);

  WrappedImmediate wrapped{789};
  EXPECT_EQ(wrapped.value_, 789);
}

CompositeImmediate make_composite() {
  // This is a function to show that `CompositeImmediate` is not taking rvalue
  // refs to the ephemeral sub-objects, but actually moves them in.
  return CompositeImmediate{CustomImmediate{42}, WrappedImmediate{456}};
}

TEST(MustUseImmediatelyTest, CompositionExample) {
  CompositeImmediate composite = make_composite();

  EXPECT_EQ(composite.custom_.value_, 42);
  EXPECT_EQ(composite.wrapped_.value_, 456);

  // Equivalent to `check_unsafe_mover_round_trip` --

  static_assert(
      noexcept(must_use_immediately_unsafe_mover(std::move(composite))));

  auto mover = must_use_immediately_unsafe_mover(std::move(composite));
  static_assert(noexcept(std::move(mover)()));
  static_assert(std::is_nothrow_move_constructible_v<decltype(mover)>);
  static_assert(
      std::is_same_v<
          decltype(mover),
          typename CompositeImmediate::test_unsafe_mover_t>);

  auto reconstructed = std::move(mover)();
  EXPECT_EQ(reconstructed.custom_.value_, 42);
  EXPECT_EQ(reconstructed.wrapped_.value_, 456);
}

template <typename T, typename ExpectedMover>
void check_unsafe_mover_round_trip() {
  T t{999};
  static_assert(noexcept(must_use_immediately_unsafe_mover(std::move(t))));

  auto mover = must_use_immediately_unsafe_mover(std::move(t));
  static_assert(noexcept(std::move(mover)()));
  static_assert(std::is_nothrow_move_constructible_v<decltype(mover)>);
  static_assert(std::is_same_v<decltype(mover), ExpectedMover>);

  auto reconstructed = std::move(mover)(); // `auto` to test for implicit conv
  static_assert(std::is_same_v<T, decltype(reconstructed)>); // no conversion
  EXPECT_EQ(reconstructed.value_, 999);
}

TEST(MustUseImmediatelyTest, CustomUnsafeMoverRoundTrip) {
  check_unsafe_mover_round_trip<
      CustomImmediate,
      curried_unsafe_mover_t<CustomImmediate, default_unsafe_mover_t<int>>>();
}

TEST(MustUseImmediatelyTest, WrappedUnsafeMoverRoundTrip) {
  check_unsafe_mover_round_trip<
      WrappedImmediate,
      curried_unsafe_mover_t<
          WrappedImmediate,
          default_unsafe_mover_t<Regular>>>();
}

TEST(MustUseImmediatelyTest, RegularUnsafeMoverRoundTrip) {
  check_unsafe_mover_round_trip<Regular, default_unsafe_mover_t<Regular>>();
}

// Demo of how to extend `unsafe_mover` when the derive class cannot be sliced.
template <typename Base>
struct DerivedAddsMember : Base {
  int doNotSliceOff_;

  DerivedAddsMember(int v, int n) : Base{v}, doNotSliceOff_{n} {}

  using test_base_mover_t =
      decltype(must_use_immediately_unsafe_mover(FOLLY_DECLVAL(Base&&)));
  template <std::derived_from<DerivedAddsMember> T>
  using test_unsafe_mover_t =
      curried_unsafe_mover_t<T, test_base_mover_t, default_unsafe_mover_t<int>>;

  static auto unsafe_mover(
      must_use_immediately_private_t,
      // Allow `DoubleDerived` to reuse our `unsafe_mover`
      std::derived_from<DerivedAddsMember> auto&& me) noexcept {
    return curried_unsafe_mover_from_bases_and_members<DerivedAddsMember>(
        folly::tag<Base>,
        folly::vtag<&DerivedAddsMember::doNotSliceOff_>,
        // `me` is always an rval, so this is just `std::move` (but w/o lints)
        static_cast<decltype(me)>(me));
  }
  template <std::derived_from<DerivedAddsMember> T>
  DerivedAddsMember(
      curried_unsafe_mover_private_t priv,
      // Allow `DoubleDerived` to reuse this uncurry ctor
      test_unsafe_mover_t<T>&& mover) noexcept
      : Base{priv, std::move(mover.template get<0>())},
        doNotSliceOff_{std::move(mover.template get<1>())()} {}
};

template <template <typename> class DerivedT, typename Base>
void check_nontrivial_derived_class() {
  using Derived = DerivedT<Base>;

  if constexpr (std::is_same_v<Base, Regular>) {
    static_assert(!must_use_immediately_v<Derived>);
    static_assert(std::is_move_constructible_v<Derived>);
  } else {
    static_assert(must_use_immediately_v<Derived>);
    static_assert(!std::is_move_constructible_v<Derived>);
  }
  static_assert(sizeof(Derived) > sizeof(Base));

  Derived derived{999, 42};

  static_assert(
      noexcept(must_use_immediately_unsafe_mover(std::move(derived))));

  auto mover = must_use_immediately_unsafe_mover(std::move(derived));
  static_assert(std::is_nothrow_move_constructible_v<decltype(mover)>);
  static_assert(noexcept(std::move(mover)()));
  static_assert(
      std::is_same_v<
          decltype(mover),
          typename Derived::template test_unsafe_mover_t<Derived>>);

  Derived reconstructed = std::move(mover)();
  EXPECT_EQ(reconstructed.value_, 999);
  EXPECT_EQ(reconstructed.doNotSliceOff_, 42);
}

TEST(MustUseImmediatelyTest, NontrivialDerivedFromWrapped) {
  check_nontrivial_derived_class<DerivedAddsMember, WrappedImmediate>();
}

TEST(MustUseImmediatelyTest, NontrivialDerivedFromCustom) {
  check_nontrivial_derived_class<DerivedAddsMember, CustomImmediate>();
}

TEST(MustUseImmediatelyTest, NontrivialDerivedFromRegular) {
  check_nontrivial_derived_class<DerivedAddsMember, Regular>();
}

// Make sure the unsafe-mover machinery is correctly inherited when a derived
// class is a simple type wrapper around a must-use-immediately base.
template <typename Base>
struct DoubleDerived : DerivedAddsMember<Base> {
  using DerivedAddsMember<Base>::DerivedAddsMember;
};

TEST(MustUseImmediatelyTest, DoubleDerivedFromWrapped) {
  check_nontrivial_derived_class<DoubleDerived, WrappedImmediate>();
}

TEST(MustUseImmediatelyTest, DoubleDerivedFromCustom) {
  check_nontrivial_derived_class<DoubleDerived, CustomImmediate>();
}

TEST(MustUseImmediatelyTest, DoubleDerivedFromRegular) {
  check_nontrivial_derived_class<DoubleDerived, Regular>();
}
