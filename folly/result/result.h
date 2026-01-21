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
#include <folly/Portability.h> // FOLLY_HAS_RESULT
#include <folly/lang/Align.h> // for `hardware_constructive_interference_size`
#include <folly/lang/RValueReferenceWrapper.h>
#include <folly/portability/GTestProd.h>
#include <folly/result/rich_exception_ptr.h>

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
///       if (auto ex = folly::get_exception<Ex>(res)) {
///         // Handle `ex`, quacks like `const Ex*`
///       }
///
///   - User-friendly constructors & conversions -- you can write
///     `result<T>`-returning functions as-if they returned `T`, while returning
///     returning `non_value_result{YourException{...}}` on error.
///
///   - Can store & and && references.  Think of them as syntax sugar for
///     `std::reference_wrapper` and `folly::rvalue_reference_wrapper`.
///
///        #include <folly/result/coro.h>
///        struct FancyIntMap {
///          int n;
///          result<int&> at(int i) {
///            if (n + i == 42) { return std::ref(n); }
///            return non_value_result{std::out_of_range{"FancyIntMap"}};
///          }
///        };
///        FancyIntMap m{.n = 12};
///        int& n1 = co_await or_unwind(m.at(30)); // points at 12
///        result<int&> rn2 = m.at(20); // has error
///        // On error, copies and propagates `exception_ptr` to the parent.
///        // When errors are hot, `std::move()` is appropriate.
///        co_return n1 + co_await or_unwind(rn2);
///
///     Key things to remember:
///       - `result<V&&>` is "use-once" -- and it must be r-value qualified to
///         access the reference inside.
///       - `const result<V&>` only gives `const V&` access to the contents.
///         It should be rare that you need the opposite behavior.  If you do,
///         use `result<std::reference_wrapper<V>>` or `result<V*>`.
///
///   - `has_stopped()` & `stopped_result` to nudge `folly` to the C++26 idea
///     that cancellation is NOT an error, see https://wg21.link/P1677 & P2300.
///
///   - Easy short-circuiting of "error" / "stopped" status to the caller:
///     * In both `result<T>` coroutines and `folly::coro` coroutines:
///       - `co_await or_unwind(resultFn())` returns `T&&` from a `result<T>`,
///         or propagates error/stopped to the parent. Same for:
///           * `co_await or_unwind(std::move(res))`
///           * `co_await or_unwind(res.copy())`
///           * `co_await or_unwind(folly::copy(res))`
///       - `co_await or_unwind(res) -> T&`. Copies `exception_ptr` on error.
///       - `co_await or_unwind(std::as_const(res)) -> const T&`
///       WARNING: `auto&& ref = co_await or_unwind(rvalueFn())` dangles; search
///       `result.md` for "LLVM issue #177023".  Safe: `auto val = ...`
///       - `co_await stopped_result` or `non_value_result{YourErr{}}` to
///         end the coroutine with an error without throwing.
///     * In `folly::coro` coroutines:
///       - `co_await value_or_error_or_stopped(x())` makes `result<X>`, does
///         not throw.
///       - `co_yield co_result(std::move(res))` completes with a `result<T>`.
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
void fatal_if_eptr_empty_or_stopped(const std::exception_ptr&);
inline void dfatal_if_eptr_empty_or_stopped(const std::exception_ptr& eptr) {
  // This can be hot in production code (usage similar to `co_awaitTry`).  So,
  // we choose to omit `if (RTTI-test-for-stopped) { log(); }` from opt builds.
  if constexpr (kIsDebug) {
    fatal_if_eptr_empty_or_stopped(eptr);
  }
}

// Do NOT use outside of `folly`, the APIs this gates WILL change.
struct result_private_t {};
} // namespace detail

// Place this into `result` or `non_value_result` to signal that a work-tree
// was stopped (aka cancelled).  You can also `co_await stopped_result` from
// `result` coroutines.
struct stopped_result_t {};
inline constexpr stopped_result_t stopped_result;

template <typename, typename, auto...>
class immortal_rich_error_t;

// NB: Copying `non_value_result` is ~7ns due to `std::exception_ptr` atomics.
// Unlike `result`, it is implicitly copyable, because:
//   - Common usage involves only rvalues, so the risk of perf bugs is low.
//   - `folly::Expected` assumes that the error type is copyable, and it's
//     too convenient an implementation not to use.
class [[nodiscard]] non_value_result {
 private:
  rich_exception_ptr rep_;

