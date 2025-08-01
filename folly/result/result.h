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

#include <folly/ExceptionWrapper.h>
#include <folly/Expected.h>
#include <folly/OperationCancelled.h>
#include <folly/lang/Align.h> // for `hardware_constructive_interference_size`
#include <folly/lang/RValueReferenceWrapper.h>
#include <folly/portability/GTestProd.h>

/// Read the full docs in `result.md`!
///
/// `result<T>` resembles `std::variant<T, std::exception_ptr, stopped_result>`,
/// but is cheaper, and targets "never empty" semantics in C++23.  Its intended
/// use-case is a "better `Try<T>`", both in sync & `folly::coro` code:
///
///   - No "empty state" wart -- all state of `result` have a clear meaning,
///     as far as control flow is concerned.
///
///   - Easy exception checks:
///       if (auto* ex = folly::get_exception<Ex>(res)) { /*...*/ }
///
///   - User-friendly constructors & conversions -- you can write
///     `result<T>`-returning functions as-if they returned `T`, while returning
///     returning `non_value_result{YourException{...}}` on error.
///
///   - Can store & and && references.  Think of them as syntax sugar for
///     `std::reference_wrapper` and `folly::rvalue_reference_wrapper`.
///
///        struct FancyIntMap {
///          int n;
///          result<int&> at(int i) {
///            if (n + i == 42) { return std::ref(n); }
///            return non_value_result{std::out_of_range{"FancyIntMap"}};
///          }
///        };
///        FancyIntMap m{.n = 12};
///        int& n1 = co_await m.at(30); // points at 12
///        result<int&> rn2 = m.at(20); // has error
///        co_return n1 + co_await std::move(rn2); // propagates error
///
///     Key things to remember:
///       - `result<V&&>` is "use-once" -- and it must be r-value qualified to
///         access the reference inside.
///       - `const result<V&>` gives non-`const` access to `V&`, just as `const
///         result<V*>` would.
///
///   - `has_stopped()` & `stopped_result` to nudge `folly` to the C++26 idea
///     that cancellation is NOT an error, see https://wg21.link/P1677 & P2300.
///
///   - Easy short-circuiting of "error" / "stopped" status to the caller:
///     * In `folly::coro` coroutines:
///       - `co_await co_await_result(x())` makes `result<X>`, does not throw.
///       - `co_await co_ready(syncResultFn())` extracts `T` from a
///          `result<T>`, or propagates error/stopped.
///       - `co_yield co_result(std::move(res))` returns a `result<T>`.
///     * In synchronous `result<T>` coroutines,
///       - `co_await std::move(res)` and `folly::copy(res)` give you `T`,
///       - `co_await std::ref(res)` gives you `T&` -- ditto for `std::cref`
///         and `folly::cref`.
///     * While you should strongly prefer to write `result<T>` coroutines,
///       propagation in non-coroutine `result<T>` functions is also easy:
///         if (!res.has_value()) {
///           return std::move(res).non_value();
///         }
///
///   - `result` is mainly used for return values -- implying single ownership.
///     For this reason, it encourages moves over copies (with a few carve-outs
///     for better usability), which also helps prevent perf bugs.
///
/// Note: Unlike `Try`, `non_value_result` (and thus `result<T>` in a non-value
/// state) will `std::terminate` in debug builds if you attempt to construct it,
/// or access it while it contains either of:
///
///   - `OperationCancelled` -- the header explains why user code should not
///     use that exception.  Instead, store `stopped_result`, and use
///     `has_stopped()` to check for its presence.
///
///   - An empty `std::exception_ptr`.  For prior art, consider that
///     `exception_wrapper::throw_exception` unconditionally calls
///     `std::terminate` when the wrapper is empty.  By explicitly specifying
///     this as out-of-contract, and validating eagerly, we reserve this
///     representation to potentially mean something else in the future.

#if FOLLY_HAS_RESULT

