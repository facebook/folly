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

// Can make lighter: only shares `result_ref_wrap`, `Align.h`
#include <folly/result/result.h>

/// See `result.h` for detailed API docs.
///
/// See `value_only_result_coro.h` if you need to await-unwrap these from
/// inside `result` or task coros.
///
/// This API is a partial mirror of `result<T>`, so comments here are minimal
/// to reduce copy-pasta. The main differences are:
///   - It is statically guaranteed to be in the `has_value() == true` state.
///   - Provides `value_only()`, equivalent to `value_or_throw()`, for those
///     scenarios where you wish to assert you have a `value_only_result`.
///   - Omits some APIs (for now) -- see "Potential extensions"
///
/// A major motivation for `value_only_result` is to enable uniform APIs in
/// `folly::coro::collect`.  Specifically:
///   - The same generic lambda can handle values from both throwing and
///     non-throwing inputs.
///   - It is easy to specify that an adaptor or sink expects errors to be
///     swallowed before it is reached.
/// It similarly also enables other generic code to handle both fallible and
/// infallible inputs.
///
/// ## Potential extensions
///
/// Features are added as-needed ...  and some should never be needed.  If you
/// need it, it is definitely fine to support `co_yield co_result(valOnlyRes)`
/// from async tasks.
///
/// It's a good idea to add a linter that errors whenever a function returning
/// `value_only_result` is not `noexcept`.
///
/// While it is hard to imagine a use-case that requires `value_only_result` to
/// **be** a coroutine, here are some notes:
///   - The `noexcept` linter above becomes more important, since writing
///     `unhandled_exception() { throw; }` neither makes sense, nor bodes well
///     for well-optimized code.  But writing `unhandled_exception() noexcept`
///     definitely requires a pervasive linter.
///   - You may want to add a `::template apply<>` to `result`-like types
///     to make it easier for users to write generic coros that are
///     either-`result`-or-`value_only_result`.
///
/// Implementation note: It wouldn't be hard to deduplicate most of the API
/// implementation with `result.h`, but I went with the "copy" route since it
/// keeps down the complexity of the main file -- and this here isn't too bad.

#if FOLLY_HAS_RESULT

namespace folly {

template <typename T = void>
class value_only_result;

namespace detail {

// Shared implementation for `T` non-`void` and `void`
template <typename Derived, typename T>
class value_only_result_crtp {
  static_assert(!std::is_same_v<non_value_result, std::remove_cvref_t<T>>);
  static_assert(!std::is_same_v<stopped_result_t, std::remove_cvref_t<T>>);

 protected:
  using storage_type = detail::result_ref_wrap<lift_unit_t<T>>;
  static_assert(!std::is_reference_v<storage_type>);

  storage_type value_;

  template <typename>
  friend class folly::value_only_result; // The simple conversion uses `value_`

  struct private_copy_t {};
  value_only_result_crtp(private_copy_t, const Derived& that)
      : value_(that.value_) {}

  template <typename V>
  value_only_result_crtp(std::in_place_t, V&& v)
      : value_(static_cast<V&&>(v)) {}

  ~value_only_result_crtp() = default;

 public:
  using value_type = T; // NB: can be a reference

  /// Movable, so long as `T` is.
  value_only_result_crtp(value_only_result_crtp&&) = default;
  value_only_result_crtp& operator=(value_only_result_crtp&&) = default;

  /// Explicit `.copy()` method instead of a standard copy constructor.
  Derived copy() {
    return Derived{private_copy_t{}, static_cast<const Derived&>(*this)};
  }
  // The `const`-safety constraints are explained on `result::copy`.
  Derived copy() const
    requires(
        !std::is_reference_v<T> || std::is_const_v<std::remove_reference_t<T>>)
  {
    return Derived{private_copy_t{}, static_cast<const Derived&>(*this)};
  }
  // IMPORTANT: Read the `result` copy ctor comment before touching.
  value_only_result_crtp(const value_only_result_crtp&) = delete;
  value_only_result_crtp& operator=(const value_only_result_crtp&) = delete;

  // NB: This is currently missing the counterpart to "Fallible copy/move
  // conversion" from `result.h`.  That's because it seems very unlikely that
  // types would have implicit `U1` -> `value_only_result<U2>` conversions.

  bool has_value() const { return true; }
  bool has_stopped() const { return false; }