  non_value_result(std::in_place_t, std::exception_ptr&& eptr) noexcept
      : rep_{rich_exception_ptr::from_exception_ptr_slow(std::move(eptr))} {}

  template <typename Ex, typename REP>
  static Ex* get_exception_impl(REP& rep) {
    return folly::get_exception<Ex>(rep);
  }

 public:
  /// Future: Fine to make implicit if a good use-case arises.
  explicit non_value_result(stopped_result_t) : rep_{OperationCancelled{}} {}
  non_value_result& operator=(stopped_result_t) {
    rep_ = rich_exception_ptr{OperationCancelled{}};
    return *this;
  }

  /// Use this ctor to report errors from `result` coroutines & functions:
  ///   co_await non_value_result{YourError{...}};
  ///
  /// Hot error paths should consider the `immortal_rich_error_t` ctor instead.
  ///
  /// Design note: We do NOT want most users to construct `non_value_result`
  /// from type-erased `std::exception_ptr` or `folly::exception_wrapper`,
  /// because that would block RTTI-avoidance optimizations for `result` code.
  explicit non_value_result(std::derived_from<std::exception> auto ex)
      : rep_(std::move(ex)) {
    static_assert(
        !std::is_same_v<decltype(ex), OperationCancelled>,
        // The reasons for this are discussed in `folly/OperationCancelled.h`.
        "Do not use `OperationCancelled` in new user code. Instead, construct "
        "your `result` or `non_value_result` via `stopped_result`");
  }

  /// Immortal rich errors are MUCH cheaper to instantiate than dynamic
  /// exceptions.  This does NOT allocate a `std::exception_ptr` or perform
  /// atomic refcount ops.
  ///
  /// Usage for a `YourErr` taking a single `rich_msg` constructor argument:
  ///    non_value_result{immortal_error<YourErr, "msg"_litv>}
  ///
  /// PS These are also usable in `constexpr` code, although the current header
  /// will need some more `contexpr` annotation to take advantage of this.
  template <typename T, auto... Args>
  explicit non_value_result(
      const immortal_rich_error_t<rich_exception_ptr, T, Args...>& err)
      : rep_{err.ptr()} {}

  [[nodiscard]] bool has_stopped() const {
    return bool{::folly::get_exception<OperationCancelled>(rep_)};
  }

  // Implement the `folly::get_exception<Ex>(res)` protocol
  template <typename Ex>
  rich_ptr_to_underlying_error<const Ex> get_exception(
      get_exception_tag_t) const noexcept {
    static_assert( // Note: `OperationCancelled` is final
        !std::is_same_v<const OperationCancelled, const Ex>,
        "Test results for cancellation via `has_stopped()`");
    return folly::get_exception<Ex>(rep_);
  }
  template <typename Ex>
  rich_ptr_to_underlying_error<Ex> get_mutable_exception(
      get_exception_tag_t) noexcept {
    static_assert( // Note: `OperationCancelled` is final
        !std::is_same_v<const OperationCancelled, const Ex>,
        "Test results for cancellation via `has_stopped()`");
    return folly::get_mutable_exception<Ex>(rep_);
  }

  // AVOID -- throwing costs upwards of 1usec.
  //
  // NB: Throwing empty eptr currently terminates even in non-debug builds,
  // following `exception_wrapper`.  Our `dfatal_if_eptr_empty_or_stopped`
  // checks are intended to make such problems less likely, but ...  if
  // production reliability issues trace back to this decision, it could be
  // made to throw a private sigil instead.
  //
  // While user code should not intentionally rethrow `OperationCancelled` on
  // this path, we do NOT dfatal on rethrowing `OperationCancelled` here, in
  // contrast to `to_exception_ptr_slow()`. The motivation is:
  //
  //   - `result` is intended to become the next internal representation
  //     for `folly::coro` task results.
  //
  //   - `value_or_throw()` is the only reasonable bridge from coro code into
  //     non-coro code (see `blocking_wait`, `SemiFuture` conversions, etc).
  //     On exiting coro / result code, there is no real alternative to
  //     propagating cancellation as an exception.
  //
  //   - Internally, `value_or_throw()` uses `throw_exception()`.  And it would
  //     be incoherent for one, but not the other, to be OK rethrowing
  //     `OperationCancelled`.
  [[noreturn]] void throw_exception() const { rep_.throw_exception(); }