namespace folly {

struct OperationCancelled;

namespace detail {
// In order to give `result` a stronger contract, debug builds prevent `result`
// and `non_value_result` from ingesting empty `std::exception_ptr`s, and ones
// with `OperationCancelled`.
//
// In prod, neither check is done since the legacy behaviors are "okay"-ish:
//   - Empty `std::exception_ptr`s, while nonsensical in the context of
//     `result`, are safe to use unless you call `throw_exception()`.  And,
//     unfortunately, `co_yield co_error(exception_wrapper{})` compiles.
//   - As of 2025, erroring with `OperationCancelled` is the implementation of
//     `co_yield co_canceled`, and some code paths actually rely on this, often
//     erroneously (see `coro/Retry.h`).  So, even as we work to reduce
//     reliance on this in anticipation of C++26 "stopped" semantics, for
//     the foreseeable future it will "sort of work".
void fatal_if_exception_wrapper_invalid(const exception_wrapper&);
inline void dfatal_if_exception_wrapper_invalid(const exception_wrapper& ew) {
  // This code path could be hot in production code, so there's no branch or
  // logging in opt builds.
  if constexpr (kIsDebug) {
    fatal_if_exception_wrapper_invalid(ew);
  }
}
} // namespace detail

// Place this into `result` or `non_value_result` to signal that a work-tree
// was stopped (aka cancelled).  You can also `co_await stopped_result` from
// `result` coroutines.
struct stopped_result_t {};
inline constexpr stopped_result_t stopped_result;

// NB: Copying `non_value_result` is ~25ns due to `std::exception_ptr` atomics.
// Unlike `result`, it is implicitly copyable, because:
//   - Common usage involves only rvalues, so the risk of perf bugs is low.
//   - `folly::Expected` assumes that the error type is copyable, and it's
//     too convenient an implementation not to use.
class non_value_result {
 private:
  exception_wrapper ew_;

  non_value_result(std::in_place_t, exception_wrapper ew)
      : ew_(std::move(ew)) {}

  template <typename Ex, typename EW>
  static Ex* get_exception_impl(EW& ew) {
    return folly::get_exception<Ex>(ew);
  }

 public:
  /// Future: Fine to make implicit if a good use-case arises.
  explicit non_value_result(stopped_result_t)
      : ew_(make_exception_wrapper<OperationCancelled>()) {}
  non_value_result& operator=(stopped_result_t) {
    ew_ = make_exception_wrapper<OperationCancelled>();
    return *this;
  }

  /// Use this ctor to report errors from `result` coroutines & functions:
  ///   co_await non_value_result{YourError{...}};
  ///
  /// Design note: We do NOT want most users to construct `non_value_result`
  /// from type-erased `std::exception_ptr` or `folly::exception_wrapper`,
  /// because that would block RTTI-avoidance optimizations for `result` code.
  explicit non_value_result(std::derived_from<std::exception> auto ex)
      : ew_(std::in_place, std::move(ex)) {
    static_assert(
        !std::is_same_v<decltype(ex), OperationCancelled>,
        // The reasons for this are discussed in `folly/OperationCancelled.h`.
        "Do not use `OperationCancelled` in new user code. Instead, construct "
        "your `result` or `non_value_result` via `stopped_result`");
  }

  bool has_stopped() const { return ew_.get_exception<OperationCancelled>(); }

  // Implement the `folly::get_exception<Ex>(res)` protocol
  template <typename Ex>
  const Ex* get_exception(get_exception_tag_t) const noexcept {
    static_assert( // Note: `OperationCancelled` is final
        !std::is_same_v<const OperationCancelled, const Ex>,
        "Test results for cancellation via `has_stopped()`");
    return folly::get_exception<Ex>(ew_);
  }
  template <typename Ex>
  Ex* get_mutable_exception(get_exception_tag_t) noexcept {
    static_assert( // Note: `OperationCancelled` is final
        !std::is_same_v<const OperationCancelled, const Ex>,
        "Test results for cancellation via `has_stopped()`");
    return folly::get_mutable_exception<Ex>(ew_);
  }

  // AVOID. Throw-catch costs upwards of 1usec.
  [[noreturn]] exception_wrapper throw_exception() const {
    detail::dfatal_if_exception_wrapper_invalid(ew_);
    ew_.throw_exception();
  }

  /// AVOID.  Use `non_value_result(YourException{...})` if at all possible.
  /// Add a `std::in_place_type_t<Ex>` constructor if needed.
  ///
  /// Provided for compatibility with existing `exception_wrapper` code.  It
  /// has several downsides for `result`-first code:
  ///   - It is a debug-fatal invariant violation to pass in an
  ///     `exception_wrapper` that is empty or has `OperationCancelled`.
  ///     See the `dfatal_if_exception_wrapper_invalid` doc.
  ///   - Not knowing the static exception type blocks optimizations that can
  ///     otherwise help avoid RTTI on error paths.
  static non_value_result from_exception_wrapper(exception_wrapper ew) {
    detail::dfatal_if_exception_wrapper_invalid(ew);
    return non_value_result{std::in_place, std::move(ew)};
  }

  /// AVOID. Use `folly::get_exception<Ex>(r)` to check for specific exceptions.
  /// It may be OK to add more specific accessors to `non_value_result`, see
  /// `throw_exception()` for an example.
  ///
  /// INVARIANT: Ensure `!has_stopped()`, or you will see a debug-fatal.
  ///
  /// See `from_exception_wrapper` for the downsides and the rationale.
  exception_wrapper to_exception_wrapper() && {
    detail::dfatal_if_exception_wrapper_invalid(ew_);
    return std::move(ew_);
  }

