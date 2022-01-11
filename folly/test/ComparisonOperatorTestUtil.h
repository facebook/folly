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

#include <functional>
#include <sstream>
#include <folly/portability/GTest.h>

namespace folly {
namespace test {
namespace detail {

/**
 * Returns a lambda, which when called, will return a string describing the
 * first and second values. Using a lambda vs direct returning the string will
 * ensure the object under test will not have any other methods called
 * (including those invoked by an ostream operator) until the lambda is invoked.
 * This will allow usage in `EXPECT_TRUE(...) << theReturnedLambda()` that will
 * only trigger when a test has already failed.
 *
 * NOTE: The returned lambda will have captured the provided references and thus
 * is only intended for use while those references remain valid. We
 * intentionally don't capture by value to again leave the object intact, but
 * also not every object has a copy constructor. We take by reference_wrapper to
 * make this more clear in the calling code, and communicate that it should not
 * be null.
 */
template <typename FirstType, typename SecondType>
auto getComparisonTestDescriptionGenerator(
    const std::string& firstVarName,
    std::reference_wrapper<const FirstType> firstValueRef,
    const std::string& secondVarName,
    std::reference_wrapper<const SecondType> secondValueRef) {
  return [=]() {
    std::stringstream ss;

    ss << firstVarName << ": " << ::testing::PrintToString(firstValueRef.get())
       << ", compared to " << secondVarName << ": "
       << ::testing::PrintToString(secondValueRef.get());

    return ss.str();
  };
}

/**
 * NOTE: All the below operator testing functions will ensure that each value
 * will show up on both sides of the operator under test, so as to exercise the
 * operators of each type/value - given that the logic may vary.
 *
 * NOTE: All the test functions use EXPECT gtest macros and thus will emit
 * multiple gtest failures for all operator failures within the function.
 *
 * NOTE: In all functions manual `EXPECT_TRUE/FALSE(valueA op valueB)` type
 * calls are done, as opposed to using the 1EXPECT_LT/LE/EQ/NE/GE/GT` macros
 * which can accomplish the same. This is purely for clarity of visual
 * inspection.
 */

/**
 * Tests equality operators (`==`, `!=`) for values which should be
 * considered equal.
 */
template <typename TypeA, typename TypeB>
void testEqualityOperatorsForEqualValues(
    const TypeA& valueA, const TypeB& valueB) {
  auto genDescription = getComparisonTestDescriptionGenerator(
      "valueA", std::cref(valueA), "valueB", std::cref(valueB));

  EXPECT_TRUE(valueA == valueB) << genDescription();
  EXPECT_TRUE(valueB == valueA) << genDescription();

  EXPECT_FALSE(valueA != valueB) << genDescription();
  EXPECT_FALSE(valueB != valueA) << genDescription();
}

/**
 * Tests ordering operators (`<`, `<=`, `>`, `>=`) for values
 * which should be considered equal.
 */
template <typename TypeA, typename TypeB>
void testOrderingOperatorsForEqualValues(
    const TypeA& valueA, const TypeB& valueB) {
  auto genDescription = getComparisonTestDescriptionGenerator(
      "valueA", std::cref(valueA), "valueB", std::cref(valueB));

  EXPECT_FALSE(valueA < valueB) << genDescription();
  EXPECT_FALSE(valueB < valueA) << genDescription();

  EXPECT_TRUE(valueA <= valueB) << genDescription();
  EXPECT_TRUE(valueB <= valueA) << genDescription();

  EXPECT_TRUE(valueA >= valueB) << genDescription();
  EXPECT_TRUE(valueB >= valueA) << genDescription();

  EXPECT_FALSE(valueA > valueB) << genDescription();
  EXPECT_FALSE(valueB > valueA) << genDescription();
}

/**
 * Tests all comparison operators (`==`, `!=`, `<`, `<=`, `>`, `>=`) for values
 * which should be considered equal.
 */
template <typename TypeA, typename TypeB>
void testComparisonOperatorsForEqualValues(
    const TypeA& valueA, const TypeB& valueB) {
  testEqualityOperatorsForEqualValues(valueA, valueB);
  testOrderingOperatorsForEqualValues(valueA, valueB);
}

/**
 * Tests equality operators (`==`, `!=`) for values which should not be
 * considered equal.
 */
template <typename TypeA, typename TypeB>
void testEqualityOperatorsForNotEqualValues(
    const TypeA& valueA, const TypeB& valueB) {
  auto genDescription = getComparisonTestDescriptionGenerator(
      "valueA", std::cref(valueA), "valueB", std::cref(valueB));

  EXPECT_FALSE(valueA == valueB) << genDescription();
  EXPECT_FALSE(valueB == valueA) << genDescription();

  EXPECT_TRUE(valueA != valueB) << genDescription();
  EXPECT_TRUE(valueB != valueA) << genDescription();
}

/**
 * Tests ordering operators (`<`, `<=`, `>`, `>=`) for values
 * which should not be considered equal.
 */
template <typename TypeA, typename TypeB>
void testOrderingOperatorsForNotEqualValues(
    const TypeA& smallerValue, const TypeB& largerValue) {
  auto genDescription = getComparisonTestDescriptionGenerator(
      "smallerValue",
      std::cref(smallerValue),
      "largerValue",
      std::cref(largerValue));

  EXPECT_TRUE(smallerValue < largerValue) << genDescription();
  EXPECT_FALSE(largerValue < smallerValue) << genDescription();

  EXPECT_TRUE(smallerValue <= largerValue) << genDescription();
  EXPECT_FALSE(largerValue <= smallerValue) << genDescription();

  EXPECT_FALSE(smallerValue >= largerValue) << genDescription();
  EXPECT_TRUE(largerValue >= smallerValue) << genDescription();

  EXPECT_FALSE(smallerValue > largerValue) << genDescription();
  EXPECT_TRUE(largerValue > smallerValue) << genDescription();
}

/**
 * Tests all comparison operators (`==`, `!=`, `<`, `<=`, `>`, `>=`) for values
 * which should not be considered equal.
 */
template <typename TypeA, typename TypeB>
void testComparisonOperatorsForNotEqualValues(
    const TypeA& smallerValue, const TypeB& largerValue) {
  testEqualityOperatorsForNotEqualValues(smallerValue, largerValue);
  testOrderingOperatorsForNotEqualValues(smallerValue, largerValue);
}

} // namespace detail
} // namespace test
} // namespace folly
