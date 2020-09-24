/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <gtest/gtest.h>

#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Task.h>

/**
 * This is based on the GTEST_TEST_ macro from gtest-internal.h. It seems that
 * gtest doesn't yet support coro tests, so this macro adds a way to define a
 * test case written as a coroutine using folly::coro::Task. It will be called
 * using folly::coro::blockingWait().
 *
 * Note that you cannot use ASSERT macros in coro tests. We'll need to add a
 * CO_ASSERT macro if it's needed. EXPECT macros work fine.
 */
// clang-format off
#define CO_TEST_(test_case_name, test_name, parent_class, parent_id)\
class GTEST_TEST_CLASS_NAME_(test_case_name, test_name) : public parent_class {\
 public:\
  GTEST_TEST_CLASS_NAME_(test_case_name, test_name)() {}\
 private:\
  void TestBody() override;\
  folly::coro::Task<void> co_TestBody();\
  static ::testing::TestInfo* const test_info_ GTEST_ATTRIBUTE_UNUSED_;\
  GTEST_DISALLOW_COPY_AND_ASSIGN_(\
      GTEST_TEST_CLASS_NAME_(test_case_name, test_name));\
};\
\
::testing::TestInfo* const GTEST_TEST_CLASS_NAME_(test_case_name, test_name)\
  ::test_info_ = /* NOLINT */ \
    ::testing::internal::MakeAndRegisterTestInfo(\
        GTEST_STRINGIFY_TOKEN_(test_case_name), \
        GTEST_STRINGIFY_TOKEN_(test_name), NULL, NULL, \
        ::testing::internal::CodeLocation(__FILE__, __LINE__), /* NOLINT */ \
        (parent_id), \
        parent_class::SetUpTestCase, \
        parent_class::TearDownTestCase, \
        new ::testing::internal::TestFactoryImpl< /* NOLINT */ \
            GTEST_TEST_CLASS_NAME_(test_case_name, test_name)>);\
void GTEST_TEST_CLASS_NAME_(test_case_name, test_name)::TestBody() {\
  folly::coro::blockingWait(co_TestBody());\
}\
folly::coro::Task<void> GTEST_TEST_CLASS_NAME_(test_case_name, test_name)::co_TestBody()
// clang-format on

/**
 * TEST() for coro tests.
 */
#define CO_TEST(test_case_name, test_name) \
  CO_TEST_(                                \
      test_case_name,                      \
      test_name,                           \
      ::testing::Test,                     \
      ::testing::internal::GetTestTypeId())

/**
 * TEST_F() for coro tests.
 */
#define CO_TEST_F(test_fixture, test_name) \
  CO_TEST_(                                \
      test_fixture,                        \
      test_name,                           \
      test_fixture,                        \
      ::testing::internal::GetTypeId<test_fixture>())