  friend inline bool operator==(
      const non_value_result& lhs, const non_value_result& rhs) {
    return lhs.ew_ == rhs.ew_;
  }

  // DO NOT USE these "legacy" functions outside of `folly` internals. Instead:
  //   - `non_value_result(YourException{...})` whenever you statically know
  //     the exception type (feel free to add `std::in_place_type_t` support).
  //   - `non_value_result::from_exception_wrapper()` only when you MUST pay
  //     for RTTI, such as "thrown exceptions".
  //
  // See `OperationCancelled.h` for how to handle cancellation.  In short: use
  // `get_exception<MyErr>(res)` or `has_stopped()`.
  //
  // These internal-only functions let the `folly::coro` implementation ingest
  // `std::exception_ptr`s containing `OperationCancelled` made via
  // `folly::coro::co_cancelled`, without incurring the 20-80ns+ cost of
  // eagerly eagerly testing whether it contains `OperationCancelled`.
  static non_value_result make_legacy_error_or_cancellation(
      exception_wrapper ew) {
    return {std::in_place, std::move(ew)};
  }
  exception_wrapper get_legacy_error_or_cancellation() && {
    return std::move(ew_);
  }
};

template <typename T = void>
class result;

namespace detail {

template <typename>
struct result_promise_return;
template <typename, typename = void>
struct result_promise;
struct result_await_suspender;

// These errors are `detail` because they are only exposed on invariant
// violations in opt builds -- they are NOT part of the public API.
struct bad_result_access_error : public std::exception {};
// Future: Remove this one when we can use never-empty `std::expected`.
struct empty_result_error : public std::exception {};

// Future: To mitigate the risk of `bad_alloc` at runtime, these singletons
// should be eagerly instantiated at program start.  One way is to have a
// `shouldEagerInit` singleton in charge of this, and tell the users to do
// this on startup:
//   folly::SingletonVault::singleton()->doEagerInit();
const non_value_result& dfatal_get_empty_result_error();
const non_value_result& dfatal_get_bad_result_access_error();

template <typename T>
using result_ref_wrap = conditional_t< // Reused by `result_generator`
    std::is_rvalue_reference_v<T>,
    rvalue_reference_wrapper<std::remove_reference_t<T>>,
    conditional_t<
        std::is_lvalue_reference_v<T>,
        std::reference_wrapper<std::remove_reference_t<T>>,
        T>>;

// Shared implementation for `T` non-`void` and `void`
template <typename Derived, typename T>
class result_crtp {
  static_assert(!std::is_same_v<non_value_result, std::remove_cvref_t<T>>);
  static_assert(!std::is_same_v<stopped_result_t, std::remove_cvref_t<T>>);

 public:
  using value_type = T;

 protected:
  using storage_type = detail::result_ref_wrap<lift_unit_t<T>>;
  static_assert(!std::is_reference_v<storage_type>);

  using expected_t = Expected<storage_type, non_value_result>;

  expected_t exp_;

  template <typename>
  friend class folly::result; // The simple conversion ctor uses `exp_`

  friend struct detail::result_promise<T>;
  friend struct detail::result_promise_return<T>;
  friend struct detail::result_await_suspender;

  friend inline bool operator==(const result_crtp& a, const result_crtp& b) {
    // FIXME: This logic is meant to follow `std::expected`, so once that's in
    // use, this operator becomes `a.exp_ == b.exp_`, or simply ` = default;`.
    if (a.exp_.hasValue()) {
      return b.exp_.hasValue() && a.exp_.value() == b.exp_.value();
    } else if (a.exp_.hasError()) {
      return b.exp_.hasError() && a.exp_.error() == b.exp_.error();
    } else { // `a` empty
      return b.is_expected_empty(); // equal iff both are empty
    }
  }
  template <typename ResultT>
  static Derived rewrapping_result_convert(ResultT&& rt) {
    static_assert(is_instantiation_of_v<result, std::remove_cvref_t<ResultT>>);
    if (FOLLY_LIKELY(rt.has_value())) {
      // Implicitly convert `ResultT::value_type` to `Derived`.
      return {std::forward<ResultT>(rt).value_or_throw()};
    }
    // `Derived` lets the rewrapping conversion copy a non-value state
    return Derived{std::forward<ResultT>(rt).non_value()}; // Rewrap non-value
  }

  struct private_copy_t {};
  result_crtp(private_copy_t, const Derived& that) : exp_(that.exp_) {}

  template <typename ExpT>
  result_crtp(std::in_place_t, ExpT&& exp) : exp_(static_cast<ExpT&&>(exp)) {}

