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

#include <folly/Traits.h>

namespace folly {

namespace detail {

template <typename>
struct function_traits_base_;

template <typename R, typename... A>
struct function_traits_base_<R(A...)> {
  using result_type = R;

  template <std::size_t Idx>
  using argument = type_pack_element_t<Idx, A...>;

  template <template <typename...> class F>
  using arguments = F<A...>;
};

template <bool Nx>
struct function_traits_nx_ {
  static constexpr bool is_nothrow = Nx;
};

template <bool Var>
struct function_traits_var_ {
  static constexpr bool is_variadic = Var;
};

template <typename T>
struct function_traits_cvref_ {
  template <typename D>
  using value_like = like_t<T, D>;
};

} // namespace detail

//  function_traits
//
//  Incomplete except when instantiated over any type matching std::is_function.
//  Namely, types S over R, A..., NX of the form:
//
//      S = R(A...) [const] [volatile] (|&|&&) noexcept(NX)
//
//  When complete, has a class body of the form:
//
//      struct function_traits<S> {
//        using result_type = R;
//        static constexpr bool is_nothrow = NX;
//
//        template <std::size_t Index>
//        using argument = type_pack_element_t<Index, A...>;
//        template <typename F>
//        using arguments = F<A...>;
//        template <typename Model>
//        using value_like = Model [const] [volatile] (|&|&&);
//      };
//
//  Member argument is a metafunction allowing access to one argument type at a
//  time, by positional index:
//
//      using second_argument_type = function_traits<S>::argument<1>;
//
//  Member arguments is a metafunction allowing access to all argument types at
//  once:
//
//      using arguments_tuple_type =
//          function_traits<S>::arguments<std::tuple>;
//
//  Member value_like is a metafunction allowing access to the const-,
//  volatile-, and reference-qualifiers using like_t to transport all these
//  qualifiers to a destination type which may then be queried:
//
//      constexpr bool is_rvalue_reference = std::is_rvalue_reverence_v<
//          function_traits<S>::value_like<int>>;
//
//  Keep in mind that member types or type-aliases must be referenced with
//  keyword typename when dependent and that member templates must likewise be
//  referenced with keyword template when in dependent:
//
//      template <typename... A>
//      using get_size = index_constant<sizeof...(A);
//      template <typename S>
//      using arguments_size_t =
//          typename function_traits<S>::template arguments<get_size>;
//
//  Every fact of a function type S is thus discoverable from function_traits<S>
//  without requiring further class template specializations or further overload
//  set searches, all as types or constexpr values.
//
//  Further specializations are forbidden.
template <typename>
struct function_traits;

template <typename R, typename... A>
struct function_traits<R(A...)> //
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<false>,
      detail::function_traits_var_<false>,
      detail::function_traits_cvref_<int> {};

template <typename R, typename... A>
struct function_traits<R(A...) const>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<false>,
      detail::function_traits_var_<false>,
      detail::function_traits_cvref_<int const> {};

template <typename R, typename... A>
struct function_traits<R(A...) volatile>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<false>,
      detail::function_traits_var_<false>,
      detail::function_traits_cvref_<int volatile> {};

template <typename R, typename... A>
struct function_traits<R(A...) const volatile>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<false>,
      detail::function_traits_var_<false>,
      detail::function_traits_cvref_<int const volatile> {};

template <typename R, typename... A>
struct function_traits<R(A...)&> //
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<false>,
      detail::function_traits_var_<false>,
      detail::function_traits_cvref_<int&> {};

template <typename R, typename... A>
struct function_traits<R(A...) const&>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<false>,
      detail::function_traits_var_<false>,
      detail::function_traits_cvref_<int const&> {};

template <typename R, typename... A>
struct function_traits<R(A...) volatile&>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<false>,
      detail::function_traits_var_<false>,
      detail::function_traits_cvref_<int volatile&> {};

template <typename R, typename... A>
struct function_traits<R(A...) const volatile&>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<false>,
      detail::function_traits_var_<false>,
      detail::function_traits_cvref_<int const volatile&> {};

template <typename R, typename... A>
struct function_traits<R(A...) &&> //
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<false>,
      detail::function_traits_var_<false>,
      detail::function_traits_cvref_<int&&> {};

template <typename R, typename... A>
struct function_traits<R(A...) const&&>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<false>,
      detail::function_traits_var_<false>,
      detail::function_traits_cvref_<int const&&> {};

template <typename R, typename... A>
struct function_traits<R(A...) volatile&&>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<false>,
      detail::function_traits_var_<false>,
      detail::function_traits_cvref_<int volatile&&> {};

template <typename R, typename... A>
struct function_traits<R(A...) const volatile&&>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<false>,
      detail::function_traits_var_<false>,
      detail::function_traits_cvref_<int const volatile&&> {};

template <typename R, typename... A>
struct function_traits<R(A..., ...)> //
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<false>,
      detail::function_traits_var_<true>,
      detail::function_traits_cvref_<int> {};

template <typename R, typename... A>
struct function_traits<R(A..., ...) const>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<false>,
      detail::function_traits_var_<true>,
      detail::function_traits_cvref_<int const> {};

template <typename R, typename... A>
struct function_traits<R(A..., ...) volatile>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<false>,
      detail::function_traits_var_<true>,
      detail::function_traits_cvref_<int volatile> {};

template <typename R, typename... A>
struct function_traits<R(A..., ...) const volatile>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<false>,
      detail::function_traits_var_<true>,
      detail::function_traits_cvref_<int const volatile> {};

template <typename R, typename... A>
struct function_traits<R(A..., ...)&> //
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<false>,
      detail::function_traits_var_<true>,
      detail::function_traits_cvref_<int&> {};

template <typename R, typename... A>
struct function_traits<R(A..., ...) const&>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<false>,
      detail::function_traits_var_<true>,
      detail::function_traits_cvref_<int const&> {};

template <typename R, typename... A>
struct function_traits<R(A..., ...) volatile&>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<false>,
      detail::function_traits_var_<true>,
      detail::function_traits_cvref_<int volatile&> {};

template <typename R, typename... A>
struct function_traits<R(A..., ...) const volatile&>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<false>,
      detail::function_traits_var_<true>,
      detail::function_traits_cvref_<int const volatile&> {};

template <typename R, typename... A>
struct function_traits<R(A..., ...) &&> //
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<false>,
      detail::function_traits_var_<true>,
      detail::function_traits_cvref_<int&&> {};

template <typename R, typename... A>
struct function_traits<R(A..., ...) const&&>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<false>,
      detail::function_traits_var_<true>,
      detail::function_traits_cvref_<int const&&> {};

template <typename R, typename... A>
struct function_traits<R(A..., ...) volatile&&>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<false>,
      detail::function_traits_var_<true>,
      detail::function_traits_cvref_<int volatile&&> {};

template <typename R, typename... A>
struct function_traits<R(A..., ...) const volatile&&>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<false>,
      detail::function_traits_var_<true>,
      detail::function_traits_cvref_<int const volatile&&> {};

template <typename R, typename... A>
struct function_traits<R(A...) noexcept>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<true>,
      detail::function_traits_var_<false>,
      detail::function_traits_cvref_<int> {};

template <typename R, typename... A>
struct function_traits<R(A...) const noexcept>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<true>,
      detail::function_traits_var_<false>,
      detail::function_traits_cvref_<int const> {};

template <typename R, typename... A>
struct function_traits<R(A...) volatile noexcept>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<true>,
      detail::function_traits_var_<false>,
      detail::function_traits_cvref_<int volatile> {};

template <typename R, typename... A>
struct function_traits<R(A...) const volatile noexcept>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<true>,
      detail::function_traits_var_<false>,
      detail::function_traits_cvref_<int const volatile> {};

template <typename R, typename... A>
struct function_traits<R(A...) & noexcept>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<true>,
      detail::function_traits_var_<false>,
      detail::function_traits_cvref_<int&> {};

template <typename R, typename... A>
struct function_traits<R(A...) const & noexcept>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<true>,
      detail::function_traits_var_<false>,
      detail::function_traits_cvref_<int const&> {};

template <typename R, typename... A>
struct function_traits<R(A...) volatile & noexcept>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<true>,
      detail::function_traits_var_<false>,
      detail::function_traits_cvref_<int volatile&> {};

template <typename R, typename... A>
struct function_traits<R(A...) const volatile & noexcept>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<true>,
      detail::function_traits_var_<false>,
      detail::function_traits_cvref_<int const volatile&> {};

template <typename R, typename... A>
struct function_traits<R(A...) && noexcept>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<true>,
      detail::function_traits_var_<false>,
      detail::function_traits_cvref_<int&&> {};

template <typename R, typename... A>
struct function_traits<R(A...) const && noexcept>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<true>,
      detail::function_traits_var_<false>,
      detail::function_traits_cvref_<int const&&> {};

template <typename R, typename... A>
struct function_traits<R(A...) volatile && noexcept>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<true>,
      detail::function_traits_var_<false>,
      detail::function_traits_cvref_<int volatile&&> {};

template <typename R, typename... A>
struct function_traits<R(A...) const volatile && noexcept>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<true>,
      detail::function_traits_var_<false>,
      detail::function_traits_cvref_<int const volatile&&> {};

template <typename R, typename... A>
struct function_traits<R(A..., ...) noexcept>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<true>,
      detail::function_traits_var_<true>,
      detail::function_traits_cvref_<int> {};

template <typename R, typename... A>
struct function_traits<R(A..., ...) const noexcept>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<true>,
      detail::function_traits_var_<true>,
      detail::function_traits_cvref_<int const> {};

template <typename R, typename... A>
struct function_traits<R(A..., ...) volatile noexcept>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<true>,
      detail::function_traits_var_<true>,
      detail::function_traits_cvref_<int volatile> {};

template <typename R, typename... A>
struct function_traits<R(A..., ...) const volatile noexcept>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<true>,
      detail::function_traits_var_<true>,
      detail::function_traits_cvref_<int const volatile> {};

template <typename R, typename... A>
struct function_traits<R(A..., ...) & noexcept>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<true>,
      detail::function_traits_var_<true>,
      detail::function_traits_cvref_<int&> {};

template <typename R, typename... A>
struct function_traits<R(A..., ...) const & noexcept>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<true>,
      detail::function_traits_var_<true>,
      detail::function_traits_cvref_<int const&> {};

template <typename R, typename... A>
struct function_traits<R(A..., ...) volatile & noexcept>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<true>,
      detail::function_traits_var_<true>,
      detail::function_traits_cvref_<int volatile&> {};

template <typename R, typename... A>
struct function_traits<R(A..., ...) const volatile & noexcept>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<true>,
      detail::function_traits_var_<true>,
      detail::function_traits_cvref_<int const volatile&> {};

template <typename R, typename... A>
struct function_traits<R(A..., ...) && noexcept>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<true>,
      detail::function_traits_var_<true>,
      detail::function_traits_cvref_<int&&> {};

template <typename R, typename... A>
struct function_traits<R(A..., ...) const && noexcept>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<true>,
      detail::function_traits_var_<true>,
      detail::function_traits_cvref_<int const&&> {};

template <typename R, typename... A>
struct function_traits<R(A..., ...) volatile && noexcept>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<true>,
      detail::function_traits_var_<true>,
      detail::function_traits_cvref_<int volatile&&> {};

template <typename R, typename... A>
struct function_traits<R(A..., ...) const volatile && noexcept>
    : detail::function_traits_base_<R(A...)>,
      detail::function_traits_nx_<true>,
      detail::function_traits_var_<true>,
      detail::function_traits_cvref_<int const volatile&&> {};

//  ----

namespace detail {

template <bool Nx, bool Var, typename R>
struct function_remove_cvref_;
template <typename R>
struct function_remove_cvref_<false, false, R> {
  template <typename... A>
  using apply = R(A...);
};
template <typename R>
struct function_remove_cvref_<false, true, R> {
  template <typename... A>
  using apply = R(A..., ...);
};
template <typename R>
struct function_remove_cvref_<true, false, R> {
  template <typename... A>
  using apply = R(A...) noexcept;
};
template <typename R>
struct function_remove_cvref_<true, true, R> {
  template <typename... A>
  using apply = R(A..., ...) noexcept;
};

template <typename F, typename T = function_traits<F>>
using function_remove_cvref_t_ =
    typename T::template arguments<function_remove_cvref_<
        T::is_nothrow,
        T::is_variadic,
        typename T::result_type>::template apply>;

} // namespace detail

//  function_remove_cvref
//  function_remove_cvref_t
//
//  Given a function type of the form:
//      S = R(A...) [const] [volatile] (|&|&&) noexcept(NX)
//  Yields another function type:
//      R(A...) noexcept(NX)

template <typename F>
using function_remove_cvref_t = detail::function_remove_cvref_t_<F>;

template <typename F>
struct function_remove_cvref {
  using type = function_remove_cvref_t<F>;
};

//  ----

namespace detail {

template <typename Src, bool Var>
struct function_like_src_;
template <typename Src>
struct function_like_src_<Src, 0> {
  template <bool Nx, typename R, typename... A>
  using apply = R(A...) noexcept(Nx);
};
template <typename Src>
struct function_like_src_<Src const, 0> {
  template <bool Nx, typename R, typename... A>
  using apply = R(A...) const noexcept(Nx);
};
template <typename Src>
struct function_like_src_<Src volatile, 0> {
  template <bool Nx, typename R, typename... A>
  using apply = R(A...) volatile noexcept(Nx);
};
template <typename Src>
struct function_like_src_<Src const volatile, 0> {
  template <bool Nx, typename R, typename... A>
  using apply = R(A...) const volatile noexcept(Nx);
};
template <typename Src>
struct function_like_src_<Src&, 0> {
  template <bool Nx, typename R, typename... A>
  using apply = R(A...) & noexcept(Nx);
};
template <typename Src>
struct function_like_src_<Src const&, 0> {
  template <bool Nx, typename R, typename... A>
  using apply = R(A...) const& noexcept(Nx);
};
template <typename Src>
struct function_like_src_<Src volatile&, 0> {
  template <bool Nx, typename R, typename... A>
  using apply = R(A...) volatile& noexcept(Nx);
};
template <typename Src>
struct function_like_src_<Src const volatile&, 0> {
  template <bool Nx, typename R, typename... A>
  using apply = R(A...) const volatile& noexcept(Nx);
};
template <typename Src>
struct function_like_src_<Src&&, 0> {
  template <bool Nx, typename R, typename... A>
  using apply = R(A...) && noexcept(Nx);
};
template <typename Src>
struct function_like_src_<Src const&&, 0> {
  template <bool Nx, typename R, typename... A>
  using apply = R(A...) const&& noexcept(Nx);
};
template <typename Src>
struct function_like_src_<Src volatile&&, 0> {
  template <bool Nx, typename R, typename... A>
  using apply = R(A...) volatile&& noexcept(Nx);
};
template <typename Src>
struct function_like_src_<Src const volatile&&, 0> {
  template <bool Nx, typename R, typename... A>
  using apply = R(A...) const volatile&& noexcept(Nx);
};
template <typename Src>
struct function_like_src_<Src, 1> {
  template <bool Nx, typename R, typename... A>
  using apply = R(A..., ...) noexcept(Nx);
};
template <typename Src>
struct function_like_src_<Src const, 1> {
  template <bool Nx, typename R, typename... A>
  using apply = R(A..., ...) const noexcept(Nx);
};
template <typename Src>
struct function_like_src_<Src volatile, 1> {
  template <bool Nx, typename R, typename... A>
  using apply = R(A..., ...) volatile noexcept(Nx);
};
template <typename Src>
struct function_like_src_<Src const volatile, 1> {
  template <bool Nx, typename R, typename... A>
  using apply = R(A..., ...) const volatile noexcept(Nx);
};
template <typename Src>
struct function_like_src_<Src&, 1> {
  template <bool Nx, typename R, typename... A>
  using apply = R(A..., ...) & noexcept(Nx);
};
template <typename Src>
struct function_like_src_<Src const&, 1> {
  template <bool Nx, typename R, typename... A>
  using apply = R(A..., ...) const& noexcept(Nx);
};
template <typename Src>
struct function_like_src_<Src volatile&, 1> {
  template <bool Nx, typename R, typename... A>
  using apply = R(A..., ...) volatile& noexcept(Nx);
};
template <typename Src>
struct function_like_src_<Src const volatile&, 1> {
  template <bool Nx, typename R, typename... A>
  using apply = R(A..., ...) const volatile& noexcept(Nx);
};
template <typename Src>
struct function_like_src_<Src&&, 1> {
  template <bool Nx, typename R, typename... A>
  using apply = R(A..., ...) && noexcept(Nx);
};
template <typename Src>
struct function_like_src_<Src const&&, 1> {
  template <bool Nx, typename R, typename... A>
  using apply = R(A..., ...) const&& noexcept(Nx);
};
template <typename Src>
struct function_like_src_<Src volatile&&, 1> {
  template <bool Nx, typename R, typename... A>
  using apply = R(A..., ...) volatile&& noexcept(Nx);
};
template <typename Src>
struct function_like_src_<Src const volatile&&, 1> {
  template <bool Nx, typename R, typename... A>
  using apply = R(A..., ...) const volatile&& noexcept(Nx);
};

template <typename Dst>
struct function_like_dst_ : function_like_dst_<function_remove_cvref_t<Dst>> {};
template <typename R, typename... A>
struct function_like_dst_<R(A...)> {
  template <typename Src>
  using apply = typename function_like_src_<Src, 0>::template apply<0, R, A...>;
};
template <typename R, typename... A>
struct function_like_dst_<R(A..., ...)> {
  template <typename Src>
  using apply = typename function_like_src_<Src, 1>::template apply<0, R, A...>;
};
template <typename R, typename... A>
struct function_like_dst_<R(A...) noexcept> {
  template <typename Src>
  using apply = typename function_like_src_<Src, 0>::template apply<1, R, A...>;
};
template <typename R, typename... A>
struct function_like_dst_<R(A..., ...) noexcept> {
  template <typename Src>
  using apply = typename function_like_src_<Src, 1>::template apply<1, R, A...>;
};

} // namespace detail

//  function_like_value
//  function_like_value_t
//
//  Given a possibly-cvref-qualified value type Src and a possibly-cvref-
//  qualified function type Dst,  transports any cvref-qualifications found on
//  Src onto the base function type of Dst, which is Dst stripped of its own
//  cvref-qualifications.
//
//  Example:
//      function_like_value_t<int volatile, void() const&&> -> void() volatile
//      function_like_value_t<int const&&, void() volatile> -> void() const&&

template <typename Src, typename Dst>
using function_like_value_t =
    typename detail::function_like_dst_<Dst>::template apply<Src>;

template <typename Src, typename Dst>
struct function_like_value {
  using type = function_like_value_t<Src, Dst>;
};

} // namespace folly