  /// AVOID.  Use `non_value_result(YourException{...})` if at all possible.
  /// Add a `std::in_place_type_t<Ex>` constructor if needed.
  ///
  /// Provided for compatibility with existing code.  It has several downsides
  /// for `result`-first code:
  ///   - It is a debug-fatal invariant violation to pass in an `exception_ptr`
  ///     that is empty or has `OperationCancelled`.
  ///     See the `dfatal_if_eptr_empty_or_stopped` doc.
  ///   - Not knowing the static exception type blocks optimizations that can
  ///     otherwise help avoid RTTI on error paths.
  static non_value_result from_exception_ptr_slow(std::exception_ptr eptr) {
    detail::dfatal_if_eptr_empty_or_stopped(eptr);
    return non_value_result{std::in_place, std::move(eptr)};
  }

  /// AVOID. Use `folly::get_exception<Ex>(r)` to check for specific exceptions.
  /// It may be OK to add more specific accessors to `non_value_result`, see
  /// `throw_exception()` for an example.
  ///
  /// INVARIANT: Ensure `!has_stopped()`, or you will see a debug-fatal.
  ///
  /// See `from_exception_ptr_slow` for the downsides and the rationale.
  [[nodiscard]] std::exception_ptr to_exception_ptr_slow() && {
    auto eptr = std::move(rep_).to_exception_ptr_slow();
    detail::dfatal_if_eptr_empty_or_stopped(eptr);
    return detail::extract_exception_ptr(std::move(eptr));
  }

  /// AVOID.  Most code should use `result` coros, which catch most exceptions
  /// automatically.  Or, for a stronger guarantee, see `result_catch_all`.
  static non_value_result from_current_exception() {
    // Something was already thrown, and the user likely wants a result, so
    // it's appropriate to accept even `OperationCancelled` here.
    return {std::in_place, current_exception()};
  }

  friend inline bool operator==(
      const non_value_result& lhs, const non_value_result& rhs) {
    return lhs.rep_ == rhs.rep_;
  }

  // DO NOT USE these "legacy" functions outside of `folly` internals. Instead:
  //   - `non_value_result(YourException{...})` whenever you statically know
  //     the exception type (feel free to add `std::in_place_type_t` support).
  //   - `non_value_result::from_exception_ptr_slow()` only when you MUST pay
  //     for RTTI, such as "thrown exceptions".
  //
  // See `OperationCancelled.h` for how to handle cancellation.  In short: use
  // `get_exception<MyErr>(res)` or `has_stopped()`.
  //
  // These internal-only functions let the `folly::coro` implementation ingest
  // `std::exception_ptr`s containing `OperationCancelled` made via
  // `folly::coro::co_cancelled`, without incurring the 20-80ns+ cost of
  // eagerly eagerly testing whether it contains `OperationCancelled`.
  static non_value_result make_legacy_error_or_cancellation_slow(
      detail::result_private_t, exception_wrapper ew) {
    return {std::in_place, std::move(ew).exception_ptr()};
  }
  exception_wrapper get_legacy_error_or_cancellation_slow(
      detail::result_private_t) && {
    return exception_wrapper{std::move(rep_).to_exception_ptr_slow()};
  }

  // IMPORTANT: We do NOT want to provide general by-reference access to the
  // `rich_exception_ptr` because that would e.g. put in jeopardy our ability
  // to do `future_enrich_in_place.md`.
  //
  // In particular, it is an invariant violation to call `release_...` and
  // use the resulting reference for anything other than:
  //   - moving out the value (if you need a copy, add an explicit
  //     `copy_rich_exception_ptr`)
  //   - doing nothing (i.e. deciding NOT to move the value)
  // You are not to call `const` or mutable accessors on the resulting REP.  If
  // you need some such form of access, you should likely extend this API.
  rich_exception_ptr&& release_rich_exception_ptr() && {
    return std::move(rep_);
  }
};
static_assert(
    detail::rich_exception_ptr_packed_storage::is_supported
        ? sizeof(non_value_result) == sizeof(std::exception_ptr)
        : sizeof(non_value_result) ==
            sizeof(std::exception_ptr) + sizeof(void*));

template <typename T = void>
class result;