  // As of D42260201, `folly::Expected` coroutines use an empty `Expected`
  // as the default storage for a promise return object.  Here, we replicate
  // that pattern, see `result_promise_return`.
  explicit result_crtp(expected_detail::EmptyTag tag) noexcept : exp_{tag} {}
  result_crtp(expected_detail::EmptyTag tag, Derived*& pointer) noexcept
      : exp_{tag} {
    pointer = static_cast<Derived*>(this);
  }

  // Not for direct use
  ~result_crtp() = default;

  void throw_if_no_value() const {
    if (FOLLY_UNLIKELY(exp_.hasError())) {
      exp_.error().throw_exception();
    } else if (FOLLY_UNLIKELY(!exp_.hasValue())) {
      detail::dfatal_get_empty_result_error().throw_exception();
    }
  }

  bool is_expected_empty() const {
    // We're checking for an `EmptyTag`-constructed `Expected`, so this
    // would be ideal, but that detail isn't public:
    //   exp_.which_ == expected_detail::Which::eEmpty
    return !(exp_.hasValue() || exp_.hasError());
  }

 public:
  /********* Construction & assignment for `T` `void` and non-`void` **********/

  /// Movable, so long as `T` is.
  result_crtp(result_crtp&&) = default;
  result_crtp& operator=(result_crtp&&) = default;

  /// `result<T>` has an explicit `.copy()` method instead of a standard copy
  /// constructor.  This was done because `result` is intended to act as cheap
  /// plumbing for function-result-or-error, and
  ///   - Copying `T` is almost always a performance bug in this setting, but
  ///     see the below carve-out for "cheap-to-copy `T`".
  ///   - Copying `std::exception_ptr` also has atomic costs (~25ns).
  Derived copy() const {
    return Derived{private_copy_t{}, static_cast<const Derived&>(*this)};
  }
  result_crtp(const result_crtp&) = delete;
  result_crtp& operator=(const result_crtp&) = delete;

  /// Implicit constructor to allow returning `stopped_result` from `result`
  /// coroutines & functions.
  ///
  /// This forbids `result<stopped_result_t>` (`static_assert` above).
  /*implicit*/ result_crtp(stopped_result_t s)
      : exp_(Unexpected{non_value_result{s}}) {}

  /// Implicitly movable / explicitly copyable from `non_value_result` to
  /// make it easy to return `resT1.non_value()` in a `result<T2>` function.
  ///
  /// This forbids `result<non_value_result>` (`static_assert` above).
  /*implicit*/ result_crtp(non_value_result&& nvr)
      : exp_(Unexpected{std::move(nvr)}) {}
  explicit result_crtp(const non_value_result& nvr) : exp_(Unexpected{nvr}) {}

  /// Fallible copy/move conversion -- unlike the "simple" conversion, this can
  /// plausibly apply for `T` void.
  ///
  /// If a user type has a fallible conversion: `U` -> `result<T>`, implicitly
  /// convert `result<U>` into `result<T>`, and rewrap any conversion error.
  /// The test `fallibleConversion` explains why it has to be **implicit**.
  ///
  /// This helps with `for` loops that iterate over `result<U>`.  This loop:
  ///   auto uGen = generate_result<U>();
  ///   for (result<T> mv: uGen) {}
  /// expands to:
  ///   result<T> mv = *loopIter;
  /// The RHS is usually `result<U>&`, or `result<U>&&` if `U` is an rref.
  ///
  /// As with the simple conversion, prefer move conversions in hot code.
  template <class Arg, typename ResultT = std::remove_cvref_t<Arg>>
    requires(
        !std::is_same_v<ResultT, Derived> && // Not a move/copy ctor.
        // Avoid ambiguity with the above "simple conversion"
        !std::is_constructible_v<expected_t, typename ResultT::expected_t &&>)
  /*implicit*/ result_crtp(Arg&& rt)
      : result_crtp(rewrapping_result_convert(std::forward<Arg>(rt))) {}

  /***************** Accessors for `T` `void` and non-`void` ******************/

  bool has_value() const { return exp_.hasValue(); }
  // Also see `has_stopped()` below!

