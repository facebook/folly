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

#include <folly/lang/Strong.h>

#include <sstream>
#include <folly/hash/traits.h>
#include <folly/portability/GTest.h>

namespace folly {

// Used to assert that NO comparisons work with an incompatible type.
template <typename T, typename U>
concept any_comparable_with = requires(T t, U u) {
  { t == u } -> std::convertible_to<bool>;
} || requires(T t, U u) {
  { t != u } -> std::convertible_to<bool>;
} || requires(T t, U u) {
  { t < u } -> std::convertible_to<bool>;
} || requires(T t, U u) {
  { t > u } -> std::convertible_to<bool>;
} || requires(T t, U u) {
  { t <= u } -> std::convertible_to<bool>;
} || requires(T t, U u) {
  { t >= u } -> std::convertible_to<bool>;
} || requires(T t, U u) {
  { t <=> u };
} || requires(T t, U u) {
  { u == t } -> std::convertible_to<bool>;
} || requires(T t, U u) {
  { u != t } -> std::convertible_to<bool>;
} || requires(T t, U u) {
  { u < t } -> std::convertible_to<bool>;
} || requires(T t, U u) {
  { u > t } -> std::convertible_to<bool>;
} || requires(T t, U u) {
  { u <= t } -> std::convertible_to<bool>;
} || requires(T t, U u) {
  { u >= t } -> std::convertible_to<bool>;
} || requires(T t, U u) {
  { u <=> t };
};

constexpr void test(bool cond) {
  if (!cond) {
    throw std::exception{};
  }
}

struct StrongInt : public strong_derefable<int, StrongInt> {
  using strong_derefable<int, StrongInt>::strong_derefable;
  /* implicit */ constexpr StrongInt(int t) : strong_derefable{t} {}
};

constexpr bool test_is_hashable() {
  struct NonHashable {};
  struct StrongNonHashable : public strong<NonHashable, StrongNonHashable> {
    using strong<NonHashable, StrongNonHashable>::strong;
  };

  // strong inherits hashability from its underlying type.
  static_assert(folly::is_hashable_v<StrongInt>);
  static_assert(!folly::is_hashable_v<StrongNonHashable>);
  return true;
}
static_assert(test_is_hashable());

constexpr bool test_access_underlying() {
  StrongInt id{42};

  // Explicit conversions
  test(int{id} == 42);
  test(static_cast<int>(id) == 42);

  // Lvalue accessors
  test(*id == 42);
  test(*std::as_const(id) == 42);
  test(id.value() == 42);
  test(std::as_const(id).value() == 42);

  // Rvalue accessor
  auto val = *std::move(id);
  test(val == 42);

  return true;
}
static_assert(test_access_underlying());

constexpr bool test_arrow_operator() {
  struct Point {
    int x;
    int y;
    constexpr int sum() const { return x + y; }
  };
  struct StrongPoint : public strong_derefable<Point, StrongPoint> {
    using strong_derefable<Point, StrongPoint>::strong_derefable;
  };

  StrongPoint p{Point{3, 4}};
  test(p->x == 3);
  test(p->y == 4);
  test(p->sum() == 7);

  const StrongPoint cp{Point{5, 6}};
  test(cp->x == 5);
  test(cp->y == 6);
  test(cp->sum() == 11);

  return true;
}
static_assert(test_arrow_operator());

constexpr bool test_construct() { // Also see runtime `TEST()` analog
  { // Default construction
    StrongInt id;
    test(id.value() == 0);
  }
  { // From underlying type
    StrongInt id{99};
    test(*id == 99);

    // Copy/move ctor
    StrongInt id2{id};
    test(*id2 == 99);
    StrongInt id3{std::move(id2)};
    test(*id3 == 99);
  }
  { // Implicit ctor from underlying type
    auto id = []() -> StrongInt { return {123}; }();
    test(*id == 123);
  }
  return true;
}
static_assert(test_construct());

constexpr bool test_construct_without_implicit_ctor() {
  struct ExplicitInt : public strong_derefable<int, ExplicitInt> {
    using strong_derefable<int, ExplicitInt>::strong_derefable;
  };

  static_assert(!std::is_convertible_v<int, ExplicitInt>);
  static_assert(std::is_constructible_v<ExplicitInt, int>);

  ExplicitInt id{42};
  test(*id == 42);

  return true;
}
static_assert(test_construct_without_implicit_ctor());

// This test is minimal, since the other tests tests fully cover the underlying
// `strong` functionality while testing `strong_derefable`.
constexpr bool test_non_derefable() {
  struct PlainInt : public strong<int, PlainInt> {
    using strong<int, PlainInt>::strong;
  };

  PlainInt id1{42};
  test(id1.value() == 42);

  PlainInt id2{10};
  test(id2 < id1);

  id2 = id1;
  test(id2 == id1);

  return true;
}
static_assert(test_non_derefable());

constexpr bool test_comparison() { // Also see runtime `TEST()` analog
  // Comparisons between different types are not allowed
  struct OtherStrongInt : public strong<int, OtherStrongInt> {
    using strong<int, OtherStrongInt>::strong;
  };
  static_assert(!any_comparable_with<StrongInt, OtherStrongInt>);
  static_assert(std::three_way_comparable_with<StrongInt, StrongInt>);

  // Comparisons with the underlying type are allowed
  static_assert(std::three_way_comparable_with<StrongInt, int>);

  // Compare-with-underlying rejects types convertible-to-underlying.
  static_assert(std::three_way_comparable_with<int, double>);
  static_assert(!any_comparable_with<StrongInt, double>);
  struct ConvertsToInt {
    operator int() const { return 5; }
  };
  static_assert(!any_comparable_with<StrongInt, ConvertsToInt>);

  StrongInt id1{10};
  StrongInt id2{20};
  test(id1 == 10);
  test(id1 < 20);
  test(10 < id2);
  // Comparisons within the same type
  test(id1 == id1);
  test(id1 < id2);
  return true;
}
static_assert(test_comparison());

constexpr bool test_move_only() {
  struct MoveOnlyInt {
    int value_;
    constexpr explicit MoveOnlyInt(int v) : value_{v} {}

    MoveOnlyInt(const MoveOnlyInt&) = delete;
    MoveOnlyInt& operator=(const MoveOnlyInt&) = delete;

    constexpr MoveOnlyInt(MoveOnlyInt&& other) noexcept
        : value_{std::exchange(other.value_, -1)} {}
    constexpr MoveOnlyInt& operator=(MoveOnlyInt&& other) noexcept {
      value_ = std::exchange(other.value_, -1);
      return *this;
    }

    constexpr auto operator<=>(const MoveOnlyInt&) const = default;
  };
  static_assert(!std::is_copy_constructible_v<MoveOnlyInt>);
  static_assert(!std::is_copy_assignable_v<MoveOnlyInt>);
  static_assert(std::is_move_constructible_v<MoveOnlyInt>);
  static_assert(std::is_move_assignable_v<MoveOnlyInt>);

  struct StrongMoveOnlyInt
      : public strong_derefable<MoveOnlyInt, StrongMoveOnlyInt> {
    using strong_derefable<MoveOnlyInt, StrongMoveOnlyInt>::strong_derefable;
  };
  static_assert(!std::is_copy_constructible_v<StrongMoveOnlyInt>);
  static_assert(!std::is_copy_assignable_v<StrongMoveOnlyInt>);
  static_assert(std::is_move_constructible_v<StrongMoveOnlyInt>);
  static_assert(std::is_move_assignable_v<StrongMoveOnlyInt>);

  StrongMoveOnlyInt id1{MoveOnlyInt{42}};
  test(id1.value().value_ == 42);

  StrongMoveOnlyInt id2{MoveOnlyInt{10}};
  test(id2.value().value_ == 10);

  // Move assignment
  id2 = std::move(id1);
  test(id2.value().value_ == 42);
  // NOLINTNEXTLINE(bugprone-use-after-move)
  test(id1.value().value_ == -1); // moved-out

  // Move ctor
  StrongMoveOnlyInt id3{std::move(id2)};
  test(id3.value().value_ == 42);
  // NOLINTNEXTLINE(bugprone-use-after-move)
  test(id2.value().value_ == -1); // moved-out

  // Move via rvalue accessor
  MoveOnlyInt val = *std::move(id3);
  test(val.value_ == 42);
  // NOLINTNEXTLINE(bugprone-use-after-move)
  test(id3.value().value_ == -1);

  return true;
}
static_assert(test_move_only());

struct StrongStrA : public strong_derefable<std::string, StrongStrA> {
  using strong_derefable<std::string, StrongStrA>::strong_derefable;
  /* implicit */ StrongStrA(std::string s) : strong_derefable{std::move(s)} {}
};

struct StrongStrB : public strong_derefable<std::string, StrongStrB> {
  using strong_derefable<std::string, StrongStrB>::strong_derefable;
  /* implicit */ StrongStrB(std::string s) : strong_derefable{std::move(s)} {}
};

TEST(StrongTypeTest, construct) {
  // Construction of `std::string` from `std::string_view` is explicit.
  // `StrongType` should not make it implicit.
  static_assert(std::is_constructible_v<std::string, std::string_view>);
  static_assert(!std::is_convertible_v<std::string_view, std::string>);
  static_assert(std::is_constructible_v<StrongStrA, std::string_view>);
  static_assert(!std::is_convertible_v<std::string_view, StrongStrA>);

  { // Default construction
    StrongStrA id;
    EXPECT_TRUE(id.value().empty());
  }
  { // Implicit conversion "id" -> std::string + from-string ctor of StrongStrA
    StrongStrA id{"id"};
    EXPECT_EQ(*id, "id");
  }
  { // Forward ctor args to underlying type
    StrongStrA id{std::string_view{"id"}};
    EXPECT_EQ(*id, "id");
  }
  { // Implicit ctor from underlying type
    // Contrast: `return "id";` would not compile
    auto id = []() -> StrongStrA { return {std::string{"id"}}; }();
    EXPECT_EQ(*id, "id");
  }
}

TEST(StrongTest, useAsMapKeysAndValues) {
  { // Sorted maps via comparators
    std::map<StrongStrA, StrongStrB> map;
    map[StrongStrA{"key"}] = StrongStrB{"val"};
    EXPECT_EQ(map[StrongStrA{"key"}], StrongStrB{"val"});
  }
  { // Hashmaps
    std::unordered_map<StrongStrA, StrongStrB> map;
    map[StrongStrA{"key"}] = StrongStrB{"val"};
    EXPECT_EQ(map[StrongStrA{"key"}], StrongStrB{"val"});
  }
}

// Test both vanilla & gtest comparisons since their behaviors are subtly
// different wrt conversions. We need both to work.
TEST(StrongTest, comparison) {
  // Comparisons between different types are not allowed
  static_assert(std::three_way_comparable_with<StrongStrB, StrongStrB>);
  static_assert(!any_comparable_with<StrongStrA, StrongStrB>);

  StrongStrA id1{"id1"};
  StrongStrA id2{"id2"};

  // Comparisons with the underlying type -- the compiler **should** handle
  // reversals for us, but let's test to be sure.
  static_assert(std::three_way_comparable_with<StrongStrA, std::string>);
  // But, not comparable with types that implicitly convert to underlying.
  static_assert(!any_comparable_with<StrongStrA, const char*>);
  EXPECT_TRUE(id1 == std::string{"id1"});
  EXPECT_EQ(id1, std::string{"id1"});
  EXPECT_TRUE(id1 < std::string{"id2"});
  EXPECT_LT(id1, std::string{"id2"});
  EXPECT_TRUE(std::string{"id1"} < id2);
  EXPECT_LT(std::string{"id1"}, id2);

  // Comparisons within the same type
  static_assert(std::three_way_comparable_with<StrongStrA, StrongStrA>);
  EXPECT_TRUE(id1 == id1);
  EXPECT_EQ(id1, id1);
  EXPECT_TRUE(id1 < id2);
  EXPECT_LT(id1, id2);
}

TEST(StrongTest, ostreamAndFmtFormatter) {
  StrongStrA id{"user123"};

  // ostream support
  std::ostringstream oss;
  oss << id;
  EXPECT_EQ(oss.str(), "user123");

  // fmt::formatter support
  EXPECT_EQ(fmt::format("ID: {}", id), "ID: user123");
}

} // namespace folly

