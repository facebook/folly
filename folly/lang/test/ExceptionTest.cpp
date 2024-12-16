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

#include <folly/lang/Exception.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <string>

#include <fmt/format.h>

#include <folly/Portability.h>
#include <folly/lang/Align.h>
#include <folly/lang/Keep.h>
#include <folly/lang/Pretty.h>
#include <folly/portability/GTest.h>

using namespace std::literals;

extern "C" FOLLY_KEEP void check_cond_std_terminate(bool c) {
  if (c) {
    std::terminate();
  }
  folly::detail::keep_sink();
}
extern "C" FOLLY_KEEP void check_cond_folly_terminate_with(bool c) {
  if (c) {
    folly::terminate_with<std::runtime_error>("bad error");
  }
  folly::detail::keep_sink();
}
extern "C" FOLLY_KEEP std::exception const* check_get_object_exception(
    std::exception_ptr const& ptr) {
  return folly::exception_ptr_get_object<std::exception>(ptr);
}

extern "C" FOLLY_KEEP void check_cond_catch_exception(bool c) {
  auto try_ = [=] { c ? folly::throw_exception(0) : void(); };
  auto catch_ = []() { folly::detail::keep_sink(); };
  folly::catch_exception(try_, catch_);
}
extern "C" FOLLY_KEEP void check_cond_catch_exception_nx(bool c) {
  auto try_ = [=] { c ? folly::throw_exception(0) : void(); };
  auto catch_ = []() noexcept { folly::detail::keep_sink_nx(); };
  folly::catch_exception(try_, catch_);
}
extern "C" FOLLY_KEEP void check_cond_catch_exception_ptr(bool c) {
  auto try_ = [=] { c ? folly::throw_exception(0) : void(); };
  auto catch_ = folly::detail::keep_sink<>;
  folly::catch_exception(try_, catch_);
}
extern "C" FOLLY_KEEP void check_cond_catch_exception_ptr_nx(bool c) {
  auto try_ = [=] { c ? folly::throw_exception(0) : void(); };
  auto catch_ = folly::detail::keep_sink_nx<>;
  folly::catch_exception(try_, catch_);
}

extern "C" FOLLY_KEEP void check_std_make_exception_ptr(
    std::exception_ptr* ptr) {
  ::new (ptr) std::exception_ptr( //
      std::make_exception_ptr(std::runtime_error("foo")));
}
extern "C" FOLLY_KEEP void check_folly_make_exception_ptr_with_in_place(
    std::exception_ptr* ptr) {
  ::new (ptr) std::exception_ptr(
      folly::make_exception_ptr_with(std::in_place, std::runtime_error("foo")));
}
extern "C" FOLLY_KEEP void check_folly_make_exception_ptr_with_in_place_type(
    std::exception_ptr* ptr) {
  constexpr auto tag = std::in_place_type<std::runtime_error>;
  ::new (ptr) std::exception_ptr( //
      folly::make_exception_ptr_with(tag, "foo"));
}
extern "C" FOLLY_KEEP void check_folly_make_exception_ptr_with_invocable(
    std::exception_ptr* ptr) {
  ::new (ptr) std::exception_ptr(folly::make_exception_ptr_with([] {
    return std::runtime_error("foo");
  }));
}

template <typename Ex>
static std::string message_for_terminate_with(std::string const& what) {
  auto const name = folly::pretty_name<Ex>();
  std::string const p0 = "terminate called after throwing an instance of";
  std::string const p1 = "terminating (due to|with) uncaught exception of type";
  // clang-format off
  return
      folly::kIsGlibcxx ? p0 + " '" + name + "'\\s+what\\(\\):\\s+" + what :
      folly::kIsLibcpp ? p1 + " " + name + ": " + what :
      "" /* empty regex matches anything */;
  // clang-format on
}

static std::string message_for_terminate() {
  // clang-format off
  return
      folly::kIsGlibcxx ? "terminate called without an active exception" :
      folly::kIsLibcpp ? "terminating" :
      "" /* empty regex matches anything */;
  // clang-format on
}

namespace {

template <int I>
struct Virt {
  virtual ~Virt() {}
  int value = I;
  operator int() const { return value; }
};

} // namespace

class MyException : public std::exception {
 private:
  char const* what_;

 public:
  explicit MyException(char const* const what) : MyException(what, 0) {}
  MyException(char const* const what, std::size_t const strip)
      : what_(what + strip) {}

  char const* what() const noexcept override { return what_; }
};