  /// Non-value access should be used SPARINGLY!
  ///
  /// Normally, you would:
  ///   - `folly::get_exception<Ex>(res)` to test for a specific error.
  ///   - `res.has_stopped()` to test for cancellation.
  ///   - `co_await std::move(res)` to propagate unhandled error/cancellation
  ///     in a `result` sync coroutine.
  ///   - `co_await co_ready(std::move(res))` to propagate unhandled states in
  ///     a `folly::coro` async coroutine.
  ///
  /// Design notes:
  ///
  /// There is no mutable `&` overload so that we can return singleton
  /// invariant-violation exceptions for `dfatal_..._error()` in opt builds:
  ///   - `folly::Expected` is empty due to an `operator=` exception
  ///   - Calling `non_value()` when `has_value() == true` -- UB in
  ///     `std::expected`
  /// With folly-internal optimizations (see `extract_exception_ptr`), moving
  /// `std::exception_ptr` takes 0.5ns, vs ~25ns for a copy.
  ///
  /// If there is a good use-case for mutating the non-value state inside
  /// `result`, we could offer `set_non_value()` with different semantics.
  ///
  /// Future: when I have the appropriate error-path benchmark, try moving the
  /// 2 unlikely branches into a .cpp helper, that might help perf.
  non_value_result non_value() && {
    if (FOLLY_LIKELY(exp_.hasError())) {
      return std::move(exp_).error();
    } else if (exp_.hasValue()) {
      return detail::dfatal_get_bad_result_access_error();
    } else {
      return detail::dfatal_get_empty_result_error();
    }
  }
  const non_value_result& non_value() const& {
    if (FOLLY_LIKELY(exp_.hasError())) {
      return exp_.error();
    } else if (exp_.hasValue()) {
      return detail::dfatal_get_bad_result_access_error();
    } else {
      return detail::dfatal_get_empty_result_error();
    }
  }

  // Syntax sugar to minimize the chances that end-users need `non_value()`.
  bool has_stopped() const { return !has_value() && non_value().has_stopped(); }

  /********************************* Protocols ********************************/

  // `result` is a short-circuiting coroutine.
  using promise_type = detail::result_promise<T>;

