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

#include <folly/ConstexprMath.h>
#include <folly/Traits.h>

#include <type_traits>

namespace folly {
template <typename>
class rvalue_reference_wrapper;
} // namespace folly

namespace folly::detail::lite_tuple {
template <typename... Ts>
struct tuple;
} // namespace folly::detail::lite_tuple

/*
"Aliasing" is indirect access to memory via pointers or references.  It is
the major cause of memory-safety bugs in C++, but is also essential for
writing correct & performant C++ programs.  Fortunately,
  - Much business logic can be written in a pure-functional style, where
    only value semantics are allowed.  Such code is easier to understand,
    and has much better memory-safety.
  - When references ARE used, the most common scenario is passing a
    reference from a parent lexical scope to descendant scopes.

`safe_alias_of_v` is a _heuristic_ to check whether a type is likely to be
memory-safe in the above settings.  The `safe_alias` enum shows a hierarchy
of memory safety, but you only need to know about two:
  - `unsafe` -- e.g. raw pointers or references, and
  - `maybe_value` -- `int`, `std::pair<int, char>`, or `std::unique_ptr<Foo>`.

A user can easily bypass the heuristic -- since C++ lacks full reflection,
it is impossible to make this bulletproof.  Our goals are much more modest:
  - Make unsafe aliasing **more** visible in code review, and
  - Encourage programmers to use safe semantics by default.

The BIG CAVEATS are:

 - The "composition hole" -- i.e. aliasing hidden in structures.  We can't see
   unsafe class members, so `UnsafeStruct` below will be deduced to have
   `maybe_value` safety unless you specialize `safe_alias_for<UnsafeStruct>`.
     struct UnsafeStruct { int* rawPtr; };
   Future: Perhaps with C++26 reflection, this could be fixed.

   The "lambda hole" is a particularly easy instance of the "composition hole".
   With lambda captures, a parent needs just one `&` to let a child pass a
   soon-to-be-dangling reference up the stack.  E.g. this compiles:
       int* badPtr;
       auto t = async_closure(
         // LAMBDA HOLE: We can't tell this callable object is unsafe!
         bound_args{[&](int p) { *badPtr = p; }},
         [](auto fn) -> ClosureTask<void> {
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

You can teach `safe_alias_of_v` about your type by specializing
`folly::safe_alias_for`, as seen below.  Best practices:
  - Declare the specialization in the same header that declares your type.
  - Only use `maybe_value` if your type ACTUALLY follows value semantics.
  - Use `least_safe_alias()` to aggregate safety across pieces of a
    composite type.
  - Unless you're implementing an `async_closure`-integrated type, it is VERY
    unlikely that you should use `safe_alias::*_cleanup`.
*/
namespace folly {

// ENUM ORDER IS IMPORTANT!  Categories go from "least safe to most safe".
// Always use >= for safety gating.
//
// Note: Only `async_closure*()` from `folly coro/safe/` use the middle
// safety levels `shared_cleanup`, `after_cleanup_ref`, and
// `co_cleanup_safe_ref`. Normal user code should stick to `maybe_value` and
// `unsafe`.
enum class safe_alias {
  // Definitely has aliasing, we know nothing of the lifetime.
  unsafe,
  // Implementation detail of `async_closure`, used for creating
  // `ClosureTask`, a restricted-usage `SafeTask`.  Other code should treat
  // this as `unsafe`.  `SafeTask.h` & `Captures.h` explain the rationale.
  unsafe_closure_internal,
  // Implementation detail of `async_closure`, used for creating
  // `MemberTask`, a restricted-usage `SafeTask`.  Other code should treat
  // this as `unsafe`.  Closure-related code that distinguishes this from
  // `unsafe_closure_internal` expects this value to be higher.
  unsafe_member_internal,
  // Used only in `async_closure*()` -- the minimum level it considers safe
  // for arguments, and the minimum level of `SafeTask` it will emit.
  //   - Represents an arg that can schedule a cleanup callback on an
  //     ancestor's cleanup arg `A`.  This safety level cannot be stronger
  //     than `after_cleanup_ref` because otherwise such a ref could be
  //     passed to a cleanup callback on a different ancestor's cleanup arg
  //     `B` -- and `A` could be invalid by the time `B` runs.
  //   - Follows all the rules of `after_cleanup_ref`.
  //   - Additionally, when a `shared_cleanup` ref is passed to
  //     `async_closure`, it knows to mark its own args as `after_cleanup_ref`.
  //     This prevents the closure from passing its short-lived `capture`s
  //     into a new callback on the longer-lived `shared_cleanup` arg.
  //     Conversely, in the absence of `shared_cleanup` args, it is safe for
  //     `async_closure` to upgrade `after_cleanup_capture*<Ref>`s to
  //     `capture*<Ref>`s, since its cleanup will terminate before the
  //     parent's will start. Explained in detail in `Captures.md`.
  shared_cleanup,
  // `async_closure` won't take `unsafe*` args.  It is important that we
  // disallow `unsafe_closure_internal` in particular, since this is part of
  // the `Captures.h` mechanism that discourages moving `async_closure`
  // capture wrappers out of the closure that owns it (we can't prevent it).
  closure_min_arg_safety = shared_cleanup,
  // Used only in `async_closure*()` when it takes a `co_cleanup_capture` ref
  // from a parent:
  //   - NOT safe to reference from tasks spawned on `co_cleanup_capture` args.
  //   - Otherwise, just like `co_cleanup_safe_ref`.
  after_cleanup_ref,
  // Used only in `async_closure*()`:
  //   - Unlike `after_cleanup_ref`, is safe to reference from tasks spawned on
  //     `co_cleanup_capture` args -- because we know these belong to the
  //     current closure.
  //   - Outlives the end of the current closure's cleanup, and is thus safe to
  //     use in `after_cleanup{}` or sub-closures.
  //   - Safe to pass to sub-closures.
  //   - NOT safe to return or pass to callbacks from ancestor closures.
  co_cleanup_safe_ref,
  // Looks like a "value", i.e. alive as long as you hold it.  Remember
  // this is just a HEURISTIC -- a ref inside a struct will fool it.
  maybe_value
};

template <safe_alias Safety>
using safe_alias_constant = std::integral_constant<safe_alias, Safety>;

// Use only `safe_alias_of_v`, which removes CV qualifiers before testing.
template <typename T>
struct safe_alias_for
    : std::conditional_t<
          std::is_reference_v<T> || std::is_pointer_v<T>,
          safe_alias_constant<safe_alias::unsafe>,
          safe_alias_constant<safe_alias::maybe_value>> {};
template <typename T>
struct safe_alias_for<std::reference_wrapper<T>>
    : safe_alias_constant<safe_alias::unsafe> {};
template <typename T>
struct safe_alias_for<folly::rvalue_reference_wrapper<T>>
    : safe_alias_constant<safe_alias::unsafe> {};

template <typename T>
inline constexpr safe_alias safe_alias_of_v =
    safe_alias_for<std::remove_cv_t<T>>::value;

template <safe_alias... Vs>
constexpr safe_alias least_safe_alias(vtag_t<Vs...>) {
  return folly::constexpr_min(safe_alias::maybe_value, Vs...);
}

namespace detail {
// Helper: Inspects its own template args for aliasing.
template <typename... Ts>
struct safe_alias_for_pack {
  static constexpr auto value =
      folly::least_safe_alias(vtag<safe_alias_of_v<Ts>...>);
};
} // namespace detail

// Let `safe_alias_of_v` recursively inspect `std` containers that are likely
// to be involved in bugs.  If you encounter a memory-safety issue that
// would've been caught by this, feel free to extend this.
template <typename... As>
struct safe_alias_for<std::tuple<As...>> : detail::safe_alias_for_pack<As...> {
};
template <typename... As>
struct safe_alias_for<std::pair<As...>> : detail::safe_alias_for_pack<As...> {};
template <typename... As>
struct safe_alias_for<std::vector<As...>> : detail::safe_alias_for_pack<As...> {
};

// A `folly`-internal `std::tuple` mimic with good ctor/dtor ordering.
template <typename... As>
struct safe_alias_for<::folly::detail::lite_tuple::tuple<As...>>
    : detail::safe_alias_for_pack<As...> {};

// Recursing into `tag_t<>` type lists is nice for metaprogramming
template <typename... As>
struct safe_alias_for<::folly::tag_t<As...>>
    : detail::safe_alias_for_pack<As...> {};

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

template <safe_alias, typename T>
struct manual_safe_ref_t : std::reference_wrapper<T> {
  using typename std::reference_wrapper<T>::type;
  using std::reference_wrapper<T>::reference_wrapper;
};

template <safe_alias, typename T>
struct manual_safe_val_t {
  using type = T;

