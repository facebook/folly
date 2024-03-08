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

#include <folly/functional/traits.h>

#include <folly/portability/GTest.h>

struct TraitsTest : testing::Test {
  template <typename... T>
  static constexpr bool same_v = std::is_same<T...>::value;

  template <typename S>
  using traits = folly::function_traits<S>;

  template <typename S>
  using result_type_t = typename traits<S>::result_type;

  template <typename R, typename S>
  static constexpr bool is_result_type_v =
      std::is_same<R, result_type_t<S>>::value;

  template <typename S>
  static constexpr bool is_nothrow_v = traits<S>::is_nothrow;

  template <size_t I, typename S>
  using argument_type_t = typename traits<S>::template argument<I>;

  template <typename R, size_t I, typename S>
  static constexpr bool is_arg_type_v =
      std::is_same<R, argument_type_t<I, S>>::value;

  template <typename S>
  static constexpr size_t args_size_v =
      traits<S>::template arguments<folly::type_pack_size_t>::value;

  template <typename S>
  static constexpr bool is_variadic_v = traits<S>::is_variadic;

  template <typename T, typename S>
  using cvref_t = typename traits<S>::template value_like<T>;

  template <typename D, typename T, typename S>
  static constexpr bool is_cvref_t = std::is_same<D, cvref_t<T, S>>::value;

  template <typename F>
  using rem_cvref_t = folly::function_remove_cvref_t<F>;

  template <typename D, typename S, typename I>
  static constexpr bool is_like_v =
      same_v<D, folly::function_like_value_t<S, I>>;
};

