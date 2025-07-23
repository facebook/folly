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
#include <folly/lang/SafeAlias-fwd.h>

#include <type_traits>

namespace folly {
template <typename> // Forward-decl to avoid `RValueReferenceWrapper.h` dep
class rvalue_reference_wrapper;
} // namespace folly

/*
"Aliasing" is indirect access to memory via pointers or references.  It is
the major cause of memory-safety bugs in C++, but is also essential for
writing correct & performant C++ programs.  Fortunately,
  - Much business logic can be written in a pure-functional style, where
    only value semantics are allowed.  Such code is easier to understand,
    and has much better memory-safety.
  - When references ARE used, the most common scenario is passing a
    reference from a parent lexical scope to descendant scopes.

`strict_safe_alias_of_v` / `lenient_safe_alias_of_v` are _heuristics_ to check
whether a type is likely to be memory-safe in the above settings.  The
`safe_alias` enum shows a hierarchy of memory safety, but you only need to know
about two:
  - `unsafe` -- e.g. raw pointers or references, and
  - `maybe_value` -- `int`, `std::pair<int, char>`, or `std::unique_ptr<Foo>`.

A user can easily bypass the heuristic -- since C++ lacks full reflection,
it is impossible to make this bulletproof.  Our goals are much more modest:
  - Make unsafe aliasing **more** visible in code review, and
  - Encourage programmers to use safe semantics by default.

The BIG CAVEATS are:

 - The "composition hole" -- i.e. aliasing hidden in structures.  We can't see
   unsafe class members, so `UnsafeStruct` below will be deduced to have
   `maybe_value` safety unless you specify a `safe_alias` value:
     struct UnsafeStruct {
       int* rawPtr;
       // Without this, most APIs will treat `UnsafeStruct` as `maybe_value`
       template <safe_alias>
       using folly_private_safe_alias_t =
           safe_alias_constant<safe_alias::unsafe>;
     };
   Future: Perhaps with C++26 reflection, this could be fixed.

   The "lambda hole" is a particularly easy instance of the "composition hole".
   With lambda captures, a parent needs just one `&` to let a child pass a
   soon-to-be-dangling reference up the stack.  E.g. this compiles:
       int* badPtr;
       auto t = async_closure(
         // LAMBDA HOLE: We can't tell this callable object is unsafe!
         bound_args{[&](int p) { *badPtr = p; }},
         [](auto fn) -> closure_task<void> {
           int i = 5;
           fn(i); // FAILURE: Dereferencing uninitialized `badPtr`.
           co_return;
         });

 - Nullability & pointer stability: These hazards are not very specific to
   coroutines, and the current design of `folly/coro/safe` largely avoids
   unstable containers.  Nonetheless, you must beware container mutation is
   an easy way to invalidate `safe_alias` memory-safety measurements.  For
   example `unique_ptr<int>` and `vector<int>` have `maybe_value` safety.
   However, if you mutate them (`reset()`, `clear()`, etc), that would
   invalidate any async references (e.g.  `Captures.h`) pointing inside.
   Luckily, there's no implicit way of getting a safe reference to inside
   regular containers.  However, it is recommended to reduce accidental
   nullability where possible.  For example, `capture<unique_ptr<T>>`
   exposes `reset()`, but `capture_indirect<unique_ptr<T>>` hides it behind
   `get_underlying_unsafe()`.  Better yet, `capture<AsyncObjectPtr<T>>`
   blocks the underlying `clear()` method entirely.

If you need to bypass this control, prefer the `manual_safe_*` wrappers
below, instead of writing a custom workaround.  Always explain why it's safe.

To teach `safe_alias_of` about your type, include `SafeAlias-fwd.h` and either:
 1) Add a member type alias to your class:
    template <safe_alias Default>
    using using folly_private_safe_alias_t = safe_alias_of<Inner, Default>;
 2) Specialize `folly::safe_alias_of<YourT>`.

When adding `safe_alias` annotations to types, stick to these principles:
  - Always mark the `safe_alias` level in the header that declares your type.
    For `std` types you cannot change, add the specialization here, in
    `SafeAlias.h`.  Since we cannot forward-declare from `std`, this
    unfortunately imposes a tradeoff between build cost and safety.  Commonly
    used containers are worth the cost.  For less-commonly used containers, we
    could develop a multi-header setup, plus some linter coverage to ensure the
    right headers ultimately do get included.
  - Only use `maybe_value` if your type ACTUALLY follows value semantics.
  - Unless you're implementing an `async_closure`-integrated type, it is VERY
    unlikely that you should use anything besides `unsafe` or `maybe_value`.
  - Use `safe_alias_of_pack` to aggregate safety for a multi-part type.
*/
namespace folly {

// The default safety measurements for types that don't specify one.  Note that
// `SafeAlias-fwd.h` already marks raw pointers & refs as `unsafe`, and peels
// off CV qualifiers from the type being tested.
//
// See also: `strict_safe_alias_of_v` / `lenient_safe_alias_of_v`.
//
// As explained in `SafeAlias-fwd.h`, do NOT move this to the `fwd` header.
// This is commonly used with `lenient_safe_alias_of_v`, so the default
// behavior is permissive.  Therefore, to guarantee safety, this primary
// template must be colocated with the other specializations below.
template <typename T, safe_alias Default, typename /*SFINAE*/>
struct safe_alias_of
    : safe_alias_constant<
          // Stateless & fundamental types are always safe -- `void` is covered
          // in `SafeAlias-fwd.h`.  Function pointers are covered below, on the
          // the potentially incorrect theory that adding 2 specializations is
          // cheaper to compile than this test here:
          //   std::is_pointer_v<T> &&
          //   std::is_function_v<std::remove_pointer_t<T>>
          //
          // Check `sizeof()` to avoid `is_empty` UB on incomplete types.
          (std::is_fundamental_v<T> || (sizeof(T) >= 0 && std::is_empty_v<T>))
              ? safe_alias::maybe_value
              : Default> {};

// Reference wrappers are unsafe.
template <typename T, safe_alias Default>
struct safe_alias_of<std::reference_wrapper<T>, Default>
    : safe_alias_constant<safe_alias::unsafe> {};
template <typename T, safe_alias Default>
struct safe_alias_of<folly::rvalue_reference_wrapper<T>, Default>
    : safe_alias_constant<safe_alias::unsafe> {};

// Function pointers are stateless, and thus also safe (see primary template).
template <typename R, typename... Args, safe_alias Default>
struct safe_alias_of<R (*)(Args...), Default>
    : safe_alias_constant<safe_alias::maybe_value> {};
template <typename R, typename... Args, safe_alias Default>
struct safe_alias_of<R (*)(Args...) noexcept, Default>
    : safe_alias_constant<safe_alias::maybe_value> {};

// Let `safe_alias_of` recursively inspect `std` containers that are likely to
// be involved in bugs.  If you encounter a memory-safety issue that would've
// been caught by this, feel free to extend this.
template <typename... As, safe_alias Default>
struct safe_alias_of<std::tuple<As...>, Default>
    : safe_alias_of_pack<Default, As...> {};
template <typename... As, safe_alias Default>
struct safe_alias_of<std::pair<As...>, Default>
    : safe_alias_of_pack<Default, As...> {};
template <typename... As, safe_alias Default>
struct safe_alias_of<std::vector<As...>, Default>
    : safe_alias_of_pack<Default, As...> {};

// Recursing into `tag_t<>` type lists is nice for metaprogramming
template <typename... As, safe_alias Default>
struct safe_alias_of<::folly::tag_t<As...>, Default>
    : safe_alias_of_pack<Default, As...> {};

// IMPORTANT: If you use the `manual_safe_` escape-hatch wrappers, you MUST
// comment with clear proof of WHY your usage is safe.  The goal is to
// ensure careful review of such code.
//
// Careful: With the default `Safety`, the contained value or reference can be
// passed anywhere -- the wrapper pretends to be a value type.
//
// If you know a more restrictive safety level for your ref, annotate it to
// improve safety:
//  - `after_cleanup_ref` for things owned by co_cleanup args of this closure,
//  - `co_cleanup_safe_ref` for refs to non-cleanup args owned by this closure,
//    or any ancestor closure.
//
// The types are public since they may occur in user-facing signatures.

template <safe_alias S, typename T>
struct manual_safe_ref_t : std::reference_wrapper<T> {
  using typename std::reference_wrapper<T>::type;
  using std::reference_wrapper<T>::reference_wrapper;