  // Implement the `folly::get_exception<Ex>(res)` protocol
  template <typename Ex>
  Ex* get_mutable_exception(get_exception_tag_t) noexcept {
    if (!exp_.hasError()) {
      return nullptr;
    }
    return folly::get_mutable_exception<Ex>(exp_.error());
  }
  template <typename Ex>
  const Ex* get_exception(get_exception_tag_t) const noexcept {
    if (!exp_.hasError()) {
      return nullptr;
    }
    return folly::get_exception<Ex>(exp_.error());
  }
};

} // namespace detail

// The default specialization is non-`void` (but `result<>` defaults to `void`)
template <typename T>
class FOLLY_NODISCARD [[FOLLY_ATTR_CLANG_CORO_AWAIT_ELIDABLE]] result final
    : public detail::result_crtp<result<T>, T> {
 private:
  template <typename, typename>
  friend class detail::result_crtp; // `ResultT::expected_t` in `requires`
  template <typename>
  friend class result; // `ResultT::expected_t` in `requires`

  using base = typename detail::result_crtp<result<T>, T>;
  using typename base::expected_t;
  // For `T` non-`void`, we store either `T` or a ref wrapper.
  using ref_wrapped_t = typename base::storage_type;

 protected:
  FOLLY_GTEST_FRIEND_TEST(Result, BadEmptyStateInt);
  FOLLY_GTEST_FRIEND_TEST(Result, BadEmptyStateString);

 public:
  using detail::result_crtp<result<T>, T>::result_crtp;

  /// Not default-constructible yet, since the utility is debatable.  If we
  /// were to later make `result` default-constructible, it should follow
  /// `std::expected` semantics, as below.  As of now, there are only a couple
  /// of tests in `ResultTest.cpp` marked "not default-constructible".

  /*
  /// Default-construct as `std::expected` would, and unlike `folly::Expected`
  result() noexcept(noexcept(expected_t(ref_wrapped_t{})))
    requires std::is_default_constructible_v<ref_wrapped_t>
      : base{std::in_place, ref_wrapped_t{}} {}

  TEST(result, defaultCtor) {
    result<> mVoid;
    EXPECT_TRUE(mVoid.has_value());
    result<int> mInt;
    EXPECT_EQ(0, mInt.value_or_throw());
  }
  */

  /// Copy- & move-conversion from a reference wrapper.
  ///
  /// Implicit to allow returning `std::ref(memberVar_)` from member functions.
  /* implicit */ result(ref_wrapped_t t) noexcept
    requires std::is_reference_v<T>
      : base{std::in_place, std::move(t)} {}

  /// Move-construct `result<T>` from the underlying value type `T`.
  ///
  /// Implicit to allow `result<T>` functions to return `T{}` etc.
  /* implicit */ result(T&& t) noexcept(noexcept(expected_t(std::move(t))))
    requires(
        !std::is_reference_v<T> && std::is_constructible_v<expected_t, T &&>)
      : base{std::in_place, std::move(t)} {}
  result& operator=(T&& t) noexcept(
      std::is_nothrow_assignable_v<expected_t, T&&>)
    requires(!std::is_reference_v<T> && std::is_assignable_v<expected_t, T &&>)
  {
    this->exp_ = std::move(t);
    return *this;
  }

  /// Copy underlying `T`, but ONLY when small & trivially copyable.
  /// Implicit, so that e.g. `result<int> memberFn()` can return `memVar_`.
  //
  /// These are a special case because such copies are cheap*, and because
  /// good alternatives for populating trivially copiable data are few:
  ///   - Copy-construct the value into the `result`.
  ///   - Less efficient: Default-initialize the `result` and assign a
  ///     `folly::copy()`, or use a mutable value reference to populate it.
  ///   - Future: Implement in-place construction, to handle very hot code.
  ///
  /// This constructor is deliberately restricted to objects that fit in a
  /// cache-line.  This is a heuristic to require larger copies to be explicit
  /// via `folly::copy()`.  If it proves fragile across different
  /// architectures, it can be relaxed later.
  ///
  /// Notes:
  ///   - For now, we omitted the analogous ctor to copy `result<V&>`, for the
  ///     reason that it's much less common than wanting to write e.g.
  ///     `result<int> r{intVar}`.  It can be added later if strongly needed.
  ///   - We don't need copy ctors for `co_return varOfTypeT;` because this is
  ///     an "implicitly movable context" in the C++ spec, so a move ctor is
  ///     automatically considered as the first option.
  /* implicit */ result(const T& t) noexcept(noexcept(expected_t(t)))
    requires(
        !std::is_reference_v<T> &&
        std::is_constructible_v<expected_t, const T&> &&
        std::is_trivially_copyable_v<T> &&
        sizeof(T) <= hardware_constructive_interference_size)
      : base{std::in_place, t} {}

  /// No copy assignment.  When appropriate, use a mutable `value_or_throw()`
  /// reference, or assign `folly::copy(rhs)` to be explicit.

  /// Simple copy/move conversion; `result_crtp` also has a fallible conversion.
  ///
  /// Convert `result<U>` to `result<T>` if:
  ///   - `U` is a value type that is copy/move convertible to `T`.
  ///   - `U` is a reference whose ref-wrapper is converible to `T`.
  /// The test `simpleConversion` shows why this was made implicit.
  ///
  /// In hot code, prefer to convert from an rvalue (move conversion), because
  /// that avoids the ~25ns atomic overhead of copying the `std::exception_ptr`.
  template <class Arg, typename ResultT = std::remove_cvref_t<Arg>>
    requires(
        !std::is_same_v<ResultT, result> && // Not a move/copy ctor
        // NB: This won't implicitly copy `non_value_result` since the
        // underlying `Expected` is only constructible from `Unexpected`.
        std::is_constructible_v<expected_t, typename ResultT::expected_t &&>)
  /* implicit */ result(Arg&& that)
      : base{std::in_place, std::forward<Arg>(that).exp_} {
    static_assert(is_instantiation_of_v<result, ResultT>);
  }

  /// Retrieve non-reference `T`
  const T& value_or_throw() const&
    requires(!std::is_reference_v<T>)
  {
    this->throw_if_no_value();
    return *this->exp_;
  }
  T& value_or_throw() &
    requires(!std::is_reference_v<T>)
  {
    this->throw_if_no_value();
    return *this->exp_;
  }
  const T&& value_or_throw() const&&
    requires(!std::is_reference_v<T>)
  {
    this->throw_if_no_value();
    return *std::move(this->exp_);
  }
  T&& value_or_throw() &&
    requires(!std::is_reference_v<T>)
  {
    this->throw_if_no_value();
    return *std::move(this->exp_);
  }

  /// Retrieve reference `T`.
  ///
  /// NB Unlike the value-type versions, these can't mutate the reference
  /// wrapper inside `this`.  Assign a ref-wrapper to `res` to do that.
  ///
  /// L-value refs follow `std::reference_wrapper`, exposing the underlying ref
  /// type regardless of the instance's qualification.  We never add `const`
  /// for reasons sketched in the test `checkAwaitResumeTypeForRefResult`.
  T value_or_throw() const&
    requires std::is_lvalue_reference_v<T>
  {
    this->throw_if_no_value();
    return this->exp_->get();
  }
  // R-value refs follow `folly::rvalue_reference_wrapper`.  They model
  // single-use references, and thus require `&&` qualification.
  T value_or_throw() &&
    requires std::is_rvalue_reference_v<T>
  {
    this->throw_if_no_value();
    return std::move(*std::move(this->exp_)).get();
  }
};

// Specialization for `T = void` aka `result<>`.
template <>
class FOLLY_NODISCARD [[FOLLY_ATTR_CLANG_CORO_AWAIT_ELIDABLE]] result<void>
    final : public detail::result_crtp<result<void>, void> {
 private:
  using base = detail::result_crtp<result<void>, void>;

 public:
  using base::result_crtp;

  // Unlike `result<T>`, default-constructing `result<void>` seems fine.
  // Specifically: `std::expected<void>` and `Try<void>` actually agree on the
  // semantics (yes, `Try` is internally inconsistent) -- and this is the most
  // obvious way to get a value-state `result<>`.
  result() : base(std::in_place, unit) {}

  void value_or_throw() const { this->throw_if_no_value(); }
};

// Type trait to test if a type is a `result`.
template <typename T>
struct is_result : std::false_type {};
template <typename T>
struct is_result<result<T>> : std::true_type {};

// This short-circuiting coroutine implementation was modeled on
// `folly/Expected.h`, which is likely to follow the state of the art in
// compiler support & optimizations.  So, if you're looking at this, please
// compare it to the original, and backport any improvements here.
namespace detail {

template <typename>
struct result_promise_base;

template <typename T>
struct result_promise_return {
  result<T> storage_{expected_detail::EmptyTag{}};
  result<T>*& pointer_;

