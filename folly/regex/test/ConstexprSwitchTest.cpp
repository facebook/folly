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

#include <folly/regex/detail/ConstexprSwitch.h>

#include <folly/portability/GTest.h>

using namespace folly::regex::detail;

namespace {

enum class Color : int { Red = 0, Green = 1, Blue = 2 };
inline constexpr int kMaxColor = static_cast<int>(Color::Blue);

// Per-enum alias so call sites don't need to specify MaxEnumValue.
template <typename... Cases>
constexpr auto colorSwitch(auto enumVal, auto... args) {
  return constexprSwitch<kMaxColor, Cases...>(enumVal, args...);
}

} // namespace

TEST(ConstexprSwitchTest, BasicDispatch) {
  constexpr auto result = colorSwitch<
      Case<Color::Red, [](int x) { return x + 1; }>,
      Case<Color::Green, [](int x) { return x + 2; }>,
      Case<Color::Blue, [](int x) { return x + 3; }>>(Color::Green, 10);
  static_assert(result == 12);
  EXPECT_EQ(result, 12);
}

TEST(ConstexprSwitchTest, Fallthrough) {
  // Red falls through to Green's handler
  constexpr auto result = colorSwitch<
      Case<Color::Red, Fallthrough>,
      Case<Color::Green, [](int x) { return x * 10; }>,
      Case<Color::Blue, [](int x) { return x * 20; }>>(Color::Red, 5);
  static_assert(result == 50);
  EXPECT_EQ(result, 50);
}

TEST(ConstexprSwitchTest, ChainedFallthrough) {
  // Red and Green both fall through to Blue's handler
  constexpr auto result = colorSwitch<
      Case<Color::Red, Fallthrough>,
      Case<Color::Green, Fallthrough>,
      Case<Color::Blue, [](int x) { return x + 100; }>>(Color::Red, 0);
  static_assert(result == 100);
  EXPECT_EQ(result, 100);
}

TEST(ConstexprSwitchTest, DefaultHandler) {
  // Only Red is explicit; Green and Blue use Default
  constexpr auto result = colorSwitch<
      Case<Color::Red, [](int x) { return x + 1; }>,
      Case<Default, [](int) { return -1; }>>(Color::Blue, 42);
  static_assert(result == -1);
  EXPECT_EQ(result, -1);
}

TEST(ConstexprSwitchTest, DefaultDoesNotOverrideExplicit) {
  constexpr auto red = colorSwitch<
      Case<Color::Red, [](int) { return 1; }>,
      Case<Color::Green, [](int) { return 2; }>,
      Case<Default, [](int) { return -1; }>>(Color::Red, 0);
  constexpr auto green = colorSwitch<
      Case<Color::Red, [](int) { return 1; }>,
      Case<Color::Green, [](int) { return 2; }>,
      Case<Default, [](int) { return -1; }>>(Color::Green, 0);
  constexpr auto blue = colorSwitch<
      Case<Color::Red, [](int) { return 1; }>,
      Case<Color::Green, [](int) { return 2; }>,
      Case<Default, [](int) { return -1; }>>(Color::Blue, 0);
  static_assert(red == 1);
  static_assert(green == 2);
  static_assert(blue == -1);
  EXPECT_EQ(red, 1);
  EXPECT_EQ(green, 2);
  EXPECT_EQ(blue, -1);
}

TEST(ConstexprSwitchTest, MultipleArguments) {
  constexpr auto result = colorSwitch<
      Case<Color::Red, [](int a, int b) { return a + b; }>,
      Case<Color::Green, [](int a, int b) { return a - b; }>,
      Case<Color::Blue, [](int a, int b) { return a * b; }>>(Color::Blue, 6, 7);
  static_assert(result == 42);
  EXPECT_EQ(result, 42);
}

TEST(ConstexprSwitchTest, VoidReturn) {
  int out = 0;
  colorSwitch<
      Case<Color::Red, [](int* p) { *p = 1; }>,
      Case<Color::Green, [](int* p) { *p = 2; }>,
      Case<Color::Blue, [](int* p) { *p = 3; }>>(Color::Green, &out);
  EXPECT_EQ(out, 2);
}

TEST(ConstexprSwitchTest, AllCasesExercised) {
  // Ensure every enum value dispatches correctly
  for (int i = 0; i <= kMaxColor; ++i) {
    auto c = static_cast<Color>(i);
    int result = colorSwitch<
        Case<Color::Red, [](int) { return 10; }>,
        Case<Color::Green, [](int) { return 20; }>,
        Case<Color::Blue, [](int) { return 30; }>>(c, 0);
    EXPECT_EQ(result, (i + 1) * 10);
  }
}

TEST(ConstexprSwitchTest, FallthroughWithDefault) {
  // Red falls through to Green; Blue uses Default
  constexpr auto red = colorSwitch<
      Case<Color::Red, Fallthrough>,
      Case<Color::Green, [](int) { return 1; }>,
      Case<Default, [](int) { return -1; }>>(Color::Red, 0);
  constexpr auto green = colorSwitch<
      Case<Color::Red, Fallthrough>,
      Case<Color::Green, [](int) { return 1; }>,
      Case<Default, [](int) { return -1; }>>(Color::Green, 0);
  constexpr auto blue = colorSwitch<
      Case<Color::Red, Fallthrough>,
      Case<Color::Green, [](int) { return 1; }>,
      Case<Default, [](int) { return -1; }>>(Color::Blue, 0);
  static_assert(red == 1);
  static_assert(green == 1);
  static_assert(blue == -1);
}
