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

#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Coroutine.h>
#include <folly/experimental/coro/Task.h>
#include <folly/experimental/exception_tracer/SmartExceptionTracer.h>
#include <folly/portability/GTest.h>

#if FOLLY_HAS_COROUTINES

#ifdef FOLLY_HAVE_SMART_EXCEPTION_TRACER

#define LOG_ASYNC_TEST_EXCEPTION                            \
  GTEST_LOG_(ERROR) << ex.what() << ", async stack trace: " \
                    << folly::exception_tracer::getAsyncTrace(ex);

#else

#define LOG_ASYNC_TEST_EXCEPTION GTEST_LOG_(ERROR) << ex.what();

#endif

/**
 * This is based on the GTEST_TEST_ macro from gtest-internal.h. It seems that
 * gtest doesn't yet support coro tests, so this macro adds a way to define a
 * test case written as a coroutine using folly::coro::Task. It will be called
 * using folly::coro::blockingWait().
 *
 * Note that you cannot use ASSERT macros in coro tests. See below for
 * CO_ASSERT_*.
 */
#define CO_TEST_(test_suite_name, test_name, parent_class, parent_id)          \
  static_assert(                                                               \
      sizeof(GTEST_STRINGIFY_(test_suite_name)) > 1,                           \
      "test_suite_name must not be empty");                                    \
  static_assert(                                                               \
      sizeof(GTEST_STRINGIFY_(test_name)) > 1, "test_name must not be empty"); \
  class GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)                     \
      : public parent_class {                                                  \
   public:                                                                     \
    GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)() = default;            \
    ~GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)() override = default;  \
    GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)                         \
    (const GTEST_TEST_CLASS_NAME_(test_suite_name, test_name) &) = delete;     \
    GTEST_TEST_CLASS_NAME_(test_suite_name, test_name) & operator=(            \
        const GTEST_TEST_CLASS_NAME_(test_suite_name, test_name) &) =          \
        delete; /* NOLINT */                                                   \
    GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)                         \
    (GTEST_TEST_CLASS_NAME_(test_suite_name, test_name) &&) noexcept = delete; \
    GTEST_TEST_CLASS_NAME_(test_suite_name, test_name) & operator=(            \
        GTEST_TEST_CLASS_NAME_(test_suite_name, test_name) &&) noexcept =      \
        delete; /* NOLINT */                                                   \
                                                                               \
   private:                                                                    \
    void TestBody() override;                                                  \
    folly::coro::Task<void> co_TestBody();                                     \
    static ::testing::TestInfo* const test_info_ GTEST_ATTRIBUTE_UNUSED_;      \
  };                                                                           \
                                                                               \
  ::testing::TestInfo* const GTEST_TEST_CLASS_NAME_(                           \
      test_suite_name, test_name)::test_info_ =                                \
      ::testing::internal::MakeAndRegisterTestInfo(                            \
          #test_suite_name,                                                    \
          #test_name,                                                          \
          nullptr,                                                             \
          nullptr,                                                             \
          ::testing::internal::CodeLocation(__FILE__, __LINE__),               \
          (parent_id),                                                         \
          ::testing::internal::SuiteApiResolver<                               \
              parent_class>::GetSetUpCaseOrSuite(__FILE__, __LINE__),          \
          ::testing::internal::SuiteApiResolver<                               \
              parent_class>::GetTearDownCaseOrSuite(__FILE__, __LINE__),       \
          new ::testing::internal::TestFactoryImpl<GTEST_TEST_CLASS_NAME_(     \
              test_suite_name, test_name)>);                                   \
  void GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)::TestBody() {        \
    try {                                                                      \
      folly::coro::blockingWait(co_TestBody());                                \
    } catch (const std::exception& ex) {                                       \
      LOG_ASYNC_TEST_EXCEPTION                                                 \
      throw;                                                                   \
    }                                                                          \
  }                                                                            \
  folly::coro::Task<void> GTEST_TEST_CLASS_NAME_(                              \
      test_suite_name, test_name)::co_TestBody()

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

