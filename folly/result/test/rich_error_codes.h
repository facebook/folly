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

#pragma once

#include <cstdint>
#include <type_traits>

#include <fmt/core.h>
#include <fmt/format.h>
#include <folly/Traits.h>
#include <folly/result/rich_error.h>
#include <folly/result/rich_error_code.h>

// Shared between `rich_error_code_test.cpp` and `rich_error_code_bench.cpp`

namespace folly {

// Error code definitions
enum class A1 { ZERO_A1 = 0, ONE_A1 = 1, TWO_A1 = 2 };
enum class ImplB1 : int { ZERO_B1 = 10, ONE_B1 = 11, TWO_B1 = 12 };
struct B1 {
  ImplB1 value;
  constexpr bool operator==(const B1&) const = default;
};
enum class B2 : int { ZERO_B2 = -20, ONE_B2 = -21, TWO_B2 = -22 };
enum class C1 { ZERO_C1 = 100, ONE_C1 = 101, TWO_C1 = 102 };

} // namespace folly

// Formatter for B1 type
template <>
struct fmt::formatter<folly::B1> {
  constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

  auto format(const folly::B1& b1, format_context& ctx) const {
    return fmt::format_to(ctx.out(), "{}", static_cast<int>(b1.value));
  }
};

namespace folly {

// rich_error_code specializations: UUID + type-erasure (mangle/unmangle)

template <>
struct rich_error_code<A1> {
  static constexpr uint64_t uuid = 154011801583020582ULL;
  static constexpr const char* name = "A1";
};

template <>
struct rich_error_code<B1> {
  static constexpr uint64_t uuid = 8583738238600016729ULL;
  static constexpr const char* name = "B1";
  static constexpr uintptr_t mangle_code(B1 code) {
    auto underlying = to_underlying(code.value);
    static_assert(sizeof(underlying) <= sizeof(uintptr_t));
    return static_cast<uintptr_t>(underlying);
  }
  static constexpr B1 unmangle_code(uintptr_t value) {
    using Underlying = std::underlying_type_t<ImplB1>;
    static_assert(sizeof(Underlying) <= sizeof(uintptr_t));
    return B1{.value = static_cast<ImplB1>(static_cast<Underlying>(value))};
  }
};

template <>
struct rich_error_code<B2> {
  static constexpr uint64_t uuid = 15264009825950242918ULL;
  static constexpr const char* name = "B2";
};

template <>
struct rich_error_code<C1> {
  static constexpr uint64_t uuid = 17144754153784372294ULL;
};

// Example error hierarchy: ErrorA -> ErrorB -> ErrorC
class ErrorB;
class ErrorC;

class ErrorA : public rich_error_base {
  A1 a1_;

 public:
  explicit constexpr ErrorA(A1 a1) : a1_(a1) {}

  constexpr A1 a1() const { return a1_; }
  using folly_rich_error_codes_t =
      rich_error_bases_and_own_codes<ErrorA, tag_t<>, &ErrorA::a1>;
  constexpr void retrieve_code(rich_error_code_query& c) const override {
    return folly_rich_error_codes_t::retrieve_code(*this, c);
  }
  using folly_get_exception_hint_types =
      rich_error_hints<ErrorC, ErrorB, ErrorA>;
};

class ErrorB : public ErrorA {
  B1 b1_;
  B2 b2_;

 public:
  constexpr ErrorB(A1 a, B1 b1, B2 b2) : ErrorA(a), b1_(b1), b2_(b2) {}

  constexpr B1 b1() const { return b1_; }
  constexpr B2 b2() const { return b2_; }
  using folly_rich_error_codes_t = rich_error_bases_and_own_codes<
      ErrorB,
      tag_t<ErrorA>,
      &ErrorB::b1,
      &ErrorB::b2>;
  constexpr void retrieve_code(rich_error_code_query& c) const override {
    return folly_rich_error_codes_t::retrieve_code(*this, c);
  }
  using folly_get_exception_hint_types = rich_error_hints<ErrorC, ErrorB>;
};

class ErrorC : public ErrorB {
  C1 c1_;

 public:
  constexpr ErrorC(A1 a, B1 b1, B2 b2, C1 c) : ErrorB(a, b1, b2), c1_(c) {}

  constexpr C1 c1() const { return c1_; }
  using folly_rich_error_codes_t =
      rich_error_bases_and_own_codes<ErrorC, tag_t<ErrorB>, &ErrorC::c1>;
  constexpr void retrieve_code(rich_error_code_query& c) const override {
    return folly_rich_error_codes_t::retrieve_code(*this, c);
  }
  using folly_get_exception_hint_types = rich_error_hints<ErrorC>;
};

} // namespace folly