namespace detail {

template <typename>
struct result_promise_return;
template <typename, typename = void>
struct result_promise;
struct result_await_suspender;

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

template <typename, typename>
class or_unwind_crtp;

// Shared implementation for `T` non-`void` and `void`
template <typename Derived, typename T>
class result_crtp {
  static_assert(!std::is_same_v<non_value_result, std::remove_cvref_t<T>>);
  static_assert(!std::is_same_v<stopped_result_t, std::remove_cvref_t<T>>);

 public:
  using value_type = T; // NB: can be a reference

 protected:
  using storage_type = detail::result_ref_wrap<lift_unit_t<T>>;
  static_assert(!std::is_reference_v<storage_type>);

  using expected_t = Expected<storage_type, non_value_result>;

  expected_t exp_;

  template <typename>
  friend class folly::result; // The simple conversion ctor uses `exp_`

  friend struct result_promise<T>;
  friend struct result_promise_return<T>;
  template <typename, typename>
  friend class result_or_unwind_crtp; // `await_suspend` uses `exp_`

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
  ///   - Copying `std::exception_ptr` also has atomic costs (~7s).
  ///
  /// Future: We may later make `result` copyable, see `docs/design_notes.md`.
  ///
  /// ## Copies are restricted when `T` is a reference
  ///
  /// `const result<V&>` only gives access to `const V&`, for reasons discussed
  /// in `result.md` under "Store references...".  Standard copy semantics
  /// allows creating a new `result<V&>` from `const result<V&>&`.  But, this
  /// would present a const-safety problem -- since `result<V&>` gives access
  /// to a mutable `V&`. To fix this, copying `result<V&> is ONLY allowed:
  ///   - from `result<V&>&`, since you already have mutable access
  ///   - from `const result<const V&>&`, since the inner `const` is not
  ///     lost during the copy.
  Derived copy()
    requires(
        !std::is_rvalue_reference_v<T> &&
        (std::is_void_v<T> || std::is_copy_constructible_v<T>))
  {
    return Derived{private_copy_t{}, static_cast<const Derived&>(*this)};
  }
  Derived copy() const
    requires(
        (!std::is_reference_v<T> ||
         std::is_const_v<std::remove_reference_t<T>>) &&
        (std::is_void_v<T> || std::is_copy_constructible_v<T>))
  {
    return Derived{private_copy_t{}, static_cast<const Derived&>(*this)};
  }
  // IMPORTANT: If you need to relax this, emulate the "restricted copy"
  // behavior of `DefineMovableDeepConstLrefCopyable.h` when `T` is a ref.
  // Note that this HAS to go on the leaf classes, not in the CRTP.
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

  [[nodiscard]] bool has_value() const { return exp_.hasValue(); }
  // Also see `has_stopped()` below!

  /// Non-value access should be used SPARINGLY!
  ///
  /// Normally, you would:
  ///   - `folly::get_exception<Ex>(res)` to test for a specific error.
  ///   - `res.has_stopped()` to test for cancellation.
  ///   - `co_await or_unwind(std::move(res))` to propagate unhandled
  ///     error/cancellation in `result` sync coroutines, or in `folly::coro`
  ///     async task coroutines.
  ///
  /// Design notes:
  ///
  /// There is no mutable `&` overload so that we can return singleton
  /// invariant-violation exceptions for `dfatal_..._error()` in opt builds:
  ///   - `folly::Expected` is empty due to an `operator=` exception
  ///   - Calling `non_value()` when `has_value() == true` -- UB in
  ///     `std::expected`
  /// With folly-internal optimizations (see `extract_exception_ptr`), moving
  /// `std::exception_ptr` takes 0.5ns, vs ~7ns for a copy.
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
  [[nodiscard]] bool has_stopped() const {
    return !has_value() && non_value().has_stopped();
  }

  /********************************* Protocols ********************************/

  // `result` is a short-circuiting coroutine.
  using promise_type = detail::result_promise<T>;

  // Implement the `folly::get_exception<Ex>(res)` protocol
  template <typename Ex>
  rich_ptr_to_underlying_error<const Ex> get_exception(
      get_exception_tag_t) const noexcept {
    if (!exp_.hasError()) {
      return rich_ptr_to_underlying_error<const Ex>{nullptr};
    }
    return folly::get_exception<Ex>(exp_.error());
  }
  template <typename Ex>
  rich_ptr_to_underlying_error<Ex> get_mutable_exception(
      get_exception_tag_t) noexcept {
    if (!exp_.hasError()) {
      return rich_ptr_to_underlying_error<Ex>{nullptr};
    }
    return folly::get_mutable_exception<Ex>(exp_.error());
  }
};

} // namespace detail

