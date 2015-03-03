/*
 * Copyright 2015 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>
#include <stdexcept>
#include <folly/ExceptionWrapper.h>
#include <folly/Conv.h>

using namespace folly;

// Tests that when we call throwException, the proper type is thrown (derived)
TEST(ExceptionWrapper, throw_test) {
  std::runtime_error e("payload");
  auto ew = make_exception_wrapper<std::runtime_error>(e);

  std::vector<exception_wrapper> container;
  container.push_back(ew);

  try {
    container[0].throwException();
  } catch (std::runtime_error& e) {
    std::string expected = "payload";
    std::string actual = e.what();
    EXPECT_EQ(expected, actual);
  }
}

TEST(ExceptionWrapper, members) {
  auto ew = exception_wrapper();
  EXPECT_FALSE(bool(ew));
  EXPECT_EQ(ew.what(), "");
  EXPECT_EQ(ew.class_name(), "");
  ew = make_exception_wrapper<std::runtime_error>("payload");
  EXPECT_TRUE(bool(ew));
  EXPECT_EQ(ew.what(), "std::runtime_error: payload");
  EXPECT_EQ(ew.class_name(), "std::runtime_error");
}

TEST(ExceptionWrapper, equals) {
  std::runtime_error e("payload");
  auto ew1 = make_exception_wrapper<std::runtime_error>(e);
  auto ew2 = ew1;
  EXPECT_EQ(ew1, ew2);

  auto ew3 = try_and_catch<std::exception>([&]() {
    throw std::runtime_error("payload");
  });
  auto ew4 = try_and_catch<std::exception>([&]() {
    ew3.throwException();
  });
  EXPECT_EQ(ew3, ew4);
}

TEST(ExceptionWrapper, not_equals) {
  std::runtime_error e1("payload");
  std::runtime_error e2("payload");
  auto ew1 = make_exception_wrapper<std::runtime_error>(e1);
  auto ew2 = make_exception_wrapper<std::runtime_error>(e2);
  EXPECT_NE(ew1, ew2);

  auto ew3 = make_exception_wrapper<std::runtime_error>(e1);
  auto ew4 = make_exception_wrapper<std::runtime_error>(e1);
  EXPECT_NE(ew3, ew4);

  auto ew5 = try_and_catch<std::exception>([&]() {
    throw e1;
  });
  auto ew6 = try_and_catch<std::exception>([&]() {
    throw e1;
  });
  EXPECT_NE(ew5, ew6);
}

TEST(ExceptionWrapper, try_and_catch_test) {
  std::string expected = "payload";

  // Catch rightmost matching exception type
  exception_wrapper ew = try_and_catch<std::exception, std::runtime_error>(
    [=]() {
      throw std::runtime_error(expected);
    });
  EXPECT_TRUE(bool(ew));
  EXPECT_TRUE(ew.getCopied());
  EXPECT_EQ(ew.what(), "std::runtime_error: payload");
  EXPECT_EQ(ew.class_name(), "std::runtime_error");
  auto rep = ew.is_compatible_with<std::runtime_error>();
  EXPECT_TRUE(rep);

  // Changing order is like catching in wrong order. Beware of this in your
  // code.
  auto ew2 = try_and_catch<std::runtime_error, std::exception>([=]() {
    throw std::runtime_error(expected);
  });
  EXPECT_TRUE(bool(ew2));
  // We are catching a std::exception, not std::runtime_error.
  EXPECT_FALSE(ew2.getCopied());
  // But, we can still get the actual type if we want it.
  rep = ew2.is_compatible_with<std::runtime_error>();
  EXPECT_TRUE(rep);

  // Catches even if not rightmost.
  auto ew3 = try_and_catch<std::exception, std::runtime_error>([]() {
    throw std::exception();
  });
  EXPECT_TRUE(bool(ew3));
  EXPECT_EQ(ew3.what(), "std::exception: std::exception");
  EXPECT_EQ(ew3.class_name(), "std::exception");
  rep = ew3.is_compatible_with<std::runtime_error>();
  EXPECT_FALSE(rep);

  // If does not catch, throws.
  EXPECT_THROW(
    try_and_catch<std::runtime_error>([]() {
      throw std::exception();
    }),
    std::exception);
}

class AbstractIntException : public std::exception {
public:
  virtual int getInt() const = 0;
};

class IntException : public AbstractIntException {
public:
  explicit IntException(int i)
    : i_(i) {}

  virtual int getInt() const override { return i_; }
  virtual const char* what() const noexcept override {
    what_ = folly::to<std::string>("int == ", i_);
    return what_.c_str();
  }

private:
  int i_;
  mutable std::string what_;
};

TEST(ExceptionWrapper, with_exception_test) {
  int expected = 23;

  // This works, and doesn't slice.
  exception_wrapper ew = try_and_catch<std::exception, std::runtime_error>(
    [=]() {
      throw IntException(expected);
    });
  EXPECT_TRUE(bool(ew));
  EXPECT_EQ(ew.what(), "IntException: int == 23");
  EXPECT_EQ(ew.class_name(), "IntException");
  ew.with_exception<IntException>([&](const IntException& ie) {
      EXPECT_EQ(ie.getInt(), expected);
    });

  // I can try_and_catch a non-copyable base class.  This will use
  // std::exception_ptr internally.
  exception_wrapper ew2 = try_and_catch<AbstractIntException>(
    [=]() {
      throw IntException(expected);
    });
  EXPECT_TRUE(bool(ew2));
  EXPECT_EQ(ew2.what(), "IntException: int == 23");
  EXPECT_EQ(ew2.class_name(), "IntException");
  ew2.with_exception<AbstractIntException>([&](AbstractIntException& ie) {
      EXPECT_EQ(ie.getInt(), expected);
#if defined __clang__ && (__clang_major__ > 3 || __clang_minor__ >= 6)
# pragma clang diagnostic push
# pragma clang diagnostic ignored "-Wunevaluated-expression"
#endif
      EXPECT_EQ(typeid(ie), typeid(IntException));
#if defined __clang__ && (__clang_major__ > 3 || __clang_minor__ >= 6)
# pragma clang diagnostic pop
#endif
    });

  // Test with const this.  If this compiles and does not crash due to
  // infinite loop when it runs, it succeeds.
  const exception_wrapper& cew = ew;
  cew.with_exception<IntException>([&](const IntException& ie) {
      SUCCEED();
    });

  // This won't even compile.  You can't use a function which takes a
  // non-const reference with a const exception_wrapper.
/*
  cew.with_exception<IntException>([&](IntException& ie) {
      SUCCEED();
    });
*/
}

TEST(ExceptionWrapper, non_std_exception_test) {
  int expected = 17;

  exception_wrapper ew = try_and_catch<std::exception, int>(
    [=]() {
      throw expected;
    });
  EXPECT_TRUE(bool(ew));
  EXPECT_FALSE(ew.is_compatible_with<std::exception>());
  EXPECT_EQ(ew.what(), "int");
  EXPECT_EQ(ew.class_name(), "int");
  // non-std::exception types are supported, but the only way to
  // access their value is to explicity rethrow and catch it.
  try {
    ew.throwException();
  } catch /* nolint */ (int& i) {
    EXPECT_EQ(i, expected);
  }
}