  /* implicit */ result_promise_return(result_promise_base<T>& p) noexcept
      : pointer_{p.value_} {
    pointer_ = &storage_;
  }
  result_promise_return(result_promise_return const&) = delete;
  void operator=(result_promise_return const&) = delete;
  result_promise_return(result_promise_return&&) = delete;
  void operator=(result_promise_return&&) = delete;
  // letting dtor be trivial makes the coroutine crash
  // TODO: fix clang/llvm codegen
  ~result_promise_return() {}

  /* implicit */ operator result<T>() {
    // D42260201: handle both deferred and eager return-object conversion
    // behaviors see docs for detect_promise_return_object_eager_conversion
    if (coro::detect_promise_return_object_eager_conversion()) {
      assert(storage_.is_expected_empty());
      return result<T>{expected_detail::EmptyTag{}, pointer_}; // eager
    } else {
      assert(!storage_.is_expected_empty());
      return std::move(storage_); // deferred
    }
  }
};

template <typename T>
struct result_promise_base {
  result<T>* value_ = nullptr;

  result_promise_base() = default;
  result_promise_base(result_promise_base const&) = delete;
  void operator=(result_promise_base const&) = delete;
  result_promise_base(result_promise_base&&) = delete;
  void operator=(result_promise_base&&) = delete;
  ~result_promise_base() = default;

  FOLLY_NODISCARD std::suspend_never initial_suspend() const noexcept {
    return {};
  }
  FOLLY_NODISCARD std::suspend_never final_suspend() const noexcept {
    return {};
  }
  void unhandled_exception() noexcept {
    // We're making a `result`, so it's OK to forward all exceptions into it,
    // including `OperationCancelled`.
    *value_ = non_value_result::make_legacy_error_or_cancellation(
        exception_wrapper{std::current_exception()});
  }

  result_promise_return<T> get_return_object() noexcept { return *this; }
};

template <typename T>
struct result_promise<T, typename std::enable_if<!std::is_void_v<T>>::type>
    : public result_promise_base<T> {
  // For reference types, this deliberately requires users to `co_return`
  // one of `std::ref`, `std::cref`, or `folly::rref`.
  //
  // The default for `U` is tested in `returnImplicitCtor`.
  template <typename U = T>
  void return_value(U&& u) {
    auto& v = *this->value_;
    expected_detail::ExpectedHelper::assume_empty(v.exp_);
    v = static_cast<U&&>(u);
  }
};

template <typename T>
struct result_promise<T, typename std::enable_if<std::is_void_v<T>>::type>
    : public result_promise_base<T> {
  // When the coroutine uses `return;` you can fail via `co_await err`.
  void return_void() { this->value_->exp_.emplace(unit); }
};

template <typename T>
using result_promise_handle = std::coroutine_handle<result_promise<T>>;

// This is separate to let `result_generator` reuse the awaitables below.
struct result_await_suspender {
  // Future: check if all these `FOLLY_ALWAYS_INLINE`s aren't a pessimization.
  template <typename T, typename U>
  FOLLY_ALWAYS_INLINE void operator()(T&& t, result_promise_handle<U> handle) {
    auto& v = *handle.promise().value_;
    expected_detail::ExpectedHelper::assume_empty(v.exp_);
    // `T` can be `non_value_result&&`, or one of a few `result<T>` refs.
    if constexpr (std::is_same_v<non_value_result, std::remove_cvref_t<T>>) {
      v.exp_ = Unexpected{std::forward<T>(t)};
    } else {
      v.exp_ = Unexpected{std::forward<T>(t).non_value()};
    }
    // Abort the rest of the coroutine. resume() is not going to be called
    handle.destroy();
  }
};

// There's no `result` in the name as a hint to lift this to a shared header as
// soon as another usecase arises.
template <typename AwaitSuspender>
struct non_value_awaitable {
  non_value_result non_value_;

  constexpr std::false_type await_ready() const noexcept { return {}; }
  [[noreturn]] void await_resume() {
    compiler_may_unsafely_assume_unreachable();
  }
  FOLLY_ALWAYS_INLINE void await_suspend(auto h) {
    AwaitSuspender()(std::move(non_value_), h);
  }
};

template <typename T, typename AwaitSuspender>
struct result_owning_awaitable {
  result<T> storage_;