  template <safe_alias>
  using folly_private_safe_alias_t = safe_alias_constant<S>;
};

template <safe_alias S, typename T>
struct manual_safe_val_t {
  using type = T;

  template <safe_alias>
  using folly_private_safe_alias_t = safe_alias_constant<S>;

  template <typename... Args>
  explicit manual_safe_val_t(Args&&... args)
      : t_(static_cast<Args&&>(args)...) {}
  template <typename Fn>
  manual_safe_val_t(std::in_place_type_t<T>, Fn fn) : t_(fn()) {}

  T& get() & noexcept { return t_; }
  operator T&() & noexcept { return t_; }
  const T& get() const& noexcept { return t_; }
  operator const T&() const& noexcept { return t_; }
  T&& get() && noexcept { return std::move(t_); }
  operator T&&() && noexcept { return std::move(t_); }

 private:
  T t_;
};

template <safe_alias Safety = safe_alias::maybe_value, typename T = void>
manual_safe_ref_t<Safety, T> manual_safe_ref(T& t) {
  return manual_safe_ref_t<Safety, T>{t};
}
template <safe_alias Safety = safe_alias::maybe_value, typename T>
manual_safe_val_t<Safety, T> manual_safe_val(T t) {
  return manual_safe_val_t<Safety, T>{std::move(t)};
}
template <safe_alias Safety = safe_alias::maybe_value, typename Fn>
auto manual_safe_with(Fn&& fn) {
  using FnRet = decltype(static_cast<Fn&&>(fn)());
  return manual_safe_val_t<Safety, FnRet>{
      std::in_place_type<FnRet>, static_cast<Fn&&>(fn)};
}

template <safe_alias S, typename Fn>
class manual_safe_callable_t {
 private:
  Fn fn_;

