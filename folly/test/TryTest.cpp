/*
 * Copyright 2014-present Facebook, Inc.
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

#include <folly/Try.h>

#include <glog/logging.h>

#include <folly/Memory.h>
#include <folly/Traits.h>
#include <folly/portability/GTest.h>

using namespace folly;

namespace {

class A {
 public:
  explicit A(int x) : x_(x) {}

  int x() const {
    return x_;
  }
 private:
  int x_;
};

class MoveConstructOnly {
 public:
  MoveConstructOnly() = default;
  MoveConstructOnly(const MoveConstructOnly&) = delete;
  MoveConstructOnly(MoveConstructOnly&&) = default;
};

class MutableContainer {
 public:
  mutable MoveConstructOnly val;
};
} // namespace

TEST(Try, basic) {
  A a(5);
  Try<A> t_a(std::move(a));

  Try<Unit> t_void;

  EXPECT_EQ(5, t_a.value().x());
}

TEST(Try, in_place) {
  Try<A> t_a(in_place, 5);

  EXPECT_EQ(5, t_a.value().x());
}

TEST(Try, in_place_nested) {
  Try<Try<A>> t_t_a(in_place, in_place, 5);

  EXPECT_EQ(5, t_t_a.value().value().x());
}

TEST(Try, MoveDereference) {
  auto ptr = std::make_unique<int>(1);
  auto t = Try<std::unique_ptr<int>>{std::move(ptr)};
  auto result = *std::move(t);
  EXPECT_EQ(*result, 1);
}

TEST(Try, MoveConstRvalue) {
  // tests to see if Try returns a const Rvalue, this is required in the case
  // where for example MutableContainer has a mutable memebr that is move only
  // and you want to fetch the value from the Try and move it into a member
  {
    const Try<MutableContainer> t{in_place};
    auto val = MoveConstructOnly(std::move(t).value().val);
    static_cast<void>(val);
  }
  {
    const Try<MutableContainer> t{in_place};
    auto val = (*(std::move(t))).val;
    static_cast<void>(val);
  }
}

TEST(Try, ValueOverloads) {
  using ML = int&;
  using MR = int&&;
  using CL = const int&;
  using CR = const int&&;

  {
    auto obj = Try<int>{};
    using ActualML = decltype(obj.value());
    using ActualMR = decltype(std::move(obj).value());
    using ActualCL = decltype(as_const(obj).value());
    using ActualCR = decltype(std::move(as_const(obj)).value());
    EXPECT_TRUE((std::is_same<ML, ActualML>::value));
    EXPECT_TRUE((std::is_same<MR, ActualMR>::value));
    EXPECT_TRUE((std::is_same<CL, ActualCL>::value));
    EXPECT_TRUE((std::is_same<CR, ActualCR>::value));
  }

  {
    auto obj = Try<int>{3};
    EXPECT_EQ(obj.value(), 3);
    EXPECT_EQ(std::move(obj).value(), 3);
    EXPECT_EQ(as_const(obj).value(), 3);
    EXPECT_EQ(std::move(as_const(obj)).value(), 3);
  }

  {
    auto obj = Try<int>{make_exception_wrapper<std::range_error>("oops")};
    EXPECT_THROW(obj.value(), std::range_error);
    EXPECT_THROW(std::move(obj.value()), std::range_error);
    EXPECT_THROW(as_const(obj.value()), std::range_error);
    EXPECT_THROW(std::move(as_const(obj.value())), std::range_error);
  }
}

// Make sure we can copy Trys for copyable types
TEST(Try, copy) {
  Try<int> t;
  auto t2 = t;
}

// But don't choke on move-only types
TEST(Try, moveOnly) {
  Try<std::unique_ptr<int>> t;
  std::vector<Try<std::unique_ptr<int>>> v;
  v.reserve(10);
}

TEST(Try, makeTryWith) {
  auto func = []() {
    return std::make_unique<int>(1);
  };

  auto result = makeTryWith(func);
  EXPECT_TRUE(result.hasValue());
  EXPECT_EQ(*result.value(), 1);
}

TEST(Try, makeTryWithThrow) {
  auto func = []() -> std::unique_ptr<int> {
    throw std::runtime_error("Runtime");
  };

  auto result = makeTryWith(func);
  EXPECT_TRUE(result.hasException<std::runtime_error>());
}

TEST(Try, makeTryWithVoid) {
  auto func = []() {
    return;
  };

  auto result = makeTryWith(func);
  EXPECT_TRUE(result.hasValue());
}

TEST(Try, makeTryWithVoidThrow) {
  auto func = []() {
    throw std::runtime_error("Runtime");
  };

  auto result = makeTryWith(func);
  EXPECT_TRUE(result.hasException<std::runtime_error>());
}

TEST(Try, exception) {
  using ML = exception_wrapper&;
  using MR = exception_wrapper&&;
  using CL = exception_wrapper const&;
  using CR = exception_wrapper const&&;

  {
    auto obj = Try<int>();
    using ActualML = decltype(obj.exception());
    using ActualMR = decltype(std::move(obj).exception());
    using ActualCL = decltype(as_const(obj).exception());
    using ActualCR = decltype(std::move(as_const(obj)).exception());
    EXPECT_TRUE((std::is_same<ML, ActualML>::value));
    EXPECT_TRUE((std::is_same<MR, ActualMR>::value));
    EXPECT_TRUE((std::is_same<CL, ActualCL>::value));
    EXPECT_TRUE((std::is_same<CR, ActualCR>::value));
  }

  {
    auto obj = Try<int>(3);
    EXPECT_THROW(obj.exception(), TryException);
    EXPECT_THROW(std::move(obj).exception(), TryException);
    EXPECT_THROW(as_const(obj).exception(), TryException);
    EXPECT_THROW(std::move(as_const(obj)).exception(), TryException);
  }

  {
    auto obj = Try<int>(make_exception_wrapper<int>(-3));
    EXPECT_EQ(-3, *obj.exception().get_exception<int>());
    EXPECT_EQ(-3, *std::move(obj).exception().get_exception<int>());
    EXPECT_EQ(-3, *as_const(obj).exception().get_exception<int>());
    EXPECT_EQ(-3, *std::move(as_const(obj)).exception().get_exception<int>());
  }

  {
    auto obj = Try<void>();
    using ActualML = decltype(obj.exception());
    using ActualMR = decltype(std::move(obj).exception());
    using ActualCL = decltype(as_const(obj).exception());
    using ActualCR = decltype(std::move(as_const(obj)).exception());
    EXPECT_TRUE((std::is_same<ML, ActualML>::value));
    EXPECT_TRUE((std::is_same<MR, ActualMR>::value));
    EXPECT_TRUE((std::is_same<CL, ActualCL>::value));
    EXPECT_TRUE((std::is_same<CR, ActualCR>::value));
  }

  {
    auto obj = Try<void>();
    EXPECT_THROW(obj.exception(), TryException);
    EXPECT_THROW(std::move(obj).exception(), TryException);
    EXPECT_THROW(as_const(obj).exception(), TryException);
    EXPECT_THROW(std::move(as_const(obj)).exception(), TryException);
  }

  {
    auto obj = Try<void>(make_exception_wrapper<int>(-3));
    EXPECT_EQ(-3, *obj.exception().get_exception<int>());
    EXPECT_EQ(-3, *std::move(obj).exception().get_exception<int>());
    EXPECT_EQ(-3, *as_const(obj).exception().get_exception<int>());
    EXPECT_EQ(-3, *std::move(as_const(obj)).exception().get_exception<int>());
  }
}

template <typename E>
static E* get_exception(std::exception_ptr eptr) {
  try {
    std::rethrow_exception(eptr);
  } catch (E& e) {
    return &e;
  } catch (...) {
    return nullptr;
  }
}

TEST(Try, tryGetExceptionObject) {
  auto epexn = std::make_exception_ptr(std::range_error("oops"));
  auto epnum = std::make_exception_ptr(17);

  auto exn = CHECK_NOTNULL(get_exception<std::range_error>(epexn));
  auto num = CHECK_NOTNULL(get_exception<int>(epnum));

  {
    auto t = Try<bool>(true);
    EXPECT_EQ(nullptr, t.tryGetExceptionObject());
    EXPECT_EQ(nullptr, t.tryGetExceptionObject<std::runtime_error>());
    EXPECT_EQ(nullptr, t.tryGetExceptionObject<int>());
  }

  {
    auto t = Try<bool>(exception_wrapper(epexn, *exn));
    EXPECT_EQ(exn, t.tryGetExceptionObject());
    EXPECT_EQ(exn, t.tryGetExceptionObject<std::runtime_error>());
    EXPECT_EQ(nullptr, t.tryGetExceptionObject<int>());
  }

  {
    auto t = Try<bool>(exception_wrapper(epnum, *num));
    EXPECT_EQ(nullptr, t.tryGetExceptionObject());
    EXPECT_EQ(nullptr, t.tryGetExceptionObject<std::runtime_error>());
    EXPECT_EQ(num, t.tryGetExceptionObject<int>());
  }

  {
    auto t = Try<void>();
    EXPECT_EQ(nullptr, t.tryGetExceptionObject());
    EXPECT_EQ(nullptr, t.tryGetExceptionObject<std::runtime_error>());
    EXPECT_EQ(nullptr, t.tryGetExceptionObject<int>());
  }

  {
    auto t = Try<void>(exception_wrapper(epexn, *exn));
    EXPECT_EQ(exn, t.tryGetExceptionObject());
    EXPECT_EQ(exn, t.tryGetExceptionObject<std::runtime_error>());
    EXPECT_EQ(nullptr, t.tryGetExceptionObject<int>());
  }

  {
    auto t = Try<void>(exception_wrapper(epnum, *num));
    EXPECT_EQ(nullptr, t.tryGetExceptionObject());
    EXPECT_EQ(nullptr, t.tryGetExceptionObject<std::runtime_error>());
    EXPECT_EQ(num, t.tryGetExceptionObject<int>());
  }

  {
    auto const t = Try<bool>(true);
    EXPECT_EQ(nullptr, t.tryGetExceptionObject());
    EXPECT_EQ(nullptr, t.tryGetExceptionObject<std::runtime_error>());
    EXPECT_EQ(nullptr, t.tryGetExceptionObject<int>());
  }

  {
    auto const t = Try<bool>(exception_wrapper(epexn, *exn));
    EXPECT_EQ(exn, t.tryGetExceptionObject());
    EXPECT_EQ(exn, t.tryGetExceptionObject<std::runtime_error>());
    EXPECT_EQ(nullptr, t.tryGetExceptionObject<int>());
  }

  {
    auto const t = Try<bool>(exception_wrapper(epnum, *num));
    EXPECT_EQ(nullptr, t.tryGetExceptionObject());
    EXPECT_EQ(nullptr, t.tryGetExceptionObject<std::runtime_error>());
    EXPECT_EQ(num, t.tryGetExceptionObject<int>());
  }

  {
    auto const t = Try<void>();
    EXPECT_EQ(nullptr, t.tryGetExceptionObject());
    EXPECT_EQ(nullptr, t.tryGetExceptionObject<std::runtime_error>());
    EXPECT_EQ(nullptr, t.tryGetExceptionObject<int>());
  }

  {
    auto const t = Try<void>(exception_wrapper(epexn, *exn));
    EXPECT_EQ(exn, t.tryGetExceptionObject());
    EXPECT_EQ(exn, t.tryGetExceptionObject<std::runtime_error>());
    EXPECT_EQ(nullptr, t.tryGetExceptionObject<int>());
  }

  {
    auto const t = Try<void>(exception_wrapper(epnum, *num));
    EXPECT_EQ(nullptr, t.tryGetExceptionObject());
    EXPECT_EQ(nullptr, t.tryGetExceptionObject<std::runtime_error>());
    EXPECT_EQ(num, t.tryGetExceptionObject<int>());
  }
}

TEST(Try, withException) {
  auto ew = make_exception_wrapper<std::range_error>("oops");

  {
    auto t = Try<bool>(true);
    EXPECT_FALSE(t.withException<std::runtime_error>([](auto&) {}));
    EXPECT_FALSE(t.withException<std::logic_error>([](auto&) {}));
    EXPECT_FALSE(t.withException([](std::runtime_error&) {}));
    EXPECT_FALSE(t.withException([](std::logic_error&) {}));
  }

  {
    auto t = Try<bool>(ew);
    EXPECT_TRUE(t.withException<std::runtime_error>([](auto&) {}));
    EXPECT_FALSE(t.withException<std::logic_error>([](auto&) {}));
    EXPECT_TRUE(t.withException([](std::runtime_error&) {}));
    EXPECT_FALSE(t.withException([](std::logic_error&) {}));
  }

  {
    auto t = Try<void>();
    EXPECT_FALSE(t.withException<std::runtime_error>([](auto&) {}));
    EXPECT_FALSE(t.withException<std::logic_error>([](auto&) {}));
    EXPECT_FALSE(t.withException([](std::runtime_error&) {}));
    EXPECT_FALSE(t.withException([](std::logic_error&) {}));
  }

  {
    auto t = Try<void>(ew);
    EXPECT_TRUE(t.withException<std::runtime_error>([](auto&) {}));
    EXPECT_FALSE(t.withException<std::logic_error>([](auto&) {}));
    EXPECT_TRUE(t.withException([](std::runtime_error&) {}));
    EXPECT_FALSE(t.withException([](std::logic_error&) {}));
  }

  {
    auto const t = Try<bool>(true);
    EXPECT_FALSE(t.withException<std::runtime_error>([](auto&) {}));
    EXPECT_FALSE(t.withException<std::logic_error>([](auto&) {}));
    EXPECT_FALSE(t.withException([](std::runtime_error const&) {}));
    EXPECT_FALSE(t.withException([](std::logic_error const&) {}));
  }

  {
    auto const t = Try<bool>(ew);
    EXPECT_TRUE(t.withException<std::runtime_error>([](auto&) {}));
    EXPECT_FALSE(t.withException<std::logic_error>([](auto&) {}));
    EXPECT_TRUE(t.withException([](std::runtime_error const&) {}));
    EXPECT_FALSE(t.withException([](std::logic_error const&) {}));
  }

  {
    auto const t = Try<void>();
    EXPECT_FALSE(t.withException<std::runtime_error>([](auto&) {}));
    EXPECT_FALSE(t.withException<std::logic_error>([](auto&) {}));
    EXPECT_FALSE(t.withException([](std::runtime_error const&) {}));
    EXPECT_FALSE(t.withException([](std::logic_error const&) {}));
  }

  {
    auto const t = Try<void>(ew);
    EXPECT_TRUE(t.withException<std::runtime_error>([](auto&) {}));
    EXPECT_FALSE(t.withException<std::logic_error>([](auto&) {}));
    EXPECT_TRUE(t.withException([](std::runtime_error const&) {}));
    EXPECT_FALSE(t.withException([](std::logic_error const&) {}));
  }
}

TEST(Try, TestUnwrapTuple) {
  auto original = std::make_tuple(Try<int>{1}, Try<int>{2});
  EXPECT_EQ(std::make_tuple(1, 2), unwrapTryTuple(original));
  EXPECT_EQ(std::make_tuple(1, 2), unwrapTryTuple(folly::copy(original)));
  EXPECT_EQ(std::make_tuple(1, 2), unwrapTryTuple(folly::as_const(original)));
}

TEST(Try, TestUnwrapPair) {
  auto original = std::make_pair(Try<int>{1}, Try<int>{2});
  EXPECT_EQ(std::make_pair(1, 2), unwrapTryTuple(original));
  EXPECT_EQ(std::make_pair(1, 2), unwrapTryTuple(folly::copy(original)));
  EXPECT_EQ(std::make_pair(1, 2), unwrapTryTuple(folly::as_const(original)));
}

TEST(Try, TestUnwrapForward) {
  using UPtr_t = std::unique_ptr<int>;
  auto original = std::make_tuple(Try<UPtr_t>{std::make_unique<int>(1)});
  auto unwrapped = unwrapTryTuple(std::move(original));
  EXPECT_EQ(*std::get<0>(unwrapped), 1);
}