TEST_F(TraitsTest, function_traits) {
  using vc = void const;

  //  result_type

  EXPECT_TRUE((is_result_type_v<vc, vc()>));
  EXPECT_TRUE((is_result_type_v<vc, vc() const>));
  EXPECT_TRUE((is_result_type_v<vc, vc() volatile>));
  EXPECT_TRUE((is_result_type_v<vc, vc() const volatile>));
  EXPECT_TRUE((is_result_type_v<vc, vc()&>));
  EXPECT_TRUE((is_result_type_v<vc, vc() const&>));
  EXPECT_TRUE((is_result_type_v<vc, vc() volatile&>));
  EXPECT_TRUE((is_result_type_v<vc, vc() const volatile&>));
  EXPECT_TRUE((is_result_type_v<vc, vc() &&>));
  EXPECT_TRUE((is_result_type_v<vc, vc() const&&>));
  EXPECT_TRUE((is_result_type_v<vc, vc() volatile&&>));
  EXPECT_TRUE((is_result_type_v<vc, vc() const volatile&&>));
  EXPECT_TRUE((is_result_type_v<vc, vc(...)>));
  EXPECT_TRUE((is_result_type_v<vc, vc(...) const>));
  EXPECT_TRUE((is_result_type_v<vc, vc(...) volatile>));
  EXPECT_TRUE((is_result_type_v<vc, vc(...) const volatile>));
  EXPECT_TRUE((is_result_type_v<vc, vc(...)&>));
  EXPECT_TRUE((is_result_type_v<vc, vc(...) const&>));
  EXPECT_TRUE((is_result_type_v<vc, vc(...) volatile&>));
  EXPECT_TRUE((is_result_type_v<vc, vc(...) const volatile&>));
  EXPECT_TRUE((is_result_type_v<vc, vc(...) &&>));
  EXPECT_TRUE((is_result_type_v<vc, vc(...) const&&>));
  EXPECT_TRUE((is_result_type_v<vc, vc(...) volatile&&>));
  EXPECT_TRUE((is_result_type_v<vc, vc(...) const volatile&&>));
  EXPECT_TRUE((is_result_type_v<vc, vc() noexcept>));
  EXPECT_TRUE((is_result_type_v<vc, vc() const noexcept>));
  EXPECT_TRUE((is_result_type_v<vc, vc() volatile noexcept>));
  EXPECT_TRUE((is_result_type_v<vc, vc() const volatile noexcept>));
  EXPECT_TRUE((is_result_type_v<vc, vc()& noexcept>));
  EXPECT_TRUE((is_result_type_v<vc, vc() const& noexcept>));
  EXPECT_TRUE((is_result_type_v<vc, vc() volatile& noexcept>));
  EXPECT_TRUE((is_result_type_v<vc, vc() const volatile& noexcept>));
  EXPECT_TRUE((is_result_type_v<vc, vc()&& noexcept>));
  EXPECT_TRUE((is_result_type_v<vc, vc() const&& noexcept>));
  EXPECT_TRUE((is_result_type_v<vc, vc() volatile&& noexcept>));
  EXPECT_TRUE((is_result_type_v<vc, vc() const volatile&& noexcept>));
  EXPECT_TRUE((is_result_type_v<vc, vc(...) noexcept>));
  EXPECT_TRUE((is_result_type_v<vc, vc(...) const noexcept>));
  EXPECT_TRUE((is_result_type_v<vc, vc(...) volatile noexcept>));
  EXPECT_TRUE((is_result_type_v<vc, vc(...) const volatile noexcept>));
  EXPECT_TRUE((is_result_type_v<vc, vc(...)& noexcept>));
  EXPECT_TRUE((is_result_type_v<vc, vc(...) const& noexcept>));
  EXPECT_TRUE((is_result_type_v<vc, vc(...) volatile& noexcept>));
  EXPECT_TRUE((is_result_type_v<vc, vc(...) const volatile& noexcept>));
  EXPECT_TRUE((is_result_type_v<vc, vc(...)&& noexcept>));
  EXPECT_TRUE((is_result_type_v<vc, vc(...) const&& noexcept>));
  EXPECT_TRUE((is_result_type_v<vc, vc(...) volatile&& noexcept>));
  EXPECT_TRUE((is_result_type_v<vc, vc(...) const volatile&& noexcept>));

  //  is_nothrow

  EXPECT_FALSE((is_nothrow_v<void()>));
  EXPECT_FALSE((is_nothrow_v<void() const>));
  EXPECT_FALSE((is_nothrow_v<void() volatile>));
  EXPECT_FALSE((is_nothrow_v<void() const volatile>));
  EXPECT_FALSE((is_nothrow_v<void()&>));
  EXPECT_FALSE((is_nothrow_v<void() const&>));
  EXPECT_FALSE((is_nothrow_v<void() volatile&>));
  EXPECT_FALSE((is_nothrow_v<void() const volatile&>));
  EXPECT_FALSE((is_nothrow_v<void() &&>));
  EXPECT_FALSE((is_nothrow_v<void() const&&>));
  EXPECT_FALSE((is_nothrow_v<void() volatile&&>));
  EXPECT_FALSE((is_nothrow_v<void() const volatile&&>));
  EXPECT_FALSE((is_nothrow_v<void(...)>));
  EXPECT_FALSE((is_nothrow_v<void(...) const>));
  EXPECT_FALSE((is_nothrow_v<void(...) volatile>));
  EXPECT_FALSE((is_nothrow_v<void(...) const volatile>));
  EXPECT_FALSE((is_nothrow_v<void(...)&>));
  EXPECT_FALSE((is_nothrow_v<void(...) const&>));
  EXPECT_FALSE((is_nothrow_v<void(...) volatile&>));
  EXPECT_FALSE((is_nothrow_v<void(...) const volatile&>));
  EXPECT_FALSE((is_nothrow_v<void(...) &&>));
  EXPECT_FALSE((is_nothrow_v<void(...) const&&>));
  EXPECT_FALSE((is_nothrow_v<void(...) volatile&&>));
  EXPECT_FALSE((is_nothrow_v<void(...) const volatile&&>));
  EXPECT_TRUE((is_nothrow_v<void() noexcept>));
  EXPECT_TRUE((is_nothrow_v<void() const noexcept>));
  EXPECT_TRUE((is_nothrow_v<void() volatile noexcept>));
  EXPECT_TRUE((is_nothrow_v<void() const volatile noexcept>));
  EXPECT_TRUE((is_nothrow_v<void()& noexcept>));
  EXPECT_TRUE((is_nothrow_v<void() const& noexcept>));
  EXPECT_TRUE((is_nothrow_v<void() volatile& noexcept>));
  EXPECT_TRUE((is_nothrow_v<void() const volatile& noexcept>));
  EXPECT_TRUE((is_nothrow_v<void()&& noexcept>));
  EXPECT_TRUE((is_nothrow_v<void() const&& noexcept>));
  EXPECT_TRUE((is_nothrow_v<void() volatile&& noexcept>));
  EXPECT_TRUE((is_nothrow_v<void() const volatile&& noexcept>));
  EXPECT_TRUE((is_nothrow_v<void(...) noexcept>));
  EXPECT_TRUE((is_nothrow_v<void(...) const noexcept>));
  EXPECT_TRUE((is_nothrow_v<void(...) volatile noexcept>));
  EXPECT_TRUE((is_nothrow_v<void(...) const volatile noexcept>));
  EXPECT_TRUE((is_nothrow_v<void(...)& noexcept>));
  EXPECT_TRUE((is_nothrow_v<void(...) const& noexcept>));
  EXPECT_TRUE((is_nothrow_v<void(...) volatile& noexcept>));
  EXPECT_TRUE((is_nothrow_v<void(...) const volatile& noexcept>));
  EXPECT_TRUE((is_nothrow_v<void(...)&& noexcept>));
  EXPECT_TRUE((is_nothrow_v<void(...) const&& noexcept>));
  EXPECT_TRUE((is_nothrow_v<void(...) volatile&& noexcept>));
  EXPECT_TRUE((is_nothrow_v<void(...) const volatile&& noexcept>));

  //  variadic

  EXPECT_FALSE((is_variadic_v<void()>));
  EXPECT_FALSE((is_variadic_v<void() const>));
  EXPECT_FALSE((is_variadic_v<void() volatile>));
  EXPECT_FALSE((is_variadic_v<void() const volatile>));
  EXPECT_FALSE((is_variadic_v<void()&>));
  EXPECT_FALSE((is_variadic_v<void() const&>));
  EXPECT_FALSE((is_variadic_v<void() volatile&>));
  EXPECT_FALSE((is_variadic_v<void() const volatile&>));
  EXPECT_FALSE((is_variadic_v<void() &&>));
  EXPECT_FALSE((is_variadic_v<void() const&&>));
  EXPECT_FALSE((is_variadic_v<void() volatile&&>));
  EXPECT_FALSE((is_variadic_v<void() const volatile&&>));
  EXPECT_TRUE((is_variadic_v<void(...)>));
  EXPECT_TRUE((is_variadic_v<void(...) const>));
  EXPECT_TRUE((is_variadic_v<void(...) volatile>));
  EXPECT_TRUE((is_variadic_v<void(...) const volatile>));
  EXPECT_TRUE((is_variadic_v<void(...)&>));
  EXPECT_TRUE((is_variadic_v<void(...) const&>));
  EXPECT_TRUE((is_variadic_v<void(...) volatile&>));
  EXPECT_TRUE((is_variadic_v<void(...) const volatile&>));
  EXPECT_TRUE((is_variadic_v<void(...) &&>));
  EXPECT_TRUE((is_variadic_v<void(...) const&&>));
  EXPECT_TRUE((is_variadic_v<void(...) volatile&&>));
  EXPECT_TRUE((is_variadic_v<void(...) const volatile&&>));
  EXPECT_FALSE((is_variadic_v<void() noexcept>));
  EXPECT_FALSE((is_variadic_v<void() const noexcept>));
  EXPECT_FALSE((is_variadic_v<void() volatile noexcept>));
  EXPECT_FALSE((is_variadic_v<void() const volatile noexcept>));
  EXPECT_FALSE((is_variadic_v<void()& noexcept>));
  EXPECT_FALSE((is_variadic_v<void() const& noexcept>));
  EXPECT_FALSE((is_variadic_v<void() volatile& noexcept>));
  EXPECT_FALSE((is_variadic_v<void() const volatile& noexcept>));
  EXPECT_FALSE((is_variadic_v<void()&& noexcept>));
  EXPECT_FALSE((is_variadic_v<void() const&& noexcept>));
  EXPECT_FALSE((is_variadic_v<void() volatile&& noexcept>));
  EXPECT_FALSE((is_variadic_v<void() const volatile&& noexcept>));
  EXPECT_TRUE((is_variadic_v<void(...) noexcept>));
  EXPECT_TRUE((is_variadic_v<void(...) const noexcept>));
  EXPECT_TRUE((is_variadic_v<void(...) volatile noexcept>));
  EXPECT_TRUE((is_variadic_v<void(...) const volatile noexcept>));
  EXPECT_TRUE((is_variadic_v<void(...)& noexcept>));
  EXPECT_TRUE((is_variadic_v<void(...) const& noexcept>));
  EXPECT_TRUE((is_variadic_v<void(...) volatile& noexcept>));
  EXPECT_TRUE((is_variadic_v<void(...) const volatile& noexcept>));
  EXPECT_TRUE((is_variadic_v<void(...)&& noexcept>));
  EXPECT_TRUE((is_variadic_v<void(...) const&& noexcept>));
  EXPECT_TRUE((is_variadic_v<void(...) volatile&& noexcept>));
  EXPECT_TRUE((is_variadic_v<void(...) const volatile&& noexcept>));

  //  argument

  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*)>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*) const>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*) volatile>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*) const volatile>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*)&>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*) const&>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*) volatile&>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*) const volatile&>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*) &&>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*) const&&>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*) volatile&&>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*) const volatile&&>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*, ...)>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*, ...) const>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*, ...) volatile>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*, ...) const volatile>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*, ...)&>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*, ...) const&>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*, ...) volatile&>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*, ...) const volatile&>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*, ...) &&>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*, ...) const&&>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*, ...) volatile&&>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*, ...) const volatile&&>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*) noexcept>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*) const noexcept>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*) volatile noexcept>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*) const volatile noexcept>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*)& noexcept>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*) const& noexcept>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*) volatile& noexcept>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*) const volatile& noexcept>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*)&& noexcept>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*) const&& noexcept>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*) volatile&& noexcept>));
  EXPECT_TRUE(( //
      is_arg_type_v<vc*, 1, void(int, vc*) const volatile&& noexcept>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*, ...) noexcept>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*, ...) const noexcept>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*, ...) volatile noexcept>));
  EXPECT_TRUE(( //
      is_arg_type_v<vc*, 1, void(int, vc*, ...) const volatile noexcept>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*, ...)& noexcept>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*, ...) const& noexcept>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*, ...) volatile& noexcept>));
  EXPECT_TRUE(( //
      is_arg_type_v<vc*, 1, void(int, vc*, ...) const volatile& noexcept>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*, ...)&& noexcept>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*, ...) const&& noexcept>));
  EXPECT_TRUE((is_arg_type_v<vc*, 1, void(int, vc*, ...) volatile&& noexcept>));
  EXPECT_TRUE(( //
      is_arg_type_v<vc*, 1, void(int, vc*, ...) const volatile&& noexcept>));

  //  arguments

  EXPECT_EQ(3, (args_size_v<void(int, vc*, float)>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float) const>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float) volatile>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float) const volatile>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float)&>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float) const&>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float) volatile&>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float) const volatile&>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float) &&>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float) const&&>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float) volatile&&>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float) const volatile&&>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float, ...)>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float, ...) const>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float, ...) volatile>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float, ...) const volatile>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float, ...)&>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float, ...) const&>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float, ...) volatile&>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float, ...) const volatile&>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float, ...) &&>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float, ...) const&&>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float, ...) volatile&&>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float, ...) const volatile&&>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float) noexcept>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float) const noexcept>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float) volatile noexcept>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float) const volatile noexcept>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float)& noexcept>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float) const& noexcept>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float) volatile& noexcept>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float) const volatile& noexcept>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float)&& noexcept>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float) const&& noexcept>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float) volatile&& noexcept>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float) const volatile&& noexcept>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float, ...) noexcept>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float, ...) const noexcept>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float, ...) volatile noexcept>));
  EXPECT_EQ(
      3, (args_size_v<void(int, vc*, float, ...) const volatile noexcept>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float, ...)& noexcept>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float, ...) const& noexcept>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float, ...) volatile& noexcept>));
  EXPECT_EQ(
      3, (args_size_v<void(int, vc*, float, ...) const volatile& noexcept>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float, ...)&& noexcept>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float, ...) const&& noexcept>));
  EXPECT_EQ(3, (args_size_v<void(int, vc*, float, ...) volatile&& noexcept>));
  EXPECT_EQ(
      3, (args_size_v<void(int, vc*, float, ...) const volatile&& noexcept>));

  //  value_like

  EXPECT_TRUE((is_cvref_t<int, int, void()>));
  EXPECT_TRUE((is_cvref_t<int const, int, void() const>));
  EXPECT_TRUE((is_cvref_t<int volatile, int, void() volatile>));
  EXPECT_TRUE((is_cvref_t<int const volatile, int, void() const volatile>));
  EXPECT_TRUE((is_cvref_t<int&, int, void()&>));
  EXPECT_TRUE((is_cvref_t<int const&, int, void() const&>));
  EXPECT_TRUE((is_cvref_t<int volatile&, int, void() volatile&>));
  EXPECT_TRUE((is_cvref_t<int const volatile&, int, void() const volatile&>));
  EXPECT_TRUE((is_cvref_t<int&&, int, void() &&>));
  EXPECT_TRUE((is_cvref_t<int const&&, int, void() const&&>));
  EXPECT_TRUE((is_cvref_t<int volatile&&, int, void() volatile&&>));
  EXPECT_TRUE((is_cvref_t<int const volatile&&, int, void() const volatile&&>));
  EXPECT_TRUE((is_cvref_t<int, int, void(...)>));
  EXPECT_TRUE((is_cvref_t<int const, int, void(...) const>));
  EXPECT_TRUE((is_cvref_t<int volatile, int, void(...) volatile>));
  EXPECT_TRUE((is_cvref_t<int const volatile, int, void(...) const volatile>));
  EXPECT_TRUE((is_cvref_t<int&, int, void(...)&>));
  EXPECT_TRUE((is_cvref_t<int const&, int, void(...) const&>));
  EXPECT_TRUE((is_cvref_t<int volatile&, int, void(...) volatile&>));
  EXPECT_TRUE(( //
      is_cvref_t<int const volatile&, int, void(...) const volatile&>));
  EXPECT_TRUE((is_cvref_t<int&&, int, void(...) &&>));
  EXPECT_TRUE((is_cvref_t<int const&&, int, void(...) const&&>));
  EXPECT_TRUE((is_cvref_t<int volatile&&, int, void(...) volatile&&>));
  EXPECT_TRUE(( //
      is_cvref_t<int const volatile&&, int, void(...) const volatile&&>));
  EXPECT_TRUE((is_cvref_t<int, int, void() noexcept>));
  EXPECT_TRUE((is_cvref_t<int const, int, void() const noexcept>));
  EXPECT_TRUE((is_cvref_t<int volatile, int, void() volatile noexcept>));
  EXPECT_TRUE(( //
      is_cvref_t<int const volatile, int, void() const volatile noexcept>));
  EXPECT_TRUE((is_cvref_t<int&, int, void()& noexcept>));
  EXPECT_TRUE((is_cvref_t<int const&, int, void() const& noexcept>));
  EXPECT_TRUE((is_cvref_t<int volatile&, int, void() volatile& noexcept>));
  EXPECT_TRUE(( //
      is_cvref_t<int const volatile&, int, void() const volatile& noexcept>));
  EXPECT_TRUE((is_cvref_t<int&&, int, void()&& noexcept>));
  EXPECT_TRUE((is_cvref_t<int const&&, int, void() const&& noexcept>));
  EXPECT_TRUE((is_cvref_t<int volatile&&, int, void() volatile&& noexcept>));
  EXPECT_TRUE((
      is_cvref_t<int const volatile&&, int, void() const volatile&& noexcept>));
  EXPECT_TRUE((is_cvref_t<int, int, void(...) noexcept>));
  EXPECT_TRUE((is_cvref_t<int const, int, void(...) const noexcept>));
  EXPECT_TRUE((is_cvref_t<int volatile, int, void(...) volatile noexcept>));
  EXPECT_TRUE(( //
      is_cvref_t<int const volatile, int, void(...) const volatile noexcept>));
  EXPECT_TRUE((is_cvref_t<int&, int, void(...)& noexcept>));
  EXPECT_TRUE((is_cvref_t<int const&, int, void(...) const& noexcept>));
  EXPECT_TRUE((is_cvref_t<int volatile&, int, void(...) volatile& noexcept>));
  EXPECT_TRUE(( //
      is_cvref_t<
          int const volatile&,
          int,
          void(...) const volatile& noexcept>));
  EXPECT_TRUE((is_cvref_t<int&&, int, void(...)&& noexcept>));
  EXPECT_TRUE((is_cvref_t<int const&&, int, void(...) const&& noexcept>));
  EXPECT_TRUE((is_cvref_t<int volatile&&, int, void(...) volatile&& noexcept>));
  EXPECT_TRUE(( //
      is_cvref_t<
          int const volatile&&,
          int,
          void(...) const volatile&& noexcept>));
}