 public:
  template <safe_alias>
  using folly_private_safe_alias_t = safe_alias_constant<S>;

  explicit manual_safe_callable_t(Fn fn) : fn_(std::move(fn)) {}

  template <typename... Args>
  auto operator()(Args&&... args) &
      // Same as FOLLY_DETAIL_FORWARD_BODY, but we don't yet depend on Utility.h
      noexcept(noexcept(fn_(static_cast<Args&&>(args)...)))
          -> decltype(fn_(static_cast<Args&&>(args)...)) {
    return fn_(static_cast<Args&&>(args)...);
  }

  template <typename... Args>
  auto operator()(Args&&... args) const& noexcept(
      noexcept(std::as_const(fn_)(static_cast<Args&&>(args)...)))
      -> decltype(std::as_const(fn_)(static_cast<Args&&>(args)...)) {
    return std::as_const(fn_)(static_cast<Args&&>(args)...);
  }

  template <typename... Args>
  auto operator()(Args&&... args) && noexcept(
      noexcept(std::move(fn_)(static_cast<Args&&>(args)...)))
      -> decltype(std::move(fn_)(static_cast<Args&&>(args)...)) {
    return std::move(fn_)(static_cast<Args&&>(args)...);
  }
};

// APIs taking callables should typically use `strict_safe_alias_of_v`, due to
// the risk posed by lambda captures.  Future: Instead, almost always prefer
// `safe_bind`, since that correctly measures safety for you.
template <
    safe_alias Safety = safe_alias::maybe_value,
    typename Fn,
    typename UncvrefFn = remove_cvref_t<Fn>>
manual_safe_callable_t<Safety, UncvrefFn> manual_safe_callable(Fn&& fn) {
  return manual_safe_callable_t<Safety, UncvrefFn>{static_cast<Fn&&>(fn)};
}

} // namespace folly