  bool await_ready() const noexcept { return storage_.has_value(); }
  drop_unit_t<T> await_resume() { return std::move(storage_).value_or_throw(); }
  FOLLY_ALWAYS_INLINE void await_suspend(auto h) {
    AwaitSuspender()(std::move(storage_), h);
  }
};

// We won't have a `folly::rvalue_reference_wrapper` counterpart because
// awaiting rvalue `result`s is handled by `result_owning_awaitable`, which
// avoids exposing some dangling reference footguns to the user.
template <
    typename T,
    template <typename>
    class ConstWrapper,
    typename AwaitSuspender>
struct result_ref_awaitable {
  using ResultT = ConstWrapper<result<T>>;
  constexpr static bool kIsConstRef = !std::is_same_v<ResultT, result<T>>;

  std::reference_wrapper<ResultT> storage_;

  bool await_ready() const noexcept { return storage_.get().has_value(); }

  // Awaiting a ref to `result<Value>` returns a ref to the value.
  T& await_resume()
    requires(!std::is_reference_v<T> && !kIsConstRef)
  {
    return storage_.get().value_or_throw();
  }
  const T& await_resume()
    requires(!std::is_reference_v<T> && kIsConstRef)
  {
    return storage_.get().value_or_throw();
  }
  // Awaiting a ref to `result<Reference>` returns the reference itself.
  T await_resume()
    requires std::is_reference_v<T>
  {
    return storage_.get().value_or_throw();
  }

  FOLLY_ALWAYS_INLINE void await_suspend(auto h) {
    // We can't move the error even out of a mutable l-value reference to
    // `result`, because the user isn't counting on `co_await std::ref(m)` to
    // mutate the `result`.
    AwaitSuspender()(storage_.get(), h);
  }
};

} // namespace detail

// co_await stopped_result
inline auto /* implicit */ operator co_await(stopped_result_t s) {
  return detail::non_value_awaitable<detail::result_await_suspender>{
      .non_value_ = non_value_result{s}};
}

// co_await std::move(res).non_value()
//
// Pass-by-&& to discourage accidental copies of `std::exception_ptr`.
inline auto /* implicit */ operator co_await(non_value_result && nvr) {
  return detail::non_value_awaitable<detail::result_await_suspender>{
      .non_value_ = std::move(nvr)};
}

// co_await resultFunc()
//
// DO NOT add a copyable overload for small, trivially copyable types,
// since this is (a) rare, (b) will make the error path slower.  See the
// discussion of `co_await std::{move,ref,cref}` in `result.md`.
template <typename T>
auto /* implicit */ operator co_await(result<T>&& r) {
  return detail::result_owning_awaitable<T, detail::result_await_suspender>{
      .storage_ = std::move(r)};
}

// co_await std::ref(resultVal)
template <typename T>
auto /* implicit */ operator co_await(std::reference_wrapper<result<T>> rr) {
  return detail::result_ref_awaitable<
      T,
      std::type_identity_t,
      detail::result_await_suspender>{.storage_ = std::move(rr)};
}

// co_await std::cref(resultVal)
template <typename T>
auto /* implicit */ operator co_await(
    std::reference_wrapper<const result<T>> cr) {
  return detail::result_ref_awaitable< //
      T,
      std::add_const_t,
      detail::result_await_suspender>{.storage_ = std::move(cr)};
}

/// Wraps the return value from the lambda `fn` in a `result`, putting any
/// thrown exception into its "error" state.
///
///   return result_catch_all([&](){ return riskyWork(); });
///
/// Useful when you need a subroutine **definitely** not to throw. In contrast:
///   - `result<>` coroutines catch unhandled exceptions, but can throw due to
///     argument copy/move ctors, or due to `bad_alloc`.
///   - Like all functions, `result<>` non-coroutines let exceptions fly.
template <typename F, typename RetF = decltype(FOLLY_DECLVAL(F&&)())>
// Wrap the return type of `fn` with `result` unless it already is `result`.
conditional_t<is_instantiation_of_v<result, RetF>, RetF, result<RetF>>
result_catch_all(F&& fn) noexcept {
  try {
    if constexpr (std::is_void_v<RetF>) {
      static_cast<F&&>(fn)();
      return {};
    } else {
      return static_cast<F&&>(fn)();
    }
  } catch (...) {
    // We're a making `result`, so it's OK to forward all exceptions into it,
    // including `OperationCancelled`.
    return non_value_result::make_legacy_error_or_cancellation(
        exception_wrapper{std::current_exception()});
  }
}

} // namespace folly

#endif // FOLLY_HAS_RESULT
