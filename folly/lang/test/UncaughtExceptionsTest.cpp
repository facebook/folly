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

#include <folly/lang/UncaughtExceptions.h>

#include <folly/Conv.h>
#include <folly/functional/Invoke.h>
#include <folly/portability/GTest.h>

FOLLY_CREATE_QUAL_INVOKER(invoke_std, std::uncaught_exceptions);
FOLLY_CREATE_QUAL_INVOKER(invoke_folly, folly::uncaught_exceptions);
// @lint-ignore CLANGTIDY
FOLLY_CREATE_QUAL_INVOKER(
    invoke_folly_detail_, folly::detail::uncaught_exceptions_);

template <typename Param>
struct UncaughtExceptionsTest : testing::TestWithParam<Param> {};
TYPED_TEST_SUITE_P(UncaughtExceptionsTest);

/*
 * Test helper class, when goes out of scope it validaes that
 * folly::uncaught_exceptions() returns the specified
 * value.
 */
template <typename Impl>
class Validator {
 public:
  const Impl impl{};

  Validator(int expectedCount, const std::string& msg)
      : expectedCount_(expectedCount), msg_(msg) {}

  // Automatic validation during destruction.
  ~Validator() { validate(); }

  // Invoke to validate explicitly.
  void validate() { EXPECT_EQ(expectedCount_, impl()) << msg_; }

 private:
  const int32_t expectedCount_;
  const std::string msg_;
};

TYPED_TEST_P(UncaughtExceptionsTest, no_exception) {
  Validator<TypeParam> validator(0, "no_exception");
}

TYPED_TEST_P(UncaughtExceptionsTest, no_uncaught_exception) {
  Validator<TypeParam> validator(0, "no_uncaught_exception");
  try {
    throw std::runtime_error("exception");
  } catch (const std::runtime_error&) {
    validator.validate();
  }
}

TYPED_TEST_P(UncaughtExceptionsTest, one_uncaught_exception) {
  try {
    Validator<TypeParam> validator(1, "one_uncaught_exception");
    throw std::runtime_error("exception");
  } catch (const std::runtime_error&) {
  }
}

TYPED_TEST_P(UncaughtExceptionsTest, catch_rethrow) {
  try {
    Validator<TypeParam> validatorOuter(1, "catch_rethrow_outer");
    try {
      Validator<TypeParam> validatorInner(1, "catch_rethrow_inner");
      throw std::runtime_error("exception");
    } catch (const std::runtime_error&) {
      EXPECT_EQ(0, folly::uncaught_exceptions());
      Validator<TypeParam> validatorRethrow(1, "catch_rethrow");
      throw;
    }
  } catch (const std::runtime_error&) {
    EXPECT_EQ(0, folly::uncaught_exceptions());
  }
}

template <typename TypeParam>
[[noreturn]] void throwingFunction() {
  Validator<TypeParam> validator(1, "one_uncaught_exception_in_function");
  throw std::runtime_error("exception");
}

TYPED_TEST_P(UncaughtExceptionsTest, one_uncaught_exception_in_function) {
  EXPECT_THROW({ throwingFunction<TypeParam>(); }, std::runtime_error);
}

/*
 * Test helper class. Generates N wrapped classes/objects.
 * The destructor N of the most outer class creates the N-1
 * object, and N - 1 object destructor creating the N-2 object,
 * and so on. Each destructor throws an exception after creating
 * the inner object on the stack, thus the number of exceptions
 * accumulates while the stack is unwinding. It's used to test
 * the folly::uncaught_exceptions() with value >= 2.
 */
template <typename Impl, size_t N, size_t I = N>
struct ThrowInDestructor {
  using InnerThrowInDestructor = ThrowInDestructor<Impl, N, I - 1>;

  Impl impl;

  ThrowInDestructor() {}

  ~ThrowInDestructor() {
    try {
      InnerThrowInDestructor stackObjectThrowingOnUnwind;
      (void)stackObjectThrowingOnUnwind;
      Validator<Impl> validator(
          N - I + 1, "validating in " + folly::to<std::string>(I));
      throw std::logic_error("inner");
    } catch (const std::logic_error&) {
      EXPECT_EQ(N - I, impl());
    }
  }
};

/*
 * Terminate recursion
 */
template <typename Impl, size_t N>
struct ThrowInDestructor<Impl, N, 0> {
  ThrowInDestructor() = default;
  ~ThrowInDestructor() = default;
};

TYPED_TEST_P(UncaughtExceptionsTest, two_uncaught_exceptions) {
  ThrowInDestructor<TypeParam, 2> twoUncaughtExceptions;
}

TYPED_TEST_P(UncaughtExceptionsTest, ten_uncaught_exceptions) {
  ThrowInDestructor<TypeParam, 10> twoUncaughtExceptions;
}

struct ThrowingConstructor {
  [[noreturn]] ThrowingConstructor() noexcept(false) {
    throw std::runtime_error("exception");
  }
};

template <typename Impl>
struct InheritsThrowingConstructor : public Validator<Impl>,
                                     public ThrowingConstructor {
  InheritsThrowingConstructor() try : Validator
    <Impl>(1, "one_exception_in_ctor_initializer_expression"),
        ThrowingConstructor() {}
  catch (...) {
    // This is being re-thrown once the catch block ends, so I guess
    // it's similar to a catch/throw; (re-throw) behavior and thus the value
    // is 0.
    EXPECT_EQ(0, Impl{}());
  }
};

TYPED_TEST_P(
    UncaughtExceptionsTest, one_exception_in_ctor_initializer_expression) {
  EXPECT_THROW(
      { InheritsThrowingConstructor<TypeParam> inheritsThrowingConstructor; },
      std::runtime_error);
}

REGISTER_TYPED_TEST_SUITE_P(
    UncaughtExceptionsTest,
    no_exception,
    no_uncaught_exception,
    one_uncaught_exception,
    catch_rethrow,
    one_uncaught_exception_in_function,
    two_uncaught_exceptions,
    ten_uncaught_exceptions,
    one_exception_in_ctor_initializer_expression);

INSTANTIATE_TYPED_TEST_SUITE_P(std, UncaughtExceptionsTest, invoke_std);
INSTANTIATE_TYPED_TEST_SUITE_P(folly, UncaughtExceptionsTest, invoke_folly);
INSTANTIATE_TYPED_TEST_SUITE_P(
    folly_fb, UncaughtExceptionsTest, invoke_folly_detail_);
