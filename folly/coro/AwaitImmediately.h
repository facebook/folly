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

#include <type_traits>

#include <folly/Utility.h>

#if FOLLY_HAS_COROUTINES

namespace folly::coro {

// ## What are immediately-awaitable types, and how should I handle them?
//
// When `must_await_immediately_v<A> == true`, this indicates that the
// awaitable or semiawaitable `A` should be `co_await`ed in the full-expression
// that created it.  For an example, see `NowTask.h`.  Using immediate
// awaitables reduces the risk of lifetime bugs.
//
// To create a new immediately-awaitable type, follow this protocol:
//   - Derive from `public AddMustAwaitImmediately<YourBase>`.
//   - Implement `getUnsafeMover(ForMustAwaitImmediately)`, but first read "A
//     note on object slicing" below, and review the existing examples.
//
// To handle immediately-awaitables, `folly::coro` APIs must follow these rules:
//
//  (1) NEVER expose non-static member functions for actions that consume the
//      (semi)awaitable.  For example, `task.scheduleOn()` was removed in favor
//      of `co_withExecutor()`, `.start()` and `.semi()` are unsafe, etc.
//
//      INSTEAD: Use static member functions, or ADL CPOs, which take the
//      (semi)awaitable by-value.
//
//  (2) If your API must also support await-by-reference for types like
//      `Baton`, then you must bifurcate the API on the return value of
//      `must_await_immediately_v`.  Grep for examples.
//         - `true`: Pass-by-value
//         - `false`: Pass-by-forwarding reference
//
//  (3) Immediately-awaitable types are immovable, but you may need to move
//      them internally in your library implementation.  For this, you can use
//      the callable from `mustAwaitImmediatelyUnsafeMover(std::move(t))`.  For
//      example, you can move the mover into another coroutine, and then invoke
//      its `operator()` to reconstitute the (semi)awaitable.  Most often, you
//      will invoke the mover inline.
//
//      DANGER: You should NOT use this to move immovable types outside of
//      `folly::coro` library internals, where the lifetime safety is assured
//      via pass-by-value from (1) or (2).
//
// Caveat: If you encounter a public `folly::coro` API that is does not yet
// handle immediately-awaitable types, and simply takes the awaitable by `&&`,
// please fix it via one of these paths:
//   - If possible, switch it to take all (semi)awaitables by-value.
//   - If not, branch the API as in (2) above.
//   - Ask for help in the Coroutines group.

namespace detail {

template <typename T>
using must_await_immediately_of_ =
    typename T::folly_private_must_await_immediately_t;

template <typename Void, typename T>
struct must_await_immediately_ {
  static_assert(
      require_sizeof<T>, "`must_await_immediately_t` on incomplete type");
  using type = std::false_type;
};

template <>
struct must_await_immediately_<void, void> {
  using type = std::false_type;
};

template <typename T>
struct must_await_immediately_<void_t<must_await_immediately_of_<T>>, T> {
  // We _could_ do an "is `T` immovable" check, but the cost/benefit seems low.
  // That would only guard against a users wrongly adding this to their type:
  //   folly_private_must_await_immediately_t = std::true_type
  // instead of inheriting from `AddMustAwaitImmediately<>`.
  using type = must_await_immediately_of_<T>;
};

} // namespace detail

template <typename T>
using must_await_immediately_t =
    typename detail::must_await_immediately_<void, T>::type;

template <typename T>
inline constexpr bool must_await_immediately_v =
    must_await_immediately_t<T>::value;

// To make a (semi)awaitable immediate, have it publicly inherit from
// `AddMustAwaitImmediately<InnerSemiAwaitable>`.  It is templated on the
// "inner type" so that all bases are distinct in a tower-of-wrappers, like
// `TryAwaitable<NowTask<T>>`.  Repeating a base would break the empty basea
// optimization (EBO).
template <typename InnerT>
struct AddMustAwaitImmediately : public InnerT {
  using folly_private_must_await_immediately_t = std::true_type;

