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

#include <algorithm>
#include <type_traits>

/// See `folly/coro/safe/SafeAlias.h` for the high-level docs & definitions.
///
/// This is a MINIMAL header to let types declare their `safe_alias` level.
///
///  // An unsafe type
///  template <>
///  struct safe_alias_of<TypeContainsRefs>
///      : safe_alias_constant<safe_alias::unsafe> {};
///
///  // A simple type wrapper transparent to `safe_alias`
///  template <typename T>
///  struct safe_alias_of<MyWrapper<T>> : safe_alias_of<T> {};
///
/// See `detail/tuple.h` for an example of a composite type wrapper.

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
  // `closure_task`, a restricted-usage `safe_task`.  Other code should treat
  // this as `unsafe`.  `safe_task.h` & `Captures.h` explain the rationale.
  unsafe_closure_internal,
  // Implementation detail of `async_closure`, used for creating
  // `member_task`, a restricted-usage `safe_task`.  Other code should treat
  // this as `unsafe`.  Closure-related code that distinguishes this from
  // `unsafe_closure_internal` expects this value to be higher.
  unsafe_member_internal,
  // Used only in `async_closure*()` -- the minimum level it considers safe
  // for arguments, and the minimum level of `safe_task` it will emit.
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

// IMPORTANT:
//
// (1) Do NOT move the primary template in here!
//
//     The reason is that we REQUIRE the extra specializations in `SafeAlias.h`
//     before allowing `lenient_safe_alias_v` to conclude that something is
//     safe.  It would be very bad if a user forgot to include `SafeAlias.h`,
//     and was told that a type was safe, when it isn't.
//
// (2) Keep this SMALL -- pretty much everything in `folly/coro` includes this.
//
//     Please only put universally-applicable specializations here.  A good
//     check is: if you have to add an `#include`, it goes in `SafeAlias.h`.
//
// Future: Replace the SFINAE arg with `requires` once on C++20.
template <typename T, safe_alias /*default*/, typename /*SFINAE*/ = void>
struct safe_alias_of;

// `const` and `volatile` qualifiers don't affect the `safe_alias` measurement.
template <typename T, safe_alias Default>
struct safe_alias_of<const T, Default> : safe_alias_of<T, Default> {};
template <typename T, safe_alias Default>
struct safe_alias_of<volatile T, Default> : safe_alias_of<T, Default> {};

// Raw references & pointers are `unsafe`.
template <typename T, safe_alias Default>
struct safe_alias_of<T*, Default> : safe_alias_constant<safe_alias::unsafe> {};
template <typename T, safe_alias Default>
struct safe_alias_of<T&, Default> : safe_alias_constant<safe_alias::unsafe> {};
template <typename T, safe_alias Default>
struct safe_alias_of<T&&, Default> : safe_alias_constant<safe_alias::unsafe> {};

// `void` is incomplete and would fail the primary template
template <safe_alias Default>
struct safe_alias_of<void, Default>
    : safe_alias_constant<safe_alias::maybe_value> {};

// Most `folly` types annotate their safety via a member type alias. We do
// this not just because it's shorter, but also because member classes, like
// AsyncGenerator<>::NextAwaitable, are non-deducible, and thus cannot be
// targeted by specializations.
//
// This is in `SafeAlias-fwd.h`, since that allows the various task wrappers
// NOT to include the larger `SafeAlias.h`.
template <typename T, safe_alias Default>
struct safe_alias_of<
    T,
    Default,
    std::void_t<typename T::template folly_private_safe_alias_t<Default>>>
    : T::template folly_private_safe_alias_t<Default> {};

// Use this in APIs that take callable objects, since lambda captures present a
// particularly high risk for aliasing bugs. If you have a compile error:
//   - The best solution is to expose the correct `folly_private_safe_alias_t`
//     for your type.
//   - For async coroutines, use `async_closure()` or `safe_task.h`.
//   - Future: Also see `safe_bind`.
//   - For one-offs, `SafeAlias.h`  includes some `manual_safe_*` workarounds.
//     You MUST include a comment that proves your usage is safe.
template <typename T>
inline constexpr safe_alias strict_safe_alias_of_v =
    safe_alias_of<T, safe_alias::unsafe>::value;
// For low-risk APIs, where "vanilla C++ usability" outweighs safety.
template <typename T>
inline constexpr safe_alias lenient_safe_alias_of_v =
    safe_alias_of<T, safe_alias::maybe_value>::value;

// Utility for computing the `safe_alias_of` a composite type.
// Returns the lowest `safe_alias` level of all the supplied `Ts`.
template <safe_alias Default, typename... Ts>
struct safe_alias_of_pack {
  static constexpr auto value =
      std::min({safe_alias::maybe_value, safe_alias_of<Ts, Default>::value...});
};

} // namespace folly
