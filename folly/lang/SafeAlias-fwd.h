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

template <typename>
struct safe_alias_of;

template <typename T>
inline constexpr safe_alias safe_alias_of_v = safe_alias_of<T>::value;

namespace detail {
// Helper: Inspects its own template args for aliasing.
template <typename... Ts>
struct safe_alias_of_pack {
  static constexpr auto value =
      std::min({safe_alias::maybe_value, safe_alias_of_v<Ts>...});
};
} // namespace detail

} // namespace folly