struct MyFmtFormatCStrException : std::exception {
  std::runtime_error inner;
  explicit MyFmtFormatCStrException(char const* const what) noexcept
      : inner{what} {}
  char const* what() const noexcept override { return inner.what(); }
};

struct MyFmtFormatStringException : std::exception {
  std::runtime_error inner;
  explicit MyFmtFormatStringException(std::string&& what) : inner{what} {}
  explicit MyFmtFormatStringException(char const*) : inner{""} {
    ADD_FAILURE();
  }
  char const* what() const noexcept override { return inner.what(); }
};

class ExceptionTest : public testing::Test {};

TEST_F(ExceptionTest, throw_exception_direct) {
  try {
    folly::throw_exception<MyException>("hello world");
    ADD_FAILURE();
  } catch (MyException const& ex) {
    EXPECT_STREQ("hello world", ex.what());
  }
}

TEST_F(ExceptionTest, throw_exception_variadic) {
  try {
    folly::throw_exception<MyException>("hello world", 6);
    ADD_FAILURE();
  } catch (MyException const& ex) {
    EXPECT_STREQ("world", ex.what());
  }
}

TEST_F(ExceptionTest, throw_exception_fmt_format_c_str) {
  try {
    folly::throw_exception_fmt_format<MyFmtFormatCStrException>(
        "{} {}", "hello", "world");
    ADD_FAILURE();
  } catch (MyFmtFormatCStrException const& ex) {
    EXPECT_STREQ("hello world", ex.what());
  }
}

TEST_F(ExceptionTest, throw_exception_fmt_format_string_view) {
  try {
    folly::throw_exception_fmt_format<MyFmtFormatStringException>(
        "{} {}", "hello", "world");
    ADD_FAILURE();
  } catch (MyFmtFormatStringException const& ex) {
    EXPECT_STREQ("hello world", ex.what());
  }
}

TEST_F(ExceptionTest, terminate_with_direct) {
  EXPECT_DEATH(
      folly::terminate_with<MyException>("hello world"),
      message_for_terminate_with<MyException>("hello world"));
}

TEST_F(ExceptionTest, terminate_with_variadic) {
  EXPECT_DEATH(
      folly::terminate_with<MyException>("hello world", 6),
      message_for_terminate_with<MyException>("world"));
}

TEST_F(ExceptionTest, terminate_with_fmt_format_c_str) {
  EXPECT_DEATH(
      folly::terminate_with_fmt_format<MyFmtFormatCStrException>(
          "{} {}", "hello", "world"),
      message_for_terminate_with<MyFmtFormatCStrException>("hello world"));
}

TEST_F(ExceptionTest, terminate_with_fmt_format_string_view) {
  EXPECT_DEATH(
      folly::terminate_with_fmt_format<MyFmtFormatStringException>(
          "{} {}", "hello", "world"),
      message_for_terminate_with<MyFmtFormatStringException>("hello world"));
}

TEST_F(ExceptionTest, invoke_cold) {
  EXPECT_THROW(
      folly::invoke_cold([] { throw std::runtime_error("bad"); }),
      std::runtime_error);
  EXPECT_EQ(7, folly::invoke_cold([] { return 7; }));
}

TEST_F(ExceptionTest, invoke_noreturn_cold) {
  EXPECT_THROW(
      folly::invoke_noreturn_cold([] { throw std::runtime_error("bad"); }),
      std::runtime_error);
  EXPECT_DEATH(folly::invoke_noreturn_cold([] {}), message_for_terminate());
}

TEST_F(ExceptionTest, catch_exception) {
  auto identity = [](int i) { return i; };
  auto returner = [](int i) { return [=] { return i; }; };
  auto thrower = [](int i) { return [=]() -> int { throw i; }; };
  EXPECT_EQ(3, folly::catch_exception(returner(3), returner(4)));
  EXPECT_EQ(3, folly::catch_exception<int>(returner(3), identity));
  EXPECT_EQ(3, folly::catch_exception<int>(returner(3), +identity));
  EXPECT_EQ(3, folly::catch_exception<int>(returner(3), *+identity));
  EXPECT_EQ(4, folly::catch_exception(thrower(3), returner(4)));
  EXPECT_EQ(3, folly::catch_exception<int>(thrower(3), identity));
  EXPECT_EQ(3, folly::catch_exception<int>(thrower(3), +identity));
  EXPECT_EQ(3, folly::catch_exception<int>(thrower(3), *+identity));
}

TEST_F(ExceptionTest, rethrow_current_exception) {
  EXPECT_THROW(
      folly::invoke_noreturn_cold([] {
        try {
          throw std::runtime_error("bad");
        } catch (...) {
          folly::rethrow_current_exception();
        }
      }),
      std::runtime_error);
}