  // Only provide `==` since less/greater doesn't make sense for `result`.
  bool operator==(const value_only_result_crtp&) const = default;
};

} // namespace detail

// The default specialization is non-`void`, but `value_only_result<>` defaults
// to `void`.
template <typename T>
class FOLLY_NODISCARD
    // Not a coroutine, but any reasonable implementation would be elidable.
    [[FOLLY_ATTR_CLANG_CORO_AWAIT_ELIDABLE]] value_only_result final
    : public detail::value_only_result_crtp<value_only_result<T>, T> {
 private:
  using base = detail::value_only_result_crtp<value_only_result<T>, T>;
  // For `T` non-`void`, we store either `T` or a ref wrapper.
  using ref_wrapped_t = typename base::storage_type;

 public:
  using detail::value_only_result_crtp<value_only_result<T>, T>::
      value_only_result_crtp;

  /// Future: add default-constructibility iff `result` gets it.

  /// Copy- & move-conversion from a reference wrapper.
  /* implicit */ value_only_result(ref_wrapped_t t) noexcept
    requires std::is_reference_v<T>
      : base{std::in_place, std::move(t)} {}

  /// Move-construct `value_only_result<T>` from the underlying value type `T`.
  /* implicit */ value_only_result(T&& t) noexcept(
      noexcept(ref_wrapped_t(std::move(t))))
    requires(
        !std::is_reference_v<T> && std::is_constructible_v<ref_wrapped_t, T &&>)
      : base{std::in_place, std::move(t)} {}
  value_only_result& operator=(T&& t) noexcept(
      std::is_nothrow_assignable_v<ref_wrapped_t, T&&>)
    requires(
        !std::is_reference_v<T> && std::is_assignable_v<ref_wrapped_t, T &&>)
  {
    this->value_ = std::move(t);
    return *this;
  }

  /// Copy underlying `T`, but ONLY when small & trivially copyable.
  /* implicit */ value_only_result(const T& t) noexcept(
      noexcept(ref_wrapped_t(t)))
    requires(
        !std::is_reference_v<T> &&
        std::is_constructible_v<ref_wrapped_t, const T&> &&
        std::is_trivially_copyable_v<T> &&
        sizeof(T) <= hardware_constructive_interference_size)
      : base{std::in_place, t} {}

  /// No copy assignment, just like `result`.

  /// Simple copy/move conversion.
  ///
  /// Convert `value_only_result<U>` to `value_only_result<T>` if:
  ///   - `U` is a value type that is copy/move convertible to `T`.
  ///   - `U` is a reference whose ref-wrapper is converible to `T`.
  template <class Arg, typename ResultT = std::remove_cvref_t<Arg>>
    requires(
        !std::is_same_v<ResultT, value_only_result> && // Not a move/copy ctor
        std::is_constructible_v<
            ref_wrapped_t,
            typename ResultT::storage_type &&>)
  /* implicit */ value_only_result(Arg&& that)
      : base{std::in_place, std::forward<Arg>(that).value_} {
    static_assert(is_instantiation_of_v<value_only_result, ResultT>);
  }

  /// Retrieve non-reference `T` -- `value_or_throw` is a synonym of
  /// `value_only`

  const T& value_or_throw() const& noexcept
    requires(!std::is_reference_v<T>)
  {
    return this->value_;
  }
  const T& value_only() const& noexcept
    requires(!std::is_reference_v<T>)
  {
    return this->value_;
  }

  T& value_or_throw() & noexcept
    requires(!std::is_reference_v<T>)
  {
    return this->value_;
  }
  T& value_only() & noexcept
    requires(!std::is_reference_v<T>)
  {
    return this->value_;
  }

  const T&& value_or_throw() const&& noexcept
    requires(!std::is_reference_v<T>)
  {
    return std::move(this->value_);
  }
  const T&& value_only() const&& noexcept
    requires(!std::is_reference_v<T>)
  {
    return std::move(this->value_);
  }

  T&& value_or_throw() && noexcept
    requires(!std::is_reference_v<T>)
  {
    return std::move(this->value_);
  }
  T&& value_only() && noexcept
    requires(!std::is_reference_v<T>)
  {
    return std::move(this->value_);
  }

  /// Retrieve reference `T` -- `value_or_throw` is a synonym of `value_only`
  ///
  /// NB Unlike the value-type versions, accesors cannot mutate the reference
  /// wrapper inside `this`.  Assign a ref-wrapper to the `result` to do that.

  /// Lvalue result-ref propagate `const`: `const result<T&>` -> `const T&`.
  /// See a discussion of the trade-offs in `docs/result.md`.

  like_t<const int&, T> value_or_throw() const& noexcept
    requires std::is_lvalue_reference_v<T>
  {
    return std::as_const(this->value_.get());
  }
  like_t<const int&, T> value_only() const& noexcept
    requires std::is_lvalue_reference_v<T>
  {
    return std::as_const(this->value_.get());
  }

  T value_or_throw() & noexcept
    requires std::is_lvalue_reference_v<T>
  {
    return this->value_.get();
  }

  T value_only() & noexcept
    requires std::is_lvalue_reference_v<T>
  {
    return this->value_.get();
  }

  T value_only() && noexcept
    requires std::is_lvalue_reference_v<T>
  {
    return this->value_.get();
  }
  T value_or_throw() && noexcept
    requires std::is_lvalue_reference_v<T>
  {
    return this->value_.get();
  }

  // R-value refs follow `folly::rvalue_reference_wrapper`.  They model
  // single-use references, and thus require `&&` qualification.

  T value_or_throw() && noexcept
    requires std::is_rvalue_reference_v<T>
  {
    return std::move(this->value_).get();
  }
  T value_only() && noexcept
    requires std::is_rvalue_reference_v<T>
  {
    return std::move(this->value_).get();
  }
};

template <typename T>
value_only_result(std::reference_wrapper<T>) -> value_only_result<T&>;

template <typename T>
value_only_result(rvalue_reference_wrapper<T>) -> value_only_result<T&&>;

// Specialization for `T = void` aka `value_only_result<>`.
template <>
class FOLLY_NODISCARD
    // Not yet a coroutine, but if we make it one, it SHOULD be elidable.
    [[FOLLY_ATTR_CLANG_CORO_AWAIT_ELIDABLE]] value_only_result<void>
        final
    : public detail::value_only_result_crtp<value_only_result<void>, void> {
 private:
  using base = detail::value_only_result_crtp<value_only_result<void>, void>;

 public:
  using base::value_only_result_crtp;

  value_only_result() : base(std::in_place, unit) {}

  void value_or_throw() const noexcept {}
  void value_only() const noexcept {}
};

} // namespace folly

#endif // FOLLY_HAS_RESULT