  using InnerT::InnerT;

  // Avoiding `NonCopyableNonMovable` to avoid breaking EBO.
  ~AddMustAwaitImmediately() = default;
  AddMustAwaitImmediately(AddMustAwaitImmediately&&) = delete;
  AddMustAwaitImmediately& operator=(AddMustAwaitImmediately&&) = delete;
  AddMustAwaitImmediately(const AddMustAwaitImmediately&) = delete;
  AddMustAwaitImmediately& operator=(const AddMustAwaitImmediately&) = delete;
};

// See `mustAwaitImmediatelyUnsafeMover()` for the docs.  In short, for an
// `Outer` that is immovable, this stores an unwrapped `Inner` (semi)awaitable
// that can reconstitute `Outer` on `operator()`.
//
// DANGER: Before returning this class from your `getUnsafeMover()`, you must
// review "A note on object slicing" and make sure your usage isn't affected.
template <typename Outer, typename InnerMover>
class MustAwaitImmediatelyUnsafeMover {
 private:
  InnerMover mover_;

 public:
  // `Outer*` is just for type deduction and should be `nullptr`.
  MustAwaitImmediatelyUnsafeMover(Outer*, InnerMover m) : mover_(std::move(m)) {
    // See "A note on object slicing" below
    static_assert(
        sizeof(Outer) == sizeof(decltype(FOLLY_DECLVAL(InnerMover)())));
  }
  Outer operator()() && { return Outer{std::move(mover_)()}; }
};

// Analog of `MustAwaitImmediatelyUnsafeMover` for movable (semi)awaitables.
template <typename T>
struct NoOpMover {
 private:
  T t_;

 public:
  explicit NoOpMover(T t) : t_(std::move(t)) {}
  T operator()() && { return std::move(t_); }
};

// Overload tag / passkey for the customizable method `getUnsafeMover`.
struct ForMustAwaitImmediately {};

namespace detail {
template <typename T>
using unsafe_mover_for_must_await_immediately_t =
    decltype(FOLLY_DECLVAL(T).getUnsafeMover(ForMustAwaitImmediately{}));
}

// After taking an immediately-(semi)awaitable by-value, this lets
// `folly::coro` libraries move it internally.  They MUST make sure not to let
// the awaitable be used outside of its original full-expression.
//
// This wraps `getUnsafeMover` for types that implement it, and provides
// a no-op fallback for those that don't.
//
// ## A note on object slicing -- for `getUnsafeMover` implementations
//
// The default `getUnsafeMover()` implementations return `NoOpMover` or
// `MustAwaitImmediatelyUnsafeMover`.  The net effect is that the mover
// stores an unwrapped, inner type that is movable (like `Task`), and
// its `operator()` reconstitues the original "outer" type.
//
// This is fine for type-only wrappers.  But, object slicing is a danger for
// wrappers that affect the object's lifetime management or layout. For example:
//   - If "outer" adds a new member, this would be discarded.  The current
//     `getUnsafeMover`s compare before/after `sizeof`.  Do that in new ones!
//   - If the wrapper customizes destruction, move, copy, or assignment, then
//     the wrapping/unwrapping will cause the custom logic will run at an
//     unexpected time.  Such wrapper types MUST customize `getUnsafeMover` to
//     return a custom mover that handles this correctly.
template <
    typename Awaitable,
    typename DetectRes = detected_or<
        NoOpMover<Awaitable>,
        detail::unsafe_mover_for_must_await_immediately_t,
        Awaitable>>
// CAREFUL: Passing by `&&` can violate the immediately-awaitable restriction!
typename DetectRes::type mustAwaitImmediatelyUnsafeMover(
    Awaitable&& awaitable) {
  if constexpr (DetectRes::value_t::value) {
    return static_cast<Awaitable&&>(awaitable).getUnsafeMover(
        ForMustAwaitImmediately{});
  } else {
    return NoOpMover<Awaitable>{static_cast<Awaitable&&>(awaitable)};
  }
}

} // namespace folly::coro

#endif