TEST_F(TraitsTest, function_remove_cvref) {
  using f = char(int*);
  EXPECT_TRUE((same_v<f, rem_cvref_t<char(int*)>>));
  EXPECT_TRUE((same_v<f, rem_cvref_t<char(int*) const>>));
  EXPECT_TRUE((same_v<f, rem_cvref_t<char(int*) volatile>>));
  EXPECT_TRUE((same_v<f, rem_cvref_t<char(int*) const volatile>>));
  EXPECT_TRUE((same_v<f, rem_cvref_t<char(int*)&>>));
  EXPECT_TRUE((same_v<f, rem_cvref_t<char(int*) const&>>));
  EXPECT_TRUE((same_v<f, rem_cvref_t<char(int*) volatile&>>));
  EXPECT_TRUE((same_v<f, rem_cvref_t<char(int*) const volatile&>>));
  EXPECT_TRUE((same_v<f, rem_cvref_t<char(int*) &&>>));
  EXPECT_TRUE((same_v<f, rem_cvref_t<char(int*) const&&>>));
  EXPECT_TRUE((same_v<f, rem_cvref_t<char(int*) volatile&&>>));
  EXPECT_TRUE((same_v<f, rem_cvref_t<char(int*) const volatile&&>>));

  using fv = char(int*, ...);
  EXPECT_TRUE((same_v<fv, rem_cvref_t<char(int*, ...)>>));
  EXPECT_TRUE((same_v<fv, rem_cvref_t<char(int*, ...) const>>));
  EXPECT_TRUE((same_v<fv, rem_cvref_t<char(int*, ...) volatile>>));
  EXPECT_TRUE((same_v<fv, rem_cvref_t<char(int*, ...) const volatile>>));
  EXPECT_TRUE((same_v<fv, rem_cvref_t<char(int*, ...)&>>));
  EXPECT_TRUE((same_v<fv, rem_cvref_t<char(int*, ...) const&>>));
  EXPECT_TRUE((same_v<fv, rem_cvref_t<char(int*, ...) volatile&>>));
  EXPECT_TRUE((same_v<fv, rem_cvref_t<char(int*, ...) const volatile&>>));
  EXPECT_TRUE((same_v<fv, rem_cvref_t<char(int*, ...) &&>>));
  EXPECT_TRUE((same_v<fv, rem_cvref_t<char(int*, ...) const&&>>));
  EXPECT_TRUE((same_v<fv, rem_cvref_t<char(int*, ...) volatile&&>>));
  EXPECT_TRUE((same_v<fv, rem_cvref_t<char(int*, ...) const volatile&&>>));

  using g = char(int*) noexcept;
  EXPECT_TRUE((same_v<g, rem_cvref_t<char(int*) noexcept>>));
  EXPECT_TRUE((same_v<g, rem_cvref_t<char(int*) const noexcept>>));
  EXPECT_TRUE((same_v<g, rem_cvref_t<char(int*) volatile noexcept>>));
  EXPECT_TRUE((same_v<g, rem_cvref_t<char(int*) const volatile noexcept>>));
  EXPECT_TRUE((same_v<g, rem_cvref_t<char(int*)& noexcept>>));
  EXPECT_TRUE((same_v<g, rem_cvref_t<char(int*) const& noexcept>>));
  EXPECT_TRUE((same_v<g, rem_cvref_t<char(int*) volatile& noexcept>>));
  EXPECT_TRUE((same_v<g, rem_cvref_t<char(int*) const volatile& noexcept>>));
  EXPECT_TRUE((same_v<g, rem_cvref_t<char(int*)&& noexcept>>));
  EXPECT_TRUE((same_v<g, rem_cvref_t<char(int*) const&& noexcept>>));
  EXPECT_TRUE((same_v<g, rem_cvref_t<char(int*) volatile&& noexcept>>));
  EXPECT_TRUE((same_v<g, rem_cvref_t<char(int*) const volatile&& noexcept>>));

  using gv = char(int*, ...) noexcept;
  EXPECT_TRUE((same_v<gv, rem_cvref_t<char(int*, ...) noexcept>>));
  EXPECT_TRUE((same_v<gv, rem_cvref_t<char(int*, ...) const noexcept>>));
  EXPECT_TRUE((same_v<gv, rem_cvref_t<char(int*, ...) volatile noexcept>>));
  EXPECT_TRUE(( //
      same_v<gv, rem_cvref_t<char(int*, ...) const volatile noexcept>>));
  EXPECT_TRUE((same_v<gv, rem_cvref_t<char(int*, ...)& noexcept>>));
  EXPECT_TRUE((same_v<gv, rem_cvref_t<char(int*, ...) const& noexcept>>));
  EXPECT_TRUE((same_v<gv, rem_cvref_t<char(int*, ...) volatile& noexcept>>));
  EXPECT_TRUE(( //
      same_v<gv, rem_cvref_t<char(int*, ...) const volatile& noexcept>>));
  EXPECT_TRUE((same_v<gv, rem_cvref_t<char(int*, ...)&& noexcept>>));
  EXPECT_TRUE((same_v<gv, rem_cvref_t<char(int*, ...) const&& noexcept>>));
  EXPECT_TRUE((same_v<gv, rem_cvref_t<char(int*, ...) volatile&& noexcept>>));
  EXPECT_TRUE(( //
      same_v<gv, rem_cvref_t<char(int*, ...) const volatile&& noexcept>>));
}

