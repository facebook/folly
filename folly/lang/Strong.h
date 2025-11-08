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

#include <iosfwd>
#include <type_traits>
#include <utility>

#include <fmt/format.h>

namespace folly {

/// `strong<T, Tag>` - prevent accidental mixing of semantically different uses
/// of the same underlying type. Known as "newtype" in some languages. C++20.
/// Default-construction value-initializes `T`, even if it's a primitive type.
///
/// The tag type should be the name of your derived type. This guarantees that
/// different strong `T`s are not interchangeable.
///
/// The `std::hash`, `fmt::format`, and `operator<<(ostream&)` implementations
/// rely on this convention, so if you see failures in those, check your tag.
/// You can customize the output per-type, see `CustomOutput` in the test.
///
/// Usually, it is OK to allow implicit construction from the underlying type:
///
///   struct UserId : public strong<std::string, UserId> {
///     using strong<std::string, UserId>::strong;
///     /* implicit */ UserId(std::string s) : strong{std::move(s)} {}
///   };
///
///   UserId uid{"user123"};
///   std::string s{uid}; // Explicit conversion OK
///   std::string s = uid; // Compile error -- no implicit conversion
///
/// If you want a stricter type, omit the implicit constructor. For ease of use,
/// even without the implicit constructor, your strong type will be comparable
/// with the underlying type.
template <typename T, typename /* your derived class */>
class strong {
 private:
  static_assert(!std::is_reference_v<T>);
  T value_{};

 public:
  using underlying_type = T;

  constexpr strong() = default; // note: value-initializes `T`, even if trivial

  // When constructed explicitly, forward args to the underlying ctor.
  // In particular, this will construct `strong` from `T`.
  template <typename Head, typename... Tail>
    requires(
        std::is_constructible_v<T, Head, Tail...> &&
        // Avoid touching the standard ctors
        !std::derived_from<std::remove_cvref_t<Head>, strong>)
  explicit constexpr strong(Head&& head, Tail&&... tail)
      : value_{std::forward<Head>(head), std::forward<Tail>(tail)...} {}

  // Comparable iff the underlying type is.
  friend bool operator==(const strong&, const strong&) = default;
  friend auto operator<=>(const strong&, const strong&) = default;

  // For ease of use, `strong` is comparable with `underlying_type`, even if
  // your derived class does not add an implicit constructor. Accept only `T` to
  // prevent implicit conversions from other types (`strong<int>` vs `double`).
  template <typename U>
    requires std::same_as<std::remove_cvref_t<U>, T>
  friend constexpr bool operator==(const strong& st, U&& underlying) {
    return st.value_ == underlying;
  }
  template <typename U>
    requires std::same_as<std::remove_cvref_t<U>, T>
  friend constexpr auto operator<=>(const strong& st, U&& underlying) {
    return st.value_ <=> underlying;
  }

  // Explicitly convertible to the underlying type
  explicit constexpr operator T&() & { return value_; }
  explicit constexpr operator T const&() const& { return value_; }
  explicit constexpr operator T&&() && { return std::move(value_); }
  explicit constexpr operator T const&&() const&& { return std::move(value_); }

  // Access the underlying type
  constexpr T& value() & { return value_; }
  constexpr const T& value() const& { return value_; }
  constexpr T&& value() && { return std::move(value_); }
  constexpr const T&& value() const&& { return std::move(value_); }
};

/// `strong_derefable<T, Tag>` - a strong type with pointer-like dereference
/// operators. Inherits all functionality from `strong` and adds `operator*` and
/// `operator->` for convenient access to the underlying value. Usage:
///
///   struct X : strong_derefable<T, X> { /* as above */ };
template <typename T, typename Tag>
class strong_derefable : public strong<T, Tag> {
 public:
  using strong<T, Tag>::strong;

  constexpr const T& operator*() const& { return this->value(); }
  constexpr T& operator*() & { return this->value(); }
  constexpr const T&& operator*() const&& { return std::move(*this).value(); }
  constexpr T&& operator*() && { return std::move(*this).value(); }

  constexpr const T* operator->() const { return &this->value(); }
  constexpr T* operator->() { return &this->value(); }
};

// Strong types support `ostream` iff the underlying type supports it.
template <typename T, typename Tag>
auto operator<<(std::ostream& os, const strong<T, Tag>& st)
    -> decltype(os << st.value()) {
  return os << st.value();
}

template <typename T>
concept is_strong =
    std::derived_from<T, strong<typename T::underlying_type, T>>;

} // namespace folly

// Let strong types be keys in unordered containers. A strong type is
// hashable iff the underlying type is.
template <typename T>
  requires(
      folly::is_strong<T> &&
      // Required for `folly::is_hashable_v` to be correct.
      requires(typename T::underlying_type underlying) {
        {
          std::hash<typename T::underlying_type>{}(underlying)
        } -> std::convertible_to<std::size_t>;
      })
struct std::hash<T> {
  size_t operator()(const T& st) const {
    return std::hash<typename T::underlying_type>{}(st.value());
  }
};

// Strong types are formattable iff the underlying type is.
template <typename T>
  requires(
      folly::is_strong<T> &&
      fmt::is_formattable<typename T::underlying_type>::value)
struct fmt::formatter<T> : fmt::formatter<typename T::underlying_type> {
  auto format(const T& st, auto& ctx) const {
    return fmt::formatter<typename T::underlying_type>::format(st.value(), ctx);
  }
};