TEST_F(ExceptionTest, uncaught_exception) {
  struct dtor {
    unsigned expected;
    explicit dtor(unsigned e) noexcept : expected{e} {}
    ~dtor() {
      EXPECT_EQ(expected, std::uncaught_exceptions());
      EXPECT_EQ(expected, folly::uncaught_exceptions());
    }
  };
  try {
    dtor obj{0};
  } catch (...) {
  }
  try {
    dtor obj{1};
    throw std::exception();
  } catch (...) {
  }
}

TEST_F(ExceptionTest, current_exception) {
  EXPECT_EQ(nullptr, std::current_exception());
  EXPECT_EQ(nullptr, folly::current_exception());
  try {
    throw std::exception();
  } catch (...) {
    // primary exception?
#if defined(_CPPLIB_VER)
    // As per
    // https://learn.microsoft.com/en-us/cpp/standard-library/exception-functions?view=msvc-170
    // current_exception() returns a new value each time, so its not directly
    // comparable on MSVC
    EXPECT_NE(nullptr, std::current_exception());
    EXPECT_NE(nullptr, folly::current_exception());
#else
    EXPECT_EQ(std::current_exception(), folly::current_exception());
#endif
  }
  try {
    throw std::exception();
  } catch (...) {
    try {
      throw;
    } catch (...) {
      // dependent exception?
#if defined(_CPPLIB_VER)
      EXPECT_NE(nullptr, std::current_exception());
      EXPECT_NE(nullptr, folly::current_exception());
#else
      EXPECT_EQ(std::current_exception(), folly::current_exception());
#endif
    }
  }
}

TEST_F(ExceptionTest, exception_ptr_empty) {
  auto ptr = std::exception_ptr();
  EXPECT_EQ(nullptr, folly::exception_ptr_get_type(ptr));
  EXPECT_EQ(nullptr, folly::exception_ptr_get_object(ptr, nullptr));
  EXPECT_EQ(nullptr, folly::exception_ptr_get_object(ptr, &typeid(long)));
  EXPECT_EQ(nullptr, folly::exception_ptr_get_object(ptr, &typeid(int)));
  EXPECT_EQ(nullptr, folly::exception_ptr_get_object<int>(ptr));
  EXPECT_EQ(nullptr, folly::exception_ptr_get_object(ptr));
}

TEST_F(ExceptionTest, exception_ptr_int) {
  auto ptr = std::make_exception_ptr(17);
  EXPECT_EQ(&typeid(int), folly::exception_ptr_get_type(ptr));
  EXPECT_EQ(17, *(int*)(folly::exception_ptr_get_object(ptr, nullptr)));
  EXPECT_EQ(nullptr, folly::exception_ptr_get_object(ptr, &typeid(long)));
  EXPECT_EQ(17, *(int*)(folly::exception_ptr_get_object(ptr, &typeid(int))));
  EXPECT_EQ(17, *folly::exception_ptr_get_object<int>(ptr));
  EXPECT_EQ(17, *(int*)(folly::exception_ptr_get_object(ptr)));
}

TEST_F(ExceptionTest, exception_ptr_vmi) {
  using A0 = Virt<0>;
  using A1 = Virt<1>;
  using A2 = Virt<2>;
  struct B0 : virtual A1, virtual A2 {};
  struct B1 : virtual A2, virtual A0 {};
  struct B2 : virtual A0, virtual A1 {};
  struct C : B0, B1, B2 {
    int value = 44;
    operator int() const { return value; }
  };

  auto ptr = std::make_exception_ptr(C());
  EXPECT_EQ(&typeid(C), folly::exception_ptr_get_type(ptr));
  EXPECT_EQ(44, *(C*)(folly::exception_ptr_get_object(ptr, nullptr)));
  EXPECT_EQ(nullptr, folly::exception_ptr_get_object(ptr, &typeid(long)));
  EXPECT_EQ(44, *(C*)(folly::exception_ptr_get_object(ptr, &typeid(C))));
  EXPECT_EQ(1, *(A1*)(folly::exception_ptr_get_object(ptr, &typeid(A1))));
  EXPECT_EQ(1, folly::exception_ptr_get_object<A1>(ptr)->value);
  EXPECT_EQ(44, *(C*)(folly::exception_ptr_get_object(ptr)));

  EXPECT_EQ(
      nullptr,
      folly::exception_ptr_try_get_object_exact_fast<A0>(
          nullptr, folly::tag<B1, B2>));
  EXPECT_EQ(
      nullptr,
      folly::exception_ptr_try_get_object_exact_fast<A0>(
          std::make_exception_ptr(17), folly::tag<B1, B2>));
  EXPECT_EQ(
      nullptr,
      folly::exception_ptr_try_get_object_exact_fast<A0>(
          ptr, folly::tag<B1, B2>));
  EXPECT_EQ(
      folly::exception_ptr_get_object<C>(ptr),
      folly::exception_ptr_try_get_object_exact_fast<A0>(
          ptr, folly::tag<B1, C, B2>));

  EXPECT_EQ(
      nullptr,
      folly::exception_ptr_get_object_hint<A0>(nullptr, folly::tag<B1, B2>));
  EXPECT_EQ(
      nullptr,
      folly::exception_ptr_get_object_hint<A0>(
          std::make_exception_ptr(17), folly::tag<B1, B2>));
  EXPECT_EQ(
      folly::exception_ptr_get_object<C>(ptr),
      folly::exception_ptr_get_object_hint<A0>(ptr, folly::tag<B1, B2>));
  EXPECT_EQ(
      folly::exception_ptr_get_object<C>(ptr),
      folly::exception_ptr_get_object_hint<A0>(ptr, folly::tag<B1, C, B2>));
}

