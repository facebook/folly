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

#include <folly/lang/bind/Named.h>
#include <folly/portability/GTest.h>

//
// IMPORTANT: This is intended to parallel `folly/lang/test/BindTest.cpp`!
// To reduce redundancy, we don't repeat some of the tests here.
//

namespace folly::bind::ext {

// This is here so that test "runs" show up in CI history
TEST(NamedTest, all_tests_run_at_build_time) {}

// Better UX than `assert()` in constexpr tests.
constexpr void test(bool ok) {
  if (!ok) {
    throw std::exception(); // Throwing in constexpr code is a compile error
  }
}

template <
    literal_string Id,
    constness_t Const = constness_t{},
    category_t Cat = category_t{}>
inline constexpr named_bind_info_t<Id, bind_info_t> named_bi{Cat, Const};

template <constness_t Const = constness_t{}, category_t Cat = category_t{}>
inline constexpr named_bind_info_t<self_id_t{}, bind_info_t> self_bi{
    Cat, Const};

constexpr auto check_ref_binding() {
  static_assert(std::is_same_v<decltype(self_id = 5), self_id_arg<int&&>>);

  static_assert(std::is_same_v<decltype("x"_id = 5), id_arg<"x", int&&>>);
  static_assert(
      std::is_same_v<decltype("x"_id = args{5}), id_arg<"x", args<int&&>>>);

#if 0 // Manual test showing that a name can't be bound to multiple args
  (void)("x"_id = args{5, 6});
#endif

  int y = 5;
  {
    auto lval = (self_id = y);
    static_assert(std::is_same_v<decltype(lval), self_id_arg<int&>>);
    y += 20;
    [](const auto& tup) {
      auto& [ref] = tup;
      test(25 == ref);
    }(std::move(lval).unsafe_tuple_to_bind());
  }
  {
    auto rval = ("y"_id = std::move(y));
    static_assert(std::is_same_v<decltype(rval), id_arg<"y", int&&>>);
    y -= 10;
    [](const auto& tup) {
      auto& [ref] = tup;
      test(15 == ref);
    }(std::move(rval).unsafe_tuple_to_bind());
  }
  return true;
}

static_assert(check_ref_binding());

constexpr auto check_flatten_bindings() {
  int b = 2, d = 4;
  using FlatT = decltype(args{
      "a"_id = 1.2,
      args{"b"_id = b, "c"_id = 'x'},
      "d"_id = d,
      args{},
      "e"_id = "abc"});
  static_assert(
      std::is_same_v<
          FlatT::binding_list_t,
          tag_t<
              binding_t<named_bi<"a">, double&&>,
              binding_t<named_bi<"b">, int&>,
              binding_t<named_bi<"c">, char&&>,
              binding_t<named_bi<"d">, int&>,
              binding_t<named_bi<"e">, const char(&)[4]>>>);
  return true;
}

static_assert(check_flatten_bindings());

constexpr auto check_as_const_and_non_const() {
  double b = 2.3;

  using non_const_bas =
      decltype(mut{"a"_id = 1, args{"b"_id = b, "c"_id = 'c'}});

  static_assert(
      std::is_same_v<
          non_const_bas,
          mut<id_arg<"a", int&&>,
              args<id_arg<"b", double&>, id_arg<"c", char&&>>>>);

  constexpr auto non_const = constness_t::mut;
  static_assert(
      std::is_same_v<
          non_const_bas::binding_list_t,
          tag_t<
              binding_t<named_bi<"a", non_const>, int&&>,
              binding_t<named_bi<"b", non_const>, double&>,
              binding_t<named_bi<"c", non_const>, char&&>>>);

  constexpr auto konst = constness_t::constant;
  static_assert(
      std::is_same_v<
          decltype(constant{
              mut{"a"_id = 1, args{"b"_id = b, "c"_id = 'c'}}})::binding_list_t,
          tag_t<
              binding_t<named_bi<"a", konst>, int&&>,
              binding_t<named_bi<"b", konst>, double&>,
              binding_t<named_bi<"c", konst>, char&&>>>);

  return true;
}

static_assert(check_as_const_and_non_const());

constexpr auto check_by_ref() {
  double b = 2.3;

  constexpr auto ref = category_t::ref;
  constexpr auto konst = constness_t::constant;
  static_assert(
      std::is_same_v<
          decltype(const_ref{
              "a"_id = 1, args{"b"_id = b, "c"_id = 'c'}})::binding_list_t,
          tag_t<
              binding_t<named_bi<"a", konst, ref>, int&&>,
              binding_t<named_bi<"b", konst, ref>, double&>,
              binding_t<named_bi<"c", konst, ref>, char&&>>>);

  constexpr auto non_const = constness_t::mut;
  using non_const_refs = tag_t<
      binding_t<named_bi<"a", non_const, ref>, int&&>,
      binding_t<named_bi<"b", non_const, ref>, double&>,
      binding_t<named_bi<"c", non_const, ref>, char&&>>;
  static_assert(
      std::is_same_v<
          decltype(mut{const_ref{
              "a"_id = 1, args{"b"_id = b, "c"_id = 'c'}}})::binding_list_t,
          non_const_refs>);

  static_assert(
      std::is_same_v<
          decltype(mut_ref{
              "a"_id = 1, args{"b"_id = b, "c"_id = 'c'}})::binding_list_t,
          non_const_refs>);

  return true;
}

static_assert(check_by_ref());

struct Foo : folly::NonCopyableNonMovable {
  constexpr explicit Foo(bool* made, int n) : n_(n) {
    if (made) {
      *made = true;
    }
  }
  int n_;
};

constexpr auto check_in_place_binding_type_sig() {
  using in_place_ba = decltype("x"_id = in_place<Foo>(nullptr, 7));
  static_assert(
      std::is_same_v<
          in_place_ba,
          id_arg<
              "x",
              folly::bind::detail::in_place_args<Foo, std::nullptr_t, int>>>);
  static_assert(
      std::is_same_v<
          in_place_ba::binding_list_t,
          tag_t<binding_t<named_bi<"x">, Foo>>>);

  // Composes with projection modifiers as expected
  static_assert(
      std::is_same_v<
          decltype("x"_id = constant{in_place<Foo>(nullptr, 7)})::
              binding_list_t,
          tag_t<binding_t<named_bi<"x", constness_t::constant>, Foo>>>);
  return true;
}

static_assert(check_in_place_binding_type_sig());

constexpr auto check_in_place_binding_natural_usage() {
  // projections don't affect `.unsafe_tuple_to_bind`, just the storage type
  Foo f1 = folly::detail::lite_tuple::get<0>(
      ("x"_id = constant{in_place<Foo>(nullptr, 17)}).unsafe_tuple_to_bind());
  test(17 == f1.n_);

  int n = 3;
  Foo f2 = folly::detail::lite_tuple::get<0>(
      ("y"_id = in_place<Foo>(nullptr, n)).unsafe_tuple_to_bind());
  ++n;
  test(3 == f2.n_);
  test(4 == n);

  return true;
}

static_assert(check_in_place_binding_natural_usage());

constexpr auto check_in_place_binding_modifier_distributive_property() {
  constexpr auto ref = category_t::ref;
  constexpr auto non_const = constness_t::mut;
  using my_list = tag_t<
      binding_t<named_bi<"a", non_const>, bool&&>,
      binding_t<named_bi<"b", non_const, ref>, double&>,
      binding_t<named_bi<"c", non_const>, int>,
      binding_t<self_bi<non_const, ref>, char&&>>;

  double b = 2;
  static_assert(
      std::is_same_v<
          my_list,
          decltype(mut{
              "a"_id = true,
              "b"_id = const_ref{b},
              "c"_id = in_place<int>(3),
              self_id = const_ref('d')})::binding_list_t>);
  static_assert(
      std::is_same_v<
          my_list,
          decltype(mut{
              "a"_id = mut{true},
              "b"_id = mut_ref{b},
              "c"_id = mut{in_place<int>(3)},
              self_id = mut_ref{'d'}})::binding_list_t>);
  static_assert(
      std::is_same_v<
          my_list,
          decltype(mut{
              mut{"a"_id = true},
              mut_ref{"b"_id = b},
              mut{"c"_id = in_place<int>(3)},
              mut_ref{self_id = 'd'}})::binding_list_t>);

  return true;
}

static_assert(check_in_place_binding_modifier_distributive_property());

} // namespace folly::bind::ext