TEST_F(TraitsTest, function_like_value) {
  using f = char(int*);
  EXPECT_TRUE((is_like_v<char(int*), int, f>));
  EXPECT_TRUE((is_like_v<char(int*) const, int const, f>));
  EXPECT_TRUE((is_like_v<char(int*) volatile, int volatile, f>));
  EXPECT_TRUE((is_like_v<char(int*) const volatile, int const volatile, f>));
  EXPECT_TRUE((is_like_v<char(int*)&, int&, f>));
  EXPECT_TRUE((is_like_v<char(int*) const&, int const&, f>));
  EXPECT_TRUE((is_like_v<char(int*) volatile&, int volatile&, f>));
  EXPECT_TRUE((is_like_v<char(int*) const volatile&, int const volatile&, f>));
  EXPECT_TRUE((is_like_v<char(int*)&&, int&&, f>));
  EXPECT_TRUE((is_like_v<char(int*) const&&, int const&&, f>));
  EXPECT_TRUE((is_like_v<char(int*) volatile&&, int volatile&&, f>));
  EXPECT_TRUE(( //
      is_like_v<char(int*) const volatile&&, int const volatile&&, f>));

  using fv = char(int*, ...);
  EXPECT_TRUE((is_like_v<char(int*, ...), int, fv>));
  EXPECT_TRUE((is_like_v<char(int*, ...) const, int const, fv>));
  EXPECT_TRUE((is_like_v<char(int*, ...) volatile, int volatile, fv>));
  EXPECT_TRUE(( //
      is_like_v<char(int*, ...) const volatile, int const volatile, fv>));
  EXPECT_TRUE((is_like_v<char(int*, ...)&, int&, fv>));
  EXPECT_TRUE((is_like_v<char(int*, ...) const&, int const&, fv>));
  EXPECT_TRUE((is_like_v<char(int*, ...) volatile&, int volatile&, fv>));
  EXPECT_TRUE(( //
      is_like_v<char(int*, ...) const volatile&, int const volatile&, fv>));
  EXPECT_TRUE((is_like_v<char(int*, ...)&&, int&&, fv>));
  EXPECT_TRUE((is_like_v<char(int*, ...) const&&, int const&&, fv>));
  EXPECT_TRUE((is_like_v<char(int*, ...) volatile&&, int volatile&&, fv>));
  EXPECT_TRUE(( //
      is_like_v<char(int*, ...) const volatile&&, int const volatile&&, fv>));

  using g = char(int*) noexcept;
  EXPECT_TRUE((is_like_v<char(int*) noexcept, int, g>));
  EXPECT_TRUE((is_like_v<char(int*) const noexcept, int const, g>));
  EXPECT_TRUE((is_like_v<char(int*) volatile noexcept, int volatile, g>));
  EXPECT_TRUE(( //
      is_like_v<char(int*) const volatile noexcept, int const volatile, g>));
  EXPECT_TRUE((is_like_v<char(int*)& noexcept, int&, g>));
  EXPECT_TRUE((is_like_v<char(int*) const& noexcept, int const&, g>));
  EXPECT_TRUE((is_like_v<char(int*) volatile& noexcept, int volatile&, g>));
  EXPECT_TRUE(( //
      is_like_v<char(int*) const volatile& noexcept, int const volatile&, g>));
  EXPECT_TRUE((is_like_v<char(int*)&& noexcept, int&&, g>));
  EXPECT_TRUE((is_like_v<char(int*) const&& noexcept, int const&&, g>));
  EXPECT_TRUE((is_like_v<char(int*) volatile&& noexcept, int volatile&&, g>));
  EXPECT_TRUE(( //
      is_like_v<
          char(int*) const volatile&& noexcept,
          int const volatile&&,
          g>));

  using gv = char(int*, ...) noexcept;
  EXPECT_TRUE((is_like_v<char(int*, ...) noexcept, int, gv>));
  EXPECT_TRUE((is_like_v<char(int*, ...) const noexcept, int const, gv>));
  EXPECT_TRUE((is_like_v<char(int*, ...) volatile noexcept, int volatile, gv>));
  EXPECT_TRUE(( //
      is_like_v<
          char(int*, ...) const volatile noexcept,
          int const volatile,
          gv>));
  EXPECT_TRUE((is_like_v<char(int*, ...)& noexcept, int&, gv>));
  EXPECT_TRUE((is_like_v<char(int*, ...) const& noexcept, int const&, gv>));
  EXPECT_TRUE(( //
      is_like_v<char(int*, ...) volatile& noexcept, int volatile&, gv>));
  EXPECT_TRUE(( //
      is_like_v<
          char(int*, ...) const volatile& noexcept,
          int const volatile&,
          gv>));
  EXPECT_TRUE((is_like_v<char(int*, ...)&& noexcept, int&&, gv>));
  EXPECT_TRUE((is_like_v<char(int*, ...) const&& noexcept, int const&&, gv>));
  EXPECT_TRUE(( //
      is_like_v<char(int*, ...) volatile&& noexcept, int volatile&&, gv>));
  EXPECT_TRUE(( //
      is_like_v<
          char(int*, ...) const volatile&& noexcept,
          int const volatile&&,
          gv>));
}