// The default specialization is non-`void` (but `result<>` defaults to `void`)
template <typename T>
class [[nodiscard]] [[FOLLY_ATTR_CLANG_CORO_AWAIT_ELIDABLE]]
result final : public detail::result_crtp<result<T>, T> {
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
  ///   - Less efficient: Default-initialize the `result` and assign
  ///     `folly::copy(T)`, or use a mutable value reference to populate it.
  ///   - Future: Implement in-place construction, to handle very hot code.
  ///
  /// This constructor is deliberately restricted to objects that fit in a
  /// cache-line.  This is a heuristic to require larger copies to be explicit
  /// via folly::copy()`.  If it proves fragile across different architectures,
  /// it can be relaxed later.
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
  /// reference, or assign `rhs.copy()` to be explicit.

  /// Simple copy/move conversion; `result_crtp` also has a fallible conversion.
  ///
  /// Convert `result<U>` to `result<T>` if:
  ///   - `U` is a value type that is copy/move convertible to `T`.
  ///   - `U` is a reference whose ref-wrapper is converible to `T`.
  /// The test `simpleConversion` shows why this was made implicit.
  ///
  /// In hot code, prefer to convert from an rvalue (move conversion), because
  /// that avoids the ~7ns atomic overhead of copying the `std::exception_ptr`.
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
  [[nodiscard]] const T& value_or_throw() const&
    requires(!std::is_reference_v<T>)
  {
    this->throw_if_no_value();
    return *this->exp_;
  }
  [[nodiscard]] T& value_or_throw() &
    requires(!std::is_reference_v<T>)
  {
    this->throw_if_no_value();
    return *this->exp_;
  }
  [[nodiscard]] const T&& value_or_throw() const&&
    requires(!std::is_reference_v<T>)
  {
    this->throw_if_no_value();
    return *std::move(this->exp_);
  }
  [[nodiscard]] T&& value_or_throw() &&
    requires(!std::is_reference_v<T>)
  {
    this->throw_if_no_value();
    return *std::move(this->exp_);
  }

  /// Retrieve reference `T`.
  ///
  /// NB Unlike the value-type versions, accesors cannot mutate the reference
  /// wrapper inside `this`.  Assign a ref-wrapper to the `result` to do that.

  /// Lvalue result-ref propagate `const`: `const result<T&>` -> `const T&`.
  /// See a discussion of the trade-offs in `docs/result.md` & `design_notes.md`
  [[nodiscard]] like_t<const int&, T> value_or_throw() const&
    requires std::is_lvalue_reference_v<T>
  {
    this->throw_if_no_value();
    return std::as_const(this->exp_->get());
  }
  [[nodiscard]] T value_or_throw() &
    requires std::is_lvalue_reference_v<T>
  {
    this->throw_if_no_value();
    return this->exp_->get();
  }
  [[nodiscard]] T value_or_throw() &&
    requires std::is_lvalue_reference_v<T>
  {
    this->throw_if_no_value();
    return this->exp_->get();
  }

  // R-value refs follow `folly::rvalue_reference_wrapper`.  They model
  // single-use references, and thus require `&&` qualification.
  [[nodiscard]] T value_or_throw() &&
    requires std::is_rvalue_reference_v<T>
  {
    this->throw_if_no_value();
    return std::move(*std::move(this->exp_)).get();
  }
};

template <typename T>
result(std::reference_wrapper<T>) -> result<T&>;

template <typename T>
result(rvalue_reference_wrapper<T>) -> result<T&&>;

// Specialization for `T = void` aka `result<>`.
template <>
class [[nodiscard]] [[FOLLY_ATTR_CLANG_CORO_AWAIT_ELIDABLE]] result<void> final
    : public detail::result_crtp<result<void>, void> {
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

/// Wraps the return value from the lambda `fn` in a `result`, putting any
/// thrown exception into its "error" state.
///
///   return result_catch_all([&](){ return riskyWork(); });
///
/// Note:
///   - `result<>` coroutines catch unhandled exceptions, but can additionally
///     throw `bad_alloc` for coro frame allocations (but see LLVM PR 152623).
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
    return non_value_result::from_current_exception();
  }
}

} // namespace folly

#endif // FOLLY_HAS_RESULT

#undef FOLLY_MOVABLE_AND_DEEP_CONST_LREF_COPYABLE