#define CO_TEST_P(test_suite_name, test_name)                                  \
  class GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)                     \
      : public test_suite_name {                                               \
   public:                                                                     \
    GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)() {}                    \
    void TestBody() override;                                                  \
    folly::coro::Task<void> co_TestBody();                                     \
                                                                               \
   private:                                                                    \
    static int AddToRegistry() {                                               \
      ::testing::UnitTest::GetInstance()                                       \
          ->parameterized_test_registry()                                      \
          .GetTestSuitePatternHolder<test_suite_name>(                         \
              GTEST_STRINGIFY_(test_suite_name),                               \
              ::testing::internal::CodeLocation(__FILE__, __LINE__))           \
          ->AddTestPattern(                                                    \
              GTEST_STRINGIFY_(test_suite_name),                               \
              GTEST_STRINGIFY_(test_name),                                     \
              new ::testing::internal::TestMetaFactory<GTEST_TEST_CLASS_NAME_( \
                  test_suite_name, test_name)>(),                              \
              ::testing::internal::CodeLocation(__FILE__, __LINE__));          \
      return 0;                                                                \
    }                                                                          \
    static int gtest_registering_dummy_ GTEST_ATTRIBUTE_UNUSED_;               \
    GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)                         \
    (const GTEST_TEST_CLASS_NAME_(test_suite_name, test_name) &) = delete;     \
    GTEST_TEST_CLASS_NAME_(test_suite_name, test_name) & operator=(            \
        const GTEST_TEST_CLASS_NAME_(test_suite_name, test_name) &) =          \
        delete; /* NOLINT */                                                   \
  };                                                                           \
  int GTEST_TEST_CLASS_NAME_(                                                  \
      test_suite_name, test_name)::gtest_registering_dummy_ =                  \
      GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)::AddToRegistry();     \
  void GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)::TestBody() {        \
    try {                                                                      \
      folly::coro::blockingWait(co_TestBody());                                \
    } catch (const std::exception& ex) {                                       \
      LOG_ASYNC_TEST_EXCEPTION                                                 \
      throw;                                                                   \
    }                                                                          \
  }                                                                            \
  folly::coro::Task<void> GTEST_TEST_CLASS_NAME_(                              \
      test_suite_name, test_name)::co_TestBody()

#define CO_TYPED_TEST(CaseName, TestName)                                     \
  static_assert(                                                              \
      sizeof(GTEST_STRINGIFY_(TestName)) > 1, "test-name must not be empty"); \
  template <typename gtest_TypeParam_>                                        \
  class GTEST_TEST_CLASS_NAME_(CaseName, TestName)                            \
      : public CaseName<gtest_TypeParam_> {                                   \
   private:                                                                   \
    typedef CaseName<gtest_TypeParam_> TestFixture;                           \
    typedef gtest_TypeParam_ TypeParam;                                       \
    void TestBody() override;                                                 \
    folly::coro::Task<void> co_TestBody();                                    \
  };                                                                          \
  static bool gtest_##CaseName##_##TestName##_registered_                     \
      GTEST_ATTRIBUTE_UNUSED_ = ::testing::internal::TypeParameterizedTest<   \
          CaseName,                                                           \
          ::testing::internal::TemplateSel<GTEST_TEST_CLASS_NAME_(            \
              CaseName, TestName)>,                                           \
          GTEST_TYPE_PARAMS_(CaseName)>::                                     \
          Register(                                                           \
              "",                                                             \
              ::testing::internal::CodeLocation(__FILE__, __LINE__),          \
              GTEST_STRINGIFY_(CaseName),                                     \
              GTEST_STRINGIFY_(TestName),                                     \
              0,                                                              \
              ::testing::internal::GenerateNames<                             \
                  GTEST_NAME_GENERATOR_(CaseName),                            \
                  GTEST_TYPE_PARAMS_(CaseName)>());                           \
  template <typename gtest_TypeParam_>                                        \
  void GTEST_TEST_CLASS_NAME_(                                                \
      CaseName, TestName)<gtest_TypeParam_>::TestBody() {                     \
    try {                                                                     \
      folly::coro::blockingWait(co_TestBody());                               \
    } catch (const std::exception& ex) {                                      \
      LOG_ASYNC_TEST_EXCEPTION                                                \
      throw;                                                                  \
    }                                                                         \
  }                                                                           \
  template <typename gtest_TypeParam_>                                        \
  folly::coro::Task<void> GTEST_TEST_CLASS_NAME_(                             \
      CaseName, TestName)<gtest_TypeParam_>::co_TestBody()

