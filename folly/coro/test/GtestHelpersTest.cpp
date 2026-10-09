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

#include <folly/portability/GTest.h>

#include <gtest/gtest-spi.h>

#include <folly/coro/GtestHelpers.h>

using namespace ::testing;

namespace {
folly::coro::Task<int> co_getInt(int x) {
  co_return x;
}

struct GtestHelpersMultiplicationTestParam {
  int x;
  int y;
  int expectedProduct;
};
} // namespace

class GtestHelpersMultiplicationTest
    : public TestWithParam<GtestHelpersMultiplicationTestParam> {};

CO_TEST_P(GtestHelpersMultiplicationTest, BasicTest) {
  const auto& param = GetParam();
  int product = (co_await co_getInt(param.x)) * (co_await co_getInt(param.y));

  EXPECT_EQ(product, param.expectedProduct);
}

INSTANTIATE_TEST_SUITE_P(
    GtestHelpersMultiplicationTest,
    GtestHelpersMultiplicationTest,
    ValuesIn(
        std::vector<GtestHelpersMultiplicationTestParam>{
            {1, 1, 1},
            {1, 2, 2},
            {2, 2, 4},
            {-1, -6, 6},
        }));

namespace {

// allow same test case to be used over different types.
template <typename T>
struct GtestHelpersTypedTest : public ::testing::Test {
  using type = T;

  folly::coro::Task<std::string> type_str() const {
    co_return std::string(folly::demangle(typeid(type).name()));
  }
};

struct Foo {};

// also allow same test case to be used with different fixture classes.
template <>
struct GtestHelpersTypedTest<Foo> : public ::testing::Test {
  using type = Foo;

  folly::coro::Task<std::string> type_str() const { co_return "Foo_x"; }
};

using CoroTypedTests = ::testing::Types<int, char, Foo>;

TYPED_TEST_SUITE(GtestHelpersTypedTest, CoroTypedTests);

CO_TYPED_TEST(GtestHelpersTypedTest, Test_type_str) {
  using type = typename std::remove_reference_t<decltype(*this)>::type;

  if constexpr (std::is_same_v<Foo, type>) {
    EXPECT_EQ(co_await this->type_str(), "Foo_x");
  } else if constexpr (std::is_same_v<int, type>) {
    EXPECT_EQ(co_await this->type_str(), "int");
  } else if constexpr (std::is_same_v<char, type>) {
    EXPECT_EQ(co_await this->type_str(), "char");
  }
}

template <class T>
class GtestHelpersTypedTest2 : public GtestHelpersTypedTest<T> {};

TYPED_TEST_SUITE_P(GtestHelpersTypedTest2);

CO_TYPED_TEST_P(GtestHelpersTypedTest2, Test_type_str) {
  using type = typename std::remove_reference_t<decltype(*this)>::type;

  if constexpr (std::is_same_v<Foo, type>) {
    EXPECT_EQ(co_await this->type_str(), "Foo_x");
  } else if constexpr (std::is_same_v<int, type>) {
    EXPECT_EQ(co_await this->type_str(), "int");
  } else if constexpr (std::is_same_v<char, type>) {
    EXPECT_EQ(co_await this->type_str(), "char");
  }
}

REGISTER_TYPED_TEST_SUITE_P(GtestHelpersTypedTest2, Test_type_str);

INSTANTIATE_TYPED_TEST_SUITE_P(
    GtestHelpersTypedTest2, GtestHelpersTypedTest2, CoroTypedTests);

CO_TEST(GtestHelpersTest, testCoAssertNoThrow) {
  CO_ASSERT_NO_THROW(co_await co_getInt(0));
}

CO_TEST(GtestHelpersTest, testCoAssertThrow) {
  constexpr auto co_throwInvalidArgument = []() -> folly::coro::Task<> {
    throw std::invalid_argument{""};
  };
  CO_ASSERT_THROW(co_await co_throwInvalidArgument(), std::invalid_argument);
  CO_ASSERT_ANY_THROW(co_await co_throwInvalidArgument());
}

CO_TEST(GtestHelpersTest, testCoAssertBooleans) {
  CO_ASSERT_TRUE(true);
  CO_ASSERT_FALSE(false);
  CO_ASSERT_TRUE(AssertionSuccess());
  CO_ASSERT_FALSE(AssertionFailure() << "predicate message");
}

CO_TEST(GtestHelpersTest, testCoAssertBooleansEvaluateOnce) {
  int count = 0;
  CO_ASSERT_TRUE(++count == 1);
  CO_ASSERT_FALSE(++count == 1);
  EXPECT_EQ(count, 2);

  if (count == 2)
    CO_ASSERT_FALSE(false);
  else
    ADD_FAILURE() << "Assertion must not capture the outer else";
}

folly::coro::Task<> co_assertFalseFailure() {
  CO_ASSERT_FALSE(true);
  ADD_FAILURE() << "Execution continued after a fatal assertion";
}

folly::coro::Task<> co_assertTrueFailure() {
  CO_ASSERT_TRUE(false);
  ADD_FAILURE() << "Execution continued after a fatal assertion";
}

folly::coro::Task<> co_assertFalseFailureWithMessage() {
  CO_ASSERT_FALSE(AssertionSuccess() << "predicate message") << "user message";
  ADD_FAILURE() << "Execution continued after a fatal assertion";
}

TEST(GtestHelpersTest, testCoAssertFalseFailure) {
  EXPECT_FATAL_FAILURE(
      folly::coro::blockingWait(co_assertFalseFailure()),
      "Value of: true\n  Actual: true\nExpected: false");
}

TEST(GtestHelpersTest, testCoAssertTrueFailure) {
  EXPECT_FATAL_FAILURE(
      folly::coro::blockingWait(co_assertTrueFailure()),
      "Value of: false\n  Actual: false\nExpected: true");
}

TEST(GtestHelpersTest, testCoAssertFalseFailureWithMessage) {
  EXPECT_FATAL_FAILURE(
      folly::coro::blockingWait(co_assertFalseFailureWithMessage()),
      "predicate message)\nExpected: false\nuser message");
}

} // namespace
