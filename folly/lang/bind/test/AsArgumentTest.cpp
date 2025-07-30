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

#include <folly/lang/bind/AsArgument.h>
#include <folly/portability/GTest.h>

namespace folly::bind::ext {

// This is here so that test "runs" show up in CI history
TEST(AsArgumentTest, all_tests_run_at_build_time) {}

// To test static_asserts, change the 0 to a 1 in the each #if, in turn
constexpr auto manually_check_error_cases() {
#if 0 // Test that `bind::copy` requires an rvalue ref
  int a = 42;
  auto result =
      bind_as_argument<bind_info_t{category_t::copy, constness_t::unset}>(
          std::move(a));
#elif 0 // Test that bind::move requires an rvalue ref
  int a = 42;
  auto result =
      bind_as_argument<bind_info_t{category_t::move, constness_t::unset}>(a);
#elif 0 // Test that we can't use constness with move/copy
  int a = 42;
  auto result =
      bind_as_argument<bind_info_t{category_t::move, constness_t::constant}>(
          std::move(a));
#endif
  return true;
}

static_assert(manually_check_error_cases());

// `return` path 1: Pass by const ref
// BI.category == category_t::ref || BI.category == category_t::unset
// BI.constness == constness_t::constant || BI.constness == constness_t::unset
template <bind_info_t BI>
constexpr auto check_pass_by_const_ref() {
  int a = 42;
  auto fn = [&]() -> decltype(auto) { return bind_as_argument<BI>(a); };
  static_assert(std::is_same_v<const int&, decltype(fn())>);
  return fn() == 42;
}

static_assert(check_pass_by_const_ref<bind_info_t{
                  category_t::unset, constness_t::unset}>());
static_assert(check_pass_by_const_ref<bind_info_t{
                  category_t::ref, constness_t::constant}>());
static_assert(check_pass_by_const_ref<bind_info_t{
                  category_t::ref, constness_t::constant}>());

// `return` path 1 edge case: Pass rvalue ref by const ref
constexpr auto check_pass_rvalue_by_const_ref() {
  int a = 42;
  auto fn = [&]() -> decltype(auto) {
    return bind_as_argument<bind_info_t{category_t::unset, constness_t::unset}>(
        std::move(a));
  };
  static_assert(std::is_same_v<const int&&, decltype(fn())>);
  return fn() == 42;
}

static_assert(check_pass_rvalue_by_const_ref());

// `return` path 2: Pass by mutable ref
// BI.category == category_t::ref || BI.category == category_t::unset
// BI.constness == constness_t::mut
template <bind_info_t BI>
constexpr auto check_pass_by_mut_ref() {
  int a = 42;
  auto fn = [&]() -> decltype(auto) { return bind_as_argument<BI>(a); };
  static_assert(std::is_same_v<int&, decltype(fn())>);
  return fn() == 42;
}

static_assert(
    check_pass_by_mut_ref<bind_info_t{category_t::ref, constness_t::mut}>());
static_assert(
    check_pass_by_mut_ref<bind_info_t{category_t::unset, constness_t::mut}>());

struct MoveCopyCounter {
  int moveCount_ = 0;
  int copyCount_ = 0;

  MoveCopyCounter() = default;
  constexpr MoveCopyCounter(MoveCopyCounter&& other) noexcept
      : moveCount_{other.moveCount_ + 1}, copyCount_{other.copyCount_} {}
  constexpr MoveCopyCounter& operator=(MoveCopyCounter&&) = delete;
  constexpr MoveCopyCounter(const MoveCopyCounter& other)
      : moveCount_{other.moveCount_}, copyCount_{other.copyCount_ + 1} {}
  constexpr MoveCopyCounter& operator=(const MoveCopyCounter&) = delete;
  ~MoveCopyCounter() = default;
};

// `return` path 3: Pass by copy
// BI.category == category_t::copy
constexpr auto check_pass_by_copy() {
  MoveCopyCounter mcc;
  auto fn = [&]() -> decltype(auto) {
    return bind_as_argument<bind_info_t{category_t::copy, constness_t::unset}>(
        mcc);
  };
  static_assert(std::is_same_v<MoveCopyCounter, decltype(fn())>);
  auto mcc2 = fn();
  return (mcc2.moveCount_ == 0) && (mcc2.copyCount_ == 1);
}

static_assert(check_pass_by_copy());

struct MoveOnlyInt {
  int value;

  explicit constexpr MoveOnlyInt(int v) : value(v) {}
  constexpr MoveOnlyInt(MoveOnlyInt&& other) noexcept : value(other.value) {
    other.value = 0;
  }
  constexpr MoveOnlyInt& operator=(MoveOnlyInt&&) = delete;
  constexpr MoveOnlyInt(const MoveOnlyInt&) = delete;
  constexpr MoveOnlyInt& operator=(const MoveOnlyInt&) = delete;
  ~MoveOnlyInt() = default;
};

// `return`  path 4: Pass by move
// BI.category == category_t::move
constexpr auto check_pass_by_move() {
  MoveOnlyInt moveFrom{100};
  auto fn = [&]() -> decltype(auto) {
    return bind_as_argument<bind_info_t{category_t::move, constness_t::unset}>(
        std::move(moveFrom));
  };
  static_assert(std::is_same_v<MoveOnlyInt&&, decltype(fn())>);
  auto moveTo = fn();
  return (moveTo.value == 100) && (moveFrom.value == 0);
}

static_assert(check_pass_by_move());

} // namespace folly::bind::ext