  template <typename... Args>
  manual_safe_val_t(Args&&... args) : t_(static_cast<Args&&>(args)...) {}
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
auto manual_safe_ref(T& t) {
  return manual_safe_ref_t<Safety, T>{t};
}
template <safe_alias Safety = safe_alias::maybe_value, typename T>
auto manual_safe_val(T t) {
  return manual_safe_val_t<Safety, T>{std::move(t)};
}
template <safe_alias Safety = safe_alias::maybe_value, typename Fn>
auto manual_safe_with(Fn&& fn) {
  using FnRet = decltype(static_cast<Fn&&>(fn)());
  return manual_safe_val_t<Safety, FnRet>{
      std::in_place_type<FnRet>, static_cast<Fn&&>(fn)};
}

template <safe_alias S, typename T>
struct safe_alias_for<manual_safe_ref_t<S, T>> : safe_alias_constant<S> {};
template <safe_alias S, typename T>
struct safe_alias_for<manual_safe_val_t<S, T>> : safe_alias_constant<S> {};

// Use `SafeTask<>` instead of `Task` to move tasks into other safe coro APIs.
//
// User-facing stuff from `Task.h` can trivially include unsafe aliasing,
// the `folly::coro` docs include hundreds of words of pitfalls.  The intent
// here is to catch people accidentally passing `Task`s into safer
// primitives, and breaking their memory-safety guarantess.
//
// Future: Move this into `Task.h` once `SafeAlias.h` is mature.
namespace coro {
template <typename T>
class TaskWithExecutor;
template <typename T>
class Task;
} // namespace coro
template <typename T>
struct safe_alias_for<::folly::coro::TaskWithExecutor<T>>
    : safe_alias_constant<safe_alias::unsafe> {};
template <typename T>
struct safe_alias_for<::folly::coro::Task<T>>
    : safe_alias_constant<safe_alias::unsafe> {};

// Future: Implement a `coro/safe` generator wrapper.
// Future: This `safe_alias_for` should sit in `AsyncGenerator.h` once
// `SafeAlias.h` is mature.
namespace coro {
template <typename, typename, bool>
class AsyncGenerator;
} // namespace coro
template <typename Ref, typename Val, bool Clean>
struct safe_alias_for<::folly::coro::AsyncGenerator<Ref, Val, Clean>>
    : safe_alias_constant<safe_alias::unsafe> {};

// Future: Move to `ViaIfAsync.h` once `SafeAlias.h` is mature.
namespace coro {
template <typename>
class NothrowAwaitable;
template <typename>
class TryAwaitable;
} // namespace coro
template <typename T>
struct safe_alias_for<::folly::coro::NothrowAwaitable<T>>
    : safe_alias_constant<safe_alias_of_v<T>> {};
template <typename T>
struct safe_alias_for<::folly::coro::TryAwaitable<T>>
    : safe_alias_constant<safe_alias_of_v<T>> {};

// Future: Move to `Noexcept.h` once `SafeAlias.h` is mature.
namespace coro {
template <typename, auto>
class AsNoexcept;
template <typename, auto>
class AsNoexceptWithExecutor;
namespace detail {
template <typename, auto>
class NoexceptAwaitable;
}
} // namespace coro
template <typename T, auto CancelCfg>
struct safe_alias_for<::folly::coro::AsNoexcept<T, CancelCfg>>
    : safe_alias_constant<safe_alias_of_v<T>> {};
template <typename T, auto CancelCfg>
struct safe_alias_for<::folly::coro::AsNoexceptWithExecutor<T, CancelCfg>>
    : safe_alias_constant<safe_alias_of_v<T>> {};
template <typename T, auto CancelCfg>
struct safe_alias_for<::folly::coro::detail::NoexceptAwaitable<T, CancelCfg>>
    : safe_alias_constant<safe_alias_of_v<T>> {};

} // namespace folly
