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

#include <folly/lang/Bindings.h>
#include <folly/portability/GTest.h>

namespace folly::bindings::detail {

using namespace folly::bindings::ext;

namespace detail {
using by_ref_bind_info = decltype([](auto bi) {
  bi.category = ext::category_t::ref;
  return bi;
});
} // namespace detail

// This isn't in `Bindings.h` only because it's unclear if users need something
// a const-defaultable "by reference" verb.
template <typename... Ts>
struct by_ref : ext::merge_update_bound_args<detail::by_ref_bind_info, Ts...> {
  using ext::merge_update_bound_args<detail::by_ref_bind_info, Ts...>::
      merge_update_bound_args;
};
template <typename... Ts>
by_ref(Ts&&...) -> by_ref<ext::deduce_bound_args_t<Ts>...>;

struct Foo : folly::NonCopyableNonMovable {
  constexpr explicit Foo(bool* made, int n) : n_(n) {
    if (made) {
      *made = true;
    }
  }
  int n_;
};

// This is here so that test "runs" show up in CI history
TEST(BindingsTest, all_tests_run_at_build_time) {
  // This is a manually-enabled example of the `lifetimebound` annotation on
  // `in_place_bound_args::unsafe_tuple_to_bind()`.  With `lifetimebound` it
  // won't compile, without it would hit an ASAN failure.  It has to be a
  // runtime test because `constexpr` evaluation detects usage of dangling
  // references regardless of `lifetimebound`..
#if 0
  int n = 1337;
  bool made = false;
  auto fooMaker = make_in_place<Foo>(&made, n).unsafe_tuple_to_bind();
  // UNSAFE: `fooMaker` contains a ref to the prvalue `&made`, which became
  // invalid at the `;` of the previous line.
  lite_tuple::tuple<Foo> foo = std::move(fooMaker);
  EXPECT_TRUE(made);
  EXPECT_EQ(1337, std::get<0>(foo).n_);
  FAIL() << "Should not compile, or at least fail under ASAN.";
#endif
}

// Better UX than `assert()` in constexpr tests.
constexpr void test(bool ok) {
  if (!ok) {
    throw std::exception(); // Throwing in constexpr code is a compile error
  }
}

constexpr auto check_ref_bound_args() {
  int y = 5;
  static_assert(std::is_same_v<decltype(bound_args{5}), bound_args<int&&>>);
  {
    auto lval = bound_args(y);
    static_assert(std::is_same_v<decltype(lval), bound_args<int&>>);
    static_assert(
        std::is_same_v<
            decltype(lval)::binding_list_t,
            tag_t<binding_t<bind_info_t{}, int&>>>);
    y += 20;
    static_assert(
        std::is_same_v<
            decltype(std::move(lval).unsafe_tuple_to_bind()),
            lite_tuple::tuple<int&>>);
    test(25 == lite_tuple::get<0>(std::move(lval).unsafe_tuple_to_bind()));
  }
  {
    auto rval = bound_args(std::move(y));
    static_assert(std::is_same_v<decltype(rval), bound_args<int&&>>);
    static_assert(
        std::is_same_v<
            decltype(rval)::binding_list_t,
            tag_t<binding_t<bind_info_t{}, int&&>>>);
    y -= 10;
    static_assert(
        std::is_same_v<
            decltype(std::move(rval).unsafe_tuple_to_bind()),
            lite_tuple::tuple<int&&>>);
    test(15 == lite_tuple::get<0>(std::move(rval).unsafe_tuple_to_bind()));
  }
  return true;
}

static_assert(check_ref_bound_args());

constexpr auto check_nested_bound_args() {
  int b = 2, d = 4;
  using FlatT =
      decltype(bound_args{1.2, bound_args{b, 'x'}, d, bound_args{}, "abc"});
  static_assert(
      std::is_same_v<
          FlatT,
          bound_args<
              double&&,
              bound_args<int&, char&&>,
              int&,
              bound_args<>,
              const char(&)[4]>>);
  constexpr auto BI = bind_info_t{};
  static_assert(
      std::is_same_v<
          FlatT::binding_list_t,
          tag_t<
              binding_t<BI, double&&>,
              binding_t<BI, int&>,
              binding_t<BI, char&&>,
              binding_t<BI, int&>,
              binding_t<BI, const char(&)[4]>>>);
  return true;
}

static_assert(check_nested_bound_args());

constexpr auto check_const_and_non_const() {
  double b = 2.3;

  using non_const_bas = decltype(non_constant(1, bound_args(b, 'c')));
  static_assert(
      std::is_same_v<
          non_const_bas,
          non_constant<int&&, bound_args<double&, char&&>>>);
  constexpr bind_info_t def_non_const_bi{
      category_t{}, constness_t::non_constant};
  static_assert(
      std::is_same_v<
          non_const_bas::binding_list_t,
          tag_t<
              binding_t<def_non_const_bi, int&&>,
              binding_t<def_non_const_bi, double&>,
              binding_t<def_non_const_bi, char&&>>>);

  using const_bas = decltype(constant{non_constant{1, bound_args{b, 'c'}}});
  static_assert(
      std::is_same_v<
          const_bas,
          constant<non_constant<int&&, bound_args<double&, char&&>>>>);
  constexpr bind_info_t def_const_bi{category_t{}, constness_t::constant};
  static_assert(
      std::is_same_v<
          const_bas::binding_list_t,
          tag_t<
              binding_t<def_const_bi, int&&>,
              binding_t<def_const_bi, double&>,
              binding_t<def_const_bi, char&&>>>);

  static_assert(
      std::is_same_v<
          decltype(constant(b))::binding_list_t,
          tag_t<binding_t<def_const_bi, double&>>>);

  static_assert(
      std::is_same_v<
          decltype(constant(non_constant(b)))::binding_list_t,
          tag_t<binding_t<def_const_bi, double&>>>);

  static_assert(
      std::is_same_v<
          decltype(non_constant(b))::binding_list_t,
          tag_t<binding_t<def_non_const_bi, double&>>>);

  static_assert(
      std::is_same_v<
          decltype(non_constant(constant(b)))::binding_list_t,
          tag_t<binding_t<def_non_const_bi, double&>>>);

  return true;
}

static_assert(check_const_and_non_const());

constexpr auto check_by_ref() {
  double b = 2.3;

  using ref = decltype(by_ref{1, bound_args{b, 'c'}});
  static_assert(
      std::is_same_v<ref, by_ref<int&&, bound_args<double&, char&&>>>);
  constexpr bind_info_t ref_def_bi{category_t::ref, constness_t{}};
  static_assert(
      std::is_same_v<
          ref::binding_list_t,
          tag_t<
              binding_t<ref_def_bi, int&&>,
              binding_t<ref_def_bi, double&>,
              binding_t<ref_def_bi, char&&>>>);

  using constant_ref = decltype(const_ref{1, bound_args{b, 'c'}});
  static_assert(
      std::is_same_v<
          constant_ref,
          const_ref<int&&, bound_args<double&, char&&>>>);
  constexpr bind_info_t ref_const_bi{category_t::ref, constness_t::constant};
  static_assert(
      std::is_same_v<
          constant_ref::binding_list_t,
          tag_t<
              binding_t<ref_const_bi, int&&>,
              binding_t<ref_const_bi, double&>,
              binding_t<ref_const_bi, char&&>>>);

  using non_constant_ref =
      decltype(non_constant{const_ref{1, bound_args{b, 'c'}}});
  static_assert(
      std::is_same_v<
          non_constant_ref,
          non_constant<const_ref<int&&, bound_args<double&, char&&>>>>);
  constexpr bind_info_t ref_non_const_bi{
      category_t::ref, constness_t::non_constant};
  using non_const_bindings = tag_t<
      binding_t<ref_non_const_bi, int&&>,
      binding_t<ref_non_const_bi, double&>,
      binding_t<ref_non_const_bi, char&&>>;
  static_assert(
      std::is_same_v<non_constant_ref::binding_list_t, non_const_bindings>);

  using non_const_ref = decltype(mut_ref{1, bound_args{b, 'c'}});
  static_assert(
      std::is_same_v<
          non_const_ref,
          mut_ref<int&&, bound_args<double&, char&&>>>);
  static_assert(
      std::is_same_v<non_const_ref::binding_list_t, non_const_bindings>);

  static_assert(
      std::is_same_v<
          decltype(constant(const_ref(b))),
          constant<const_ref<double&>>>);
  static_assert(
      std::is_same_v<
          decltype(constant(const_ref(b)))::binding_list_t,
          tag_t<binding_t<ref_const_bi, double&>>>);
  static_assert(
      std::is_same_v<
          decltype(const_ref(constant(b))),
          const_ref<constant<double&>>>);
  static_assert(
      std::is_same_v<
          decltype(const_ref(constant(b)))::binding_list_t,
          tag_t<binding_t<ref_const_bi, double&>>>);

  using bind_ref_non_const = tag_t<binding_t<ref_non_const_bi, double&>>;
  static_assert(
      std::is_same_v<decltype(mut_ref(b))::binding_list_t, bind_ref_non_const>);
  static_assert(
      std::is_same_v<
          decltype(non_constant(const_ref(b)))::binding_list_t,
          bind_ref_non_const>);
  static_assert(
      std::is_same_v<
          decltype(by_ref{non_constant{b}})::binding_list_t,
          bind_ref_non_const>);
  static_assert(
      std::is_same_v<
          decltype(const_ref{non_constant{b}})::binding_list_t,
          tag_t<binding_t<ref_const_bi, double&>>>);

  return true;
}

static_assert(check_by_ref());

constexpr auto check_in_place_bound_args_one_line() {
  bool made = false;

  static_assert(
      1 ==
      std::tuple_size_v<
          decltype(make_in_place<Foo>(&made, 37).unsafe_tuple_to_bind())>);

  // Binding prvalues is ok since `Foo` is constructed in the same statement.
  Foo foo =
      lite_tuple::get<0>(make_in_place<Foo>(&made, 37).unsafe_tuple_to_bind());
  test(made);
  test(foo.n_ == 37);

  int n = 3;
  Foo f2 =
      lite_tuple::get<0>(make_in_place<Foo>(nullptr, n).unsafe_tuple_to_bind());
  ++n;
  test(3 == f2.n_);
  test(4 == n);

  return true;
}

static_assert(check_in_place_bound_args_one_line());
constexpr auto check_in_place_bound_args_step_by_step() {
  bool made = false;

  // These vars can't be prvalues since the `Foo` ctor is delayed.
  bool* made_ptr = &made;
  int n = 37;

  // Not a prvalue due to [[clang::lifetimebound]] on `what_to_bind()`.
  auto b = make_in_place<Foo>(made_ptr, n);
  static_assert(
      std::is_same_v<decltype(b), in_place_bound_args<Foo, bool*&, int&>>);
  auto [fooMaker] = std::move(b).unsafe_tuple_to_bind();
  test(!made);

  Foo foo = std::move(fooMaker);
  test(made);
  test(foo.n_ == n);

  return true;
}

static_assert(check_in_place_bound_args_step_by_step());

// NB: These signatures are NOT meant to be user-visible.
constexpr auto check_in_place_bound_args_type_sig() {
  static_assert(
      std::is_same_v<
          decltype(make_in_place<Foo>(nullptr, 7)),
          in_place_bound_args<Foo, std::nullptr_t, int>>);

  int n = 7;
  static_assert(
      std::is_same_v<
          decltype(make_in_place<Foo>(nullptr, n)),
          in_place_bound_args<Foo, std::nullptr_t, int&>>);

  // Composes with projection modifiers as expected
  using const_in_place = decltype(constant(make_in_place<Foo>(nullptr, 7)));
  static_assert(
      std::is_same_v<
          const_in_place,
          constant<in_place_bound_args<Foo, std::nullptr_t, int>>>);
  constexpr bind_info_t const_bi{category_t{}, constness_t::constant};
  static_assert(
      std::is_same_v<
          const_in_place::binding_list_t,
          tag_t<binding_t<const_bi, Foo>>>);

  return true;
}

static_assert(check_in_place_bound_args_type_sig());

constexpr auto check_in_place_bound_args_via_fn() {
  // Test for issues with prvalue lambdas
  Foo f1 = lite_tuple::get<0>(
      make_in_place_with([]() {
        return Foo{nullptr, 17};
      }).unsafe_tuple_to_bind());
  test(17 == f1.n_);

  auto fn = []() { return Foo{nullptr, 37}; };
  auto b2 = make_in_place_with(fn);
  static_assert(
      std::is_same_v<decltype(b2), in_place_fn_bound_args<Foo, decltype(fn)>>);
  static_assert(
      std::is_same_v<
          decltype(b2)::binding_list_t,
          tag_t<binding_t<bind_info_t{}, Foo>>>);
  static_assert(
      1 == std::tuple_size_v<decltype(std::move(b2).unsafe_tuple_to_bind())>);
  Foo f2 = lite_tuple::get<0>(std::move(b2).unsafe_tuple_to_bind());
  test(37 == f2.n_);

  struct MoveN : MoveOnly {
    int n_;
  };

  int n1 = 1000, n2 = 300, n3 = 30, n4 = 7;
  auto fn2 = [mn1 = MoveN{.n_ = n1}](int&& i2, int& i3, const int& i4) {
    return mn1.n_ + i2 + i3 + i4;
  };
  auto b3 = make_in_place_with(
      std::move(fn2), // the contained `MoveN` is noncopyable
      std::move(n2),
      n3,
      std::as_const(n4));
  static_assert(
      std::is_same_v<
          decltype(b3),
          in_place_fn_bound_args<int, decltype(fn2), int, int&, const int&>>);
  static_assert(
      std::is_same_v<
          decltype(b3)::binding_list_t,
          tag_t<binding_t<bind_info_t{}, int>>>);

  return true;
}

static_assert(check_in_place_bound_args_via_fn());

constexpr auto check_in_place_bound_args_modifier_distributive_property() {
  constexpr bind_info_t def_non_const_bi{
      category_t{}, constness_t::non_constant};
  constexpr bind_info_t ref_non_const_bi{
      category_t::ref, constness_t::non_constant};
  using expected_binding_list = tag_t<
      binding_t<def_non_const_bi, bool&&>,
      binding_t<ref_non_const_bi, double&>,
      binding_t<def_non_const_bi, int>,
      binding_t<ref_non_const_bi, char&&>>;

  double b = 2;
  static_assert(
      std::is_same_v<
          expected_binding_list,
          decltype(non_constant(
              true, const_ref(b), make_in_place<int>(3), by_ref('c')))::
              binding_list_t>);
  static_assert(
      std::is_same_v<
          expected_binding_list,
          decltype(non_constant(
              non_constant(true),
              mut_ref(b),
              non_constant(make_in_place<int>(3)),
              mut_ref('c')))::binding_list_t>);

  return true;
}

static_assert(check_in_place_bound_args_modifier_distributive_property());

template <typename B>
using first_policy =
    binding_policy<type_list_element_t<0, typename B::binding_list_t>>;

template <typename B>
using store = typename first_policy<B>::storage_type;

constexpr auto check_in_place_binding_storage_type() {
  int b = 2;

  static_assert(std::is_same_v<store<decltype(constant(b))>, const int>);
  static_assert(std::is_same_v<store<decltype(non_constant(b))>, int>);
  static_assert(std::is_same_v<store<decltype(constant(5))>, const int>);
  static_assert(std::is_same_v<store<decltype(non_constant(5))>, int>);

  static_assert(std::is_same_v<store<decltype(const_ref(b))>, const int&>);
  static_assert(
      std::is_same_v<store<decltype(constant(const_ref(b)))>, const int&>);
  static_assert(std::is_same_v<store<decltype(mut_ref(b))>, int&>);
  static_assert(std::is_same_v<store<decltype(const_ref(5))>, const int&&>);
  static_assert(
      std::is_same_v<store<decltype(constant(const_ref(5)))>, const int&&>);
  static_assert(std::is_same_v<store<decltype(mut_ref(5))>, int&&>);

  static_assert(
      std::is_same_v<
          first_policy<decltype(make_in_place<int>(5))>::storage_type,
          int>);
  static_assert(
      std::is_same_v<
          store<decltype(constant(make_in_place<int>(5)))>,
          const int>);

  return true;
}

static_assert(check_in_place_binding_storage_type());

constexpr auto check_unsafe_move() {
  int y = 5;
  bound_args one_ref{y};
  bound_args wrapped{bound_args_unsafe_move::from(std::move(one_ref))};

  bool made = false;
  bool* made_ptr = &made;
  auto in_place = make_in_place<Foo>(made_ptr, y);

  bound_args merged1{
      0xdeadbeef,
      bound_args_unsafe_move::from(std::move(wrapped)),
      bound_args_unsafe_move::from(std::move(in_place))};
  auto merged2 = bound_args_unsafe_move::from(std::move(merged1));

  test(!made);

  static_assert(
      std::is_same_v<
          decltype(merged2),
          bound_args<
              unsigned int&&, // the now-destroyed ephemeral 0xdeadbeef
              bound_args<int&>, // wrapped ref to `y`
              in_place_bound_args<Foo, bool*&, int&>>>);

  return true;
}

static_assert(check_unsafe_move());

// A minimal test for `using signature_type = storage_type`...
static_assert(
    std::is_same_v<
        typename first_policy<decltype(constant(const_ref(5)))>::signature_type,
        const int&&>);

} // namespace folly::bindings::detail
