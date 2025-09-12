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

#include <folly/coro/safe/Captures.h>
#include <folly/lang/SafeClosure.h>
#include <folly/portability/GTest.h>

namespace folly {

using bind_info_t = folly::bind::ext::bind_info_t;
using category_t = folly::bind::ext::category_t;
using constness_t = folly::bind::ext::constness_t;

// `safe_closure` needs to transparently take capture-ref aargs -- that happens
// "for free" since capture-refs are copyable (UNLESS the source is `const
// capture<>`, see `DefineMovableDeepConstLrefCopyable.h`).
//
// These same tests cover capture-val args too (primarily, for "no outer coro"
// async closures), which works because `Captures.h` has a specialization for
// `safe_closure_arg_storage_helper<capture-val>`.

template <typename CaptureInnerT>
void check_with_mut_capture() {
  auto innerFn = [](auto c) {
    static_assert(std::is_same_v<decltype(c), coro::capture<int&>>);
    return *c;
  };
  int n = 7;
  coro::capture<CaptureInnerT> cn{
      coro::detail::capture_private_t{}, coro::detail::forward_bind_wrapper(n)};
  // `bind::args` would be `const`-by-default, but `const capture<int&>`
  // refuses to copy to `capture<int&>` for the (good) reason explained in
  // `DefineMovableDeepConstLrefCopyable.h`.  Therefore, `bind::mut` is
  // needed here -- contrast with `WithConstCaptureRef`.
  safe_closure fn{bind::mut{cn}, innerFn};
  static_assert(
      strict_safe_alias_of_v<decltype(fn)> == safe_alias::co_cleanup_safe_ref);
  static_assert(
      std::is_same_v<
          decltype(fn),
          safe_closure<
              decltype(innerFn),
              tag_t<
                  vtag_t<bind_info_t{category_t::unset, constness_t::mut}>,
                  detail::lite_tuple::tuple<coro::capture<int&>>>>>);
  EXPECT_EQ(7, fn());
}

TEST(SafeClosureTest, WithMutCapture) {
  check_with_mut_capture<int&>();
  check_with_mut_capture<int>();
}

template <typename CaptureInnerT>
void check_with_const_capture(auto innerFn) {
  int n = 7;
  coro::capture<CaptureInnerT> cn{
      coro::detail::capture_private_t{}, coro::detail::forward_bind_wrapper(n)};
  safe_closure fn{bind::constant{cn}, innerFn};
  static_assert(
      strict_safe_alias_of_v<decltype(fn)> == safe_alias::co_cleanup_safe_ref);
  static_assert(
      std::is_same_v<
          decltype(fn),
          safe_closure<
              decltype(innerFn),
              tag_t<
                  vtag_t<bind_info_t{category_t::unset, constness_t::constant}>,
                  detail::lite_tuple::tuple<coro::capture<int&>>>>>);
  EXPECT_EQ(7, fn());
}

TEST(SafeClosureTest, WithConstCapture) {
  // This arg type needs to either be a ref, or an explicit `capture<const
  // int&>`.  The reason is that `check_with_const_capture()` passes `c` as
  // `const capture<int&>&`, which isn't copyable, but **is** convertible.
  auto byValInnerFn = [](coro::capture<const int&> c) { return *c; };
  auto byRefInnerFn = [](auto& c) {
    static_assert(std::is_same_v<decltype(c), const coro::capture<int&>&>);
    return *c;
  };
  check_with_const_capture<int&>(byValInnerFn);
  check_with_const_capture<int&>(byRefInnerFn);
  check_with_const_capture<int>(byValInnerFn);
  check_with_const_capture<int>(byRefInnerFn);
}

// `LrefInvoke` and `RrefInvoke` test different aspects of the "example" from
// `SafeClosure.md`. It is impossible to test everything in one inv

// `Lref` refers to the closure being invoked as `fn(...)`.
TEST(SafeClosureTest, LrefInvoke) {
  auto innerFn = [](auto& a, auto& b, auto c, auto unboundArg) {
    static_assert(std::is_same_v<const std::unique_ptr<int>&, decltype(a)>);
    // `fn(...)` vs `std::as_const(fn)(...)` pass a different `unboundArg`
    if constexpr (std::is_same_v<char, decltype(unboundArg)>) {
      static_assert(std::is_same_v<const unsigned int&, decltype(b)>);
    } else {
      static_assert(
          std::is_same_v<int, decltype(unboundArg)> &&
          std::is_same_v<unsigned int&, decltype(b)>);
    }
    static_assert(std::is_same_v<int, decltype(c)>);
    return *a + b + c + unboundArg;
  };
  auto a = std::make_unique<int>(1000);
  unsigned int b = 300;
  auto cFn = [] { return 30; }; // + unboundArg of 7
  safe_closure fn{
      bind::args{
          // move `a` in; pass it by const ref
          std::move(a),
          // copy `b` in; pass it by mutable ref
          // causes `operator() const &` to `static_assert`
          bind::mut{b},
          // construct the prvalue, move it into `fn`
          // if you need prvalue semantics, see `bind::in_place*`.
          // when passing, decay-copy our stored argument
          bind::copy{cFn()}},
      innerFn};
  static_assert(
      strict_safe_alias_of_v<decltype(fn)> == safe_alias::maybe_value);
  static_assert(
      std::is_same_v<
          decltype(fn),
          safe_closure<
              decltype(innerFn),
              tag_t<
                  vtag_t<
                      bind_info_t{category_t::unset, constness_t::unset},
                      bind_info_t{category_t::unset, constness_t::mut},
                      bind_info_t{category_t::copy, constness_t::unset}>,
                  detail::lite_tuple::
                      tuple<std::unique_ptr<int>, unsigned int, int>>>>);

  static_assert(std::is_invocable_v<decltype(fn)&, int>);
  EXPECT_EQ(1337, fn(7));

  { // `const &`-qualified calls are fine without `bind::mut`
    safe_closure fn2{
        bind::args{std::make_unique<int>(1000), b, bind::copy{cFn()}}, innerFn};
    static_assert(std::is_invocable_v<const decltype(fn2)&, char>);
    EXPECT_EQ(1337, std::as_const(fn2)(char{7}));
  }

#if 0 // Manual test: `operator() const&` asserts on `bind::mut`.
  std::as_const(fn)(char{7});
#elif 0 // Manual test: `operator() &&` asserts on `bind::copy`.
  std::move(fn)(7);
#endif
  // Read on to `RrefInvoke` for the `operator() &&` tests.
}

// The closure is invoked as `std::move(fn)(...)`. Differs from `LrefInvoke` in
// that:
//  - `innerFn` takes `auto&& b`
//  - We must use `bind::move(cFn())` instead of `bind::copy`.
//  - Add `d` to also test `bind::move(std::move(d))`.
TEST(SafeClosureTest, RrefInvoke) {
  // Wart: if the user were to forget `&` in front of `a` or `b`, we would do
  // an extraneous copy.
  auto innerFn = [](auto& a, auto&& b, auto c, auto&& d, auto unboundArg) {
    static_assert(std::is_same_v<const std::unique_ptr<int>&, decltype(a)>);
    static_assert(std::is_same_v<unsigned int&&, decltype(b)>);
    static_assert(std::is_same_v<int, decltype(c)>);
    static_assert(std::is_same_v<char&&, decltype(d)>);
    return *a + b + c + d + unboundArg;
  };
  auto a = std::make_unique<int>(600); // + unboundArg of 400
  unsigned int b = 300;
  auto cFn = [] { return 30; };
  char d = 7;
  safe_closure fn{
      bind::args{
          std::move(a),
          bind::mut{b},
          bind::move{cFn()},
          // Move `d` in; pass it by rvalue ref.
          // Trips `static_asserts` in all overloads but `operator() &&`.
          bind::move{std::move(d)}},
      innerFn};
  static_assert(
      strict_safe_alias_of_v<decltype(fn)> == safe_alias::maybe_value);
  static_assert(
      std::is_same_v<
          decltype(fn),
          safe_closure<
              decltype(innerFn),
              tag_t<
                  vtag_t<
                      bind_info_t{category_t::unset, constness_t::unset},
                      bind_info_t{category_t::unset, constness_t::mut},
                      bind_info_t{category_t::move, constness_t::unset},
                      bind_info_t{category_t::move, constness_t::unset}>,
                  detail::lite_tuple::
                      tuple<std::unique_ptr<int>, unsigned int, int, char>>>>);

  static_assert(std::is_invocable_v<decltype(fn)&&, int>);
  EXPECT_EQ(1337, std::move(fn)(400));

  // Test `const &&` usage.  Since we don't provide an explicit `const &&`
  // overload, this resolves to `const&`.  The goal here is just to check that
  // we do something reasonable -- it's fine to update the API in the future.
  // When revising, note that it's debatable whether `bind::copy` should even
  // work with `const&&`, so you may change the API to go the other way.
  {
    const safe_closure fn2{bind::args{b, bind::copy{5}}, [](auto&, auto) {}};
    std::move(fn2)();
  }

#if 0 // Manual test: `operator() const&&` static-asserts on `bind::move`
  {
    const safe_closure fn2{bind::move{7}, [](auto&) {}};
    std::move(fn2)();
  }
#elif 0 // Manual test: `operator() const&&` static-asserts on `bind::mut`
  {
    const safe_closure fn2{bind::mut{b}, [](auto&) {}};
    std::move(fn2)();
  }
#elif 0 // Manual test: `operator() const&` static-asserts on `bind::move`.
  {
    const safe_closure fn2{bind::move{5}, [](auto&) {}};
    std::as_const(fn2)();
  }
#elif 0 // Manual test: `operator() &` static-asserts on `bind::move`.
  fn(400);
#endif
}

} // namespace folly