namespace other::unrelated::ns {

struct Potato : public ::folly::strong<int, Potato> {
  using strong<int, Potato>::strong;
};

// Test ADL for `operator<<` + `Potato` via its `strong` base.
TEST(StrongTest, ostreamInUnrelatedNamespace) {
  std::ostringstream oss;
  oss << Potato{1337};
  EXPECT_EQ(oss.str(), "1337");
}

struct CustomOutput : public ::folly::strong<int, CustomOutput> {
  using strong<int, CustomOutput>::strong;
};

} // namespace other::unrelated::ns

template <>
struct fmt::formatter<other::unrelated::ns::CustomOutput> {
  constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }
  auto format(const other::unrelated::ns::CustomOutput& id, auto& ctx) const {
    return fmt::format_to(ctx.out(), "CustomOutput({})", id.value());
  }
};

namespace other::unrelated::ns {

std::ostream& operator<<(std::ostream& os, const CustomOutput& id) {
  return os << "CustomOutput(" << id.value() << ")";
}

TEST(StrongTest, customFormatters) {
  CustomOutput id{42};

  std::ostringstream oss;
  oss << id;
  EXPECT_EQ(oss.str(), "CustomOutput(42)");

  EXPECT_EQ(fmt::format("{}", id), "CustomOutput(42)");
}

} // namespace other::unrelated::ns