/**
 * Coroutine versions of GTests's Assertion predicate macros. Use these in place
 * of ASSERT_* in CO_TEST or coroutine functions.
 */
#define CO_GTEST_FATAL_FAILURE_(message) \
  co_return GTEST_MESSAGE_(message, ::testing::TestPartResult::kFatalFailure)

#define CO_ASSERT_PRED_FORMAT1(pred_format, v1) \
  GTEST_PRED_FORMAT1_(pred_format, v1, CO_GTEST_FATAL_FAILURE_)
#define CO_ASSERT_PRED_FORMAT2(pred_format, v1, v2) \
  GTEST_PRED_FORMAT2_(pred_format, v1, v2, CO_GTEST_FATAL_FAILURE_)

#define CO_ASSERT_TRUE(condition) \
  GTEST_TEST_BOOLEAN_(            \
      (condition), #condition, false, true, CO_GTEST_FATAL_FAILURE_)
#define CO_ASSERT_FALSE(condition) \
  GTEST_TEST_BOOLEAN_(             \
      !(condition), #condition, true, false, CO_GTEST_FATAL_FAILURE_)

#if defined(GTEST_IS_NULL_LITERAL_)
#define CO_ASSERT_EQ(val1, val2)                                            \
  CO_ASSERT_PRED_FORMAT2(                                                   \
      ::testing::internal::EqHelper<GTEST_IS_NULL_LITERAL_(val1)>::Compare, \
      val1,                                                                 \
      val2)
#else
#define CO_ASSERT_EQ(val1, val2) \
  CO_ASSERT_PRED_FORMAT2(::testing::internal::EqHelper::Compare, val1, val2)
#endif

#define CO_ASSERT_NE(val1, val2) \
  CO_ASSERT_PRED_FORMAT2(::testing::internal::CmpHelperNE, val1, val2)
#define CO_ASSERT_LE(val1, val2) \
  CO_ASSERT_PRED_FORMAT2(::testing::internal::CmpHelperLE, val1, val2)
#define CO_ASSERT_LT(val1, val2) \
  CO_ASSERT_PRED_FORMAT2(::testing::internal::CmpHelperLT, val1, val2)
#define CO_ASSERT_GE(val1, val2) \
  CO_ASSERT_PRED_FORMAT2(::testing::internal::CmpHelperGE, val1, val2)
#define CO_ASSERT_GT(val1, val2) \
  CO_ASSERT_PRED_FORMAT2(::testing::internal::CmpHelperGT, val1, val2)

#define CO_ASSERT_THROW(statement, expected_exception) \
  GTEST_TEST_THROW_(statement, expected_exception, CO_GTEST_FATAL_FAILURE_)
#define CO_ASSERT_NO_THROW(statement) \
  GTEST_TEST_NO_THROW_(statement, CO_GTEST_FATAL_FAILURE_)
#define CO_ASSERT_ANY_THROW(statement) \
  GTEST_TEST_ANY_THROW_(statement, CO_GTEST_FATAL_FAILURE_)

/**
 * coroutine version of FAIL() which is defined as GTEST_FAIL()
 * GTEST_FATAL_FAILURE_("Failed")
 */
#define CO_FAIL() CO_GTEST_FATAL_FAILURE_("Failed")

/**
 * Coroutine version of SKIP() which is defined as GTEST_SKIP()
 */
#define CO_SKIP(message) \
  co_return GTEST_MESSAGE_(message, ::testing::TestPartResult::kSkip)

/**
 * Coroutine version of SKIP_IF()
 */
#define CO_SKIP_IF(expr, message) \
  GTEST_AMBIGUOUS_ELSE_BLOCKER_   \
  if (!(expr)) {                  \
  } else                          \
    CO_SKIP(message)

#endif // FOLLY_HAS_COROUTINES