TEST_F(ExceptionTest, make_exception_ptr_with_invocable_fail) {
  auto ptr = folly::make_exception_ptr_with( //
      []() -> std::string { throw 17; });
  EXPECT_EQ(&typeid(int), folly::exception_ptr_get_type(ptr));
  EXPECT_EQ(17, *folly::exception_ptr_get_object<int>(ptr));
}

TEST_F(ExceptionTest, make_exception_ptr_with_invocable) {
  auto ptr = folly::make_exception_ptr_with( //
      [] { return std::string("hello world"); });
  EXPECT_EQ(&typeid(std::string), folly::exception_ptr_get_type(ptr));
  EXPECT_EQ("hello world", *folly::exception_ptr_get_object<std::string>(ptr));
}

TEST_F(ExceptionTest, make_exception_ptr_with_in_place_type) {
  auto ptr = folly::make_exception_ptr_with(
      std::in_place_type<std::string>, "hello world");
  EXPECT_EQ(&typeid(std::string), folly::exception_ptr_get_type(ptr));
  EXPECT_EQ("hello world", *folly::exception_ptr_get_object<std::string>(ptr));
}

TEST_F(ExceptionTest, make_exception_ptr_with_in_place) {
  auto ptr = folly::make_exception_ptr_with(std::in_place, 17);
  EXPECT_EQ(&typeid(int), folly::exception_ptr_get_type(ptr));
  EXPECT_EQ(17, *folly::exception_ptr_get_object<int>(ptr));
}

TEST_F(ExceptionTest, exception_shared_string) {
  constexpr auto c = "hello, world!";

  auto s0 = folly::exception_shared_string(c);
  auto s1 = s0;
  auto s2 = s1;
  EXPECT_STREQ(c, s2.what());

  EXPECT_STREQ(c, folly::exception_shared_string(std::string_view(c)).what());
  EXPECT_STREQ(c, folly::exception_shared_string(std::string(c)).what());
}

#if FOLLY_CPLUSPLUS >= 202002

TEST_F(ExceptionTest, exception_shared_string_literal) {
  using namespace folly::string_literals;
  auto s0 = folly::exception_shared_string("hello, world!"_litv);
  auto s1 = s0;
  auto s2 = s1;
  EXPECT_STREQ("hello, world!", s2.what());
}

#endif
// example of how to do the in-place formatting efficiently
struct format_param_fn {
  template <typename A>
  using arg_t = folly::conditional_t<folly::is_register_pass_v<A>, A, A const&>;

  template <typename... A>
  auto operator()(
      fmt::format_string<arg_t<A>...> const& fmt, A const&... arg) const {
    return std::pair{
        fmt::formatted_size(fmt, static_cast<arg_t<A>>(arg)...),
        [&](auto buf, auto len) {
          auto res =
              fmt::format_to_n(buf, len, fmt, static_cast<arg_t<A>>(arg)...);
          FOLLY_SAFE_DCHECK(len == res.size);
        }};
  }
};
inline constexpr format_param_fn format_param{};

TEST_F(ExceptionTest, exception_shared_string_format) {
  auto s = std::invoke(
      [](auto p) { return folly::exception_shared_string(p.first, p.second); },
      format_param("a number {} and a string {}", 217, "flobber"s));
  EXPECT_STREQ("a number 217 and a string flobber", s.what());
}
