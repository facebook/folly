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

#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

#include <folly/CPortability.h>
#include <folly/Portability.h>
#include <folly/Traits.h>

namespace folly {

/*
 * FOLLY_DECLVAL(T)
 *
 * This macro works like std::declval<T>() but does the same thing in a way
 * that does not require instantiating a function template.
 *
 * Use this macro instead of std::declval<T>() in places that are widely
 * instantiated to reduce compile-time overhead of instantiating function
 * templates.
 *
 * Note that, like std::declval<T>(), this macro can only be used in
 * unevaluated contexts.
 *
 * There are some small differences between this macro and std::declval<T>().
 * - This macro results in a value of type 'T' instead of 'T&&'.
 * - This macro requires the type T to be a complete type at the
 *   point of use.
 *   If this is a problem then use FOLLY_DECLVAL(T&&) instead, or if T might
 *   be 'void', then use FOLLY_DECLVAL(std::add_rvalue_reference_t<T>).
 */
#if __cplusplus >= 201703L
#define FOLLY_DECLVAL(...) static_cast<__VA_ARGS__ (*)() noexcept>(nullptr)()
#else
// Don't have noexcept-qualified function types prior to C++17
// so just fall back to a function-template.
namespace detail {
template <typename T>
T declval() noexcept;
} // namespace detail

#define FOLLY_DECLVAL(...) ::folly::detail::declval<__VA_ARGS__>()
#endif

namespace detail {
template <typename T>
T decay_(T) noexcept;

//  decay_t
//
//  Like std::decay_t but possibly faster to compile.
//
//  Marked as detail since this differs from std::decay_t in some respects:
//  * incomplete decayed types are forbidden
//  * non-moveable decayed types are forbidden
//
//  mimic: std::decay_t, C++14
template <typename T>
using decay_t = decltype(detail::decay_(FOLLY_DECLVAL(T &&)));
} // namespace detail

/**
 *  copy
 *
 *  Usable when you have a function with two overloads:
 *
 *      class MyData;
 *      void something(MyData&&);
 *      void something(const MyData&);
 *
 *  Where the purpose is to make copies and moves explicit without having to
 *  spell out the full type names - in this case, for copies, to invoke copy
 *  constructors.
 *
 *  When the caller wants to pass a copy of an lvalue, the caller may:
 *
 *      void foo() {
 *        MyData data;
 *        something(folly::copy(data)); // explicit copy
 *        something(std::move(data)); // explicit move
 *        something(data); // const& - neither move nor copy
 *      }
 *
 *  Note: If passed an rvalue, invokes the move-ctor, not the copy-ctor. This
 *  can be used to to force a move, where just using std::move would not:
 *
 *      folly::copy(std::move(data)); // force-move, not just a cast to &&
 *
 *  Note: The following text appears in the standard:
 *
 *  > In several places in this Clause the operation //DECAY_COPY(x)// is used.
 *  > All such uses mean call the function `decay_copy(x)` and use the result,
 *  > where `decay_copy` is defined as follows:
 *  >
 *  >   template <class T> decay_t<T> decay_copy(T&& v)
 *  >     { return std::forward<T>(v); }
 *  >
 *  > http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2014/n4296.pdf
 *  >   30.2.6 `decay_copy` [thread.decaycopy].
 *
 *  We mimic it, with a `noexcept` specifier for good measure.
 */

template <typename T>
constexpr detail::decay_t<T> copy(T&& value) noexcept(
    noexcept(detail::decay_t<T>(static_cast<T&&>(value)))) {
  return static_cast<T&&>(value);
}

/**
 * A simple helper for getting a constant reference to an object.
 *
 * Example:
 *
 *   std::vector<int> v{1,2,3};
 *   // The following two lines are equivalent:
 *   auto a = const_cast<const std::vector<int>&>(v).begin();
 *   auto b = folly::as_const(v).begin();
 *
 * Like C++17's std::as_const. See http://wg21.link/p0007
 */
#if __cpp_lib_as_const || _LIBCPP_STD_VER > 14 || _MSC_VER

/* using override */ using std::as_const;

#else

template <class T>
constexpr T const& as_const(T& t) noexcept {
  return t;
}

template <class T>
void as_const(T const&&) = delete;

#endif

//  mimic: forward_like, p0847r0
template <typename Src, typename Dst>
constexpr like_t<Src, Dst>&& forward_like(Dst&& dst) noexcept {
  return std::forward<like_t<Src, Dst>>(static_cast<Dst&&>(dst));
}

/**
 *  Backports from C++17 of:
 *    std::in_place_t
 *    std::in_place_type_t
 *    std::in_place_index_t
 *    std::in_place
 *    std::in_place_type
 *    std::in_place_index
 */

#if FOLLY_CPLUSPLUS >= 201703L

using std::in_place_t;

using std::in_place_type_t;

using std::in_place_index_t;

using std::in_place;

using std::in_place_type;

using std::in_place_index;

#else

struct in_place_t {
  explicit in_place_t() = default;
};
FOLLY_INLINE_VARIABLE constexpr in_place_t in_place{};

template <class>
struct in_place_type_t {
  explicit in_place_type_t() = default;
};
template <class T>
FOLLY_INLINE_VARIABLE constexpr in_place_type_t<T> in_place_type{};

template <std::size_t>
struct in_place_index_t {
  explicit in_place_index_t() = default;
};
template <std::size_t I>
FOLLY_INLINE_VARIABLE constexpr in_place_index_t<I> in_place_index{};

#endif

/**
 * Initializer lists are a powerful compile time syntax introduced in C++11
 * but due to their often conflicting syntax they are not used by APIs for
 * construction.
 *
 * Further standard conforming compilers *strongly* favor an
 * std::initializer_list overload for construction if one exists.  The
 * following is a simple tag used to disambiguate construction with
 * initializer lists and regular uniform initialization.
 *
 * For example consider the following case
 *
 *  class Something {
 *  public:
 *    explicit Something(int);
 *    Something(std::initializer_list<int>);
 *
 *    operator int();
 *  };
 *
 *  ...
 *  Something something{1}; // SURPRISE!!
 *
 * The last call to instantiate the Something object will go to the
 * initializer_list overload.  Which may be surprising to users.
 *
 * If however this tag was used to disambiguate such construction it would be
 * easy for users to see which construction overload their code was referring
 * to.  For example
 *
 *  class Something {
 *  public:
 *    explicit Something(int);
 *    Something(folly::initlist_construct_t, std::initializer_list<int>);
 *
 *    operator int();
 *  };
 *
 *  ...
 *  Something something_one{1}; // not the initializer_list overload
 *  Something something_two{folly::initlist_construct, {1}}; // correct
 */
struct initlist_construct_t {};
constexpr initlist_construct_t initlist_construct{};

//  sorted_unique_t, sorted_unique
//
//  A generic tag type and value to indicate that some constructor or method
//  accepts a container in which the values are sorted and unique.
//
//  Example:
//
//    void takes_numbers(folly::sorted_unique_t, std::vector<int> alist) {
//      assert(std::is_sorted(alist.begin(), alist.end()));
//      assert(std::unique(alist.begin(), alist.end()) == alist.end());
//      for (i : alist) {
//        // some behavior which safe only when alist is sorted and unique
//      }
//    }
//    void takes_numbers(std::vector<int> alist) {
//      std::sort(alist.begin(), alist.end());
//      alist.erase(std::unique(alist.begin(), alist.end()), alist.end());
//      takes_numbers(folly::sorted_unique, alist);
//    }
//
//  mimic: std::sorted_unique_t, std::sorted_unique, p0429r6
struct sorted_unique_t {};
constexpr sorted_unique_t sorted_unique{};

//  sorted_equivalent_t, sorted_equivalent
//
//  A generic tag type and value to indicate that some constructor or method
//  accepts a container in which the values are sorted but not necessarily
//  unique.
//
//  Example:
//
//    void takes_numbers(folly::sorted_equivalent_t, std::vector<int> alist) {
//      assert(std::is_sorted(alist.begin(), alist.end()));
//      for (i : alist) {
//        // some behavior which safe only when alist is sorted
//      }
//    }
//    void takes_numbers(std::vector<int> alist) {
//      std::sort(alist.begin(), alist.end());
//      takes_numbers(folly::sorted_equivalent, alist);
//    }
//
//  mimic: std::sorted_equivalent_t, std::sorted_equivalent, p0429r6
struct sorted_equivalent_t {};
constexpr sorted_equivalent_t sorted_equivalent{};

template <typename T>
struct transparent : T {
  using is_transparent = void;
  using T::T;
};

/**
 * A simple function object that passes its argument through unchanged.
 *
 * Example:
 *
 *   int i = 42;
 *   int &j = identity(i);
 *   assert(&i == &j);
 *
 * Warning: passing a prvalue through identity turns it into an xvalue,
 * which can effect whether lifetime extension occurs or not. For instance:
 *
 *   auto&& x = std::make_unique<int>(42);
 *   cout << *x ; // OK, x refers to a valid unique_ptr.
 *
 *   auto&& y = identity(std::make_unique<int>(42));
 *   cout << *y ; // ERROR: y did not lifetime-extend the unique_ptr. It
 *                // is no longer valid
 */
struct identity_fn {
  template <class T>
  constexpr T&& operator()(T&& x) const noexcept {
    return static_cast<T&&>(x);
  }
};
using Identity = identity_fn;
FOLLY_INLINE_VARIABLE constexpr identity_fn identity{};

namespace detail {

template <typename T>
struct inheritable_inherit_ : T {
  using T::T;
  template <
      typename... A,
      std::enable_if_t<std::is_constructible<T, A...>::value, int> = 0>
  /* implicit */ FOLLY_ERASE inheritable_inherit_(A&&... a) noexcept(
      noexcept(T(static_cast<A&&>(a)...)))
      : T(static_cast<A&&>(a)...) {}
};

template <typename T>
struct inheritable_contain_ {
  T v;
  template <
      typename... A,
      std::enable_if_t<std::is_constructible<T, A...>::value, int> = 0>
  /* implicit */ FOLLY_ERASE inheritable_contain_(A&&... a) noexcept(
      noexcept(T(static_cast<A&&>(a)...)))
      : v(static_cast<A&&>(a)...) {}
  FOLLY_ERASE operator T&() & noexcept { return v; }
  FOLLY_ERASE operator T&&() && noexcept { return static_cast<T&&>(v); }
  FOLLY_ERASE operator T const &() const& noexcept { return v; }
  FOLLY_ERASE operator T const &&() const&& noexcept {
    return static_cast<T const&&>(v);
  }
};

template <bool>
struct inheritable_;
template <>
struct inheritable_<false> {
  template <typename T>
  using apply = inheritable_inherit_<T>;
};
template <>
struct inheritable_<true> {
  template <typename T>
  using apply = inheritable_contain_<T>;
};

//  inheritable
//
//  A class wrapping an arbitrary type T which is always inheritable, and which
//  enables empty-base-optimization when possible.
template <typename T>
using inheritable =
    typename inheritable_<std::is_final<T>::value>::template apply<T>;

} // namespace detail

namespace moveonly_ { // Protection from unintended ADL.

template <bool Copy, bool Move>
class EnableCopyMove {
 protected:
  constexpr EnableCopyMove() noexcept = default;
  ~EnableCopyMove() noexcept = default;

  EnableCopyMove(EnableCopyMove&&) noexcept = default;
  EnableCopyMove& operator=(EnableCopyMove&&) noexcept = default;
  EnableCopyMove(const EnableCopyMove&) noexcept = default;
  EnableCopyMove& operator=(const EnableCopyMove&) noexcept = default;
};

/**
 * Disallow copy but not move in derived types. This is essentially
 * boost::noncopyable (the implementation is almost identical) but it
 * doesn't delete move constructor and move assignment.
 */
template <>
class EnableCopyMove<false, true> {
 protected:
  constexpr EnableCopyMove() noexcept = default;
  ~EnableCopyMove() noexcept = default;

  EnableCopyMove(EnableCopyMove&&) noexcept = default;
  EnableCopyMove& operator=(EnableCopyMove&&) noexcept = default;
  EnableCopyMove(const EnableCopyMove&) = delete;
  EnableCopyMove& operator=(const EnableCopyMove&) = delete;
};

template <>
class EnableCopyMove<false, false> {
 protected:
  constexpr EnableCopyMove() noexcept = default;
  ~EnableCopyMove() noexcept = default;

  EnableCopyMove(EnableCopyMove&&) = delete;
  EnableCopyMove& operator=(EnableCopyMove&&) = delete;
  EnableCopyMove(const EnableCopyMove&) = delete;
  EnableCopyMove& operator=(const EnableCopyMove&) = delete;
};
} // namespace moveonly_

using MoveOnly = moveonly_::EnableCopyMove<false, true>;

//  unsafe_default_uninitialized
//  unsafe_default_uninitialized_cv
//
//  An object which is explicitly convertible to any default-constructible type
//  and which, upon conversion, yields a default-initialized value of that type.
//
//  https://en.cppreference.com/w/cpp/language/default_initialization
//
//  For fundamental types, a default-initalized instance may have indeterminate
//  value. Reading an indeterminate value is undefined behavior but may offer a
//  performance optimization. When using an indeterminate value as a performance
//  optimization, it is best to be explicit.
//
//  Useful as an escape hatch when enabling warnings or errors:
//  * gcc:
//    * uninitialized
//    * maybe-uninitialized
//  * clang:
//    * uninitialized
//    * conditional-uninitialized
//    * sometimes-uninitialized
//    * uninitialized-const-reference
//  * msvc:
//    * C4701: potentially uninitialized local variable used
//    * C4703: potentially uninitialized local pointer variable used
//
//  Example:
//
//      int local = folly::unsafe_default_initialized;
//      store_value_into_int_ptr(&value); // suppresses possible warning
//      use_value(value); // suppresses possible warning
struct unsafe_default_initialized_cv {
  template <typename T>
  FOLLY_ERASE constexpr /* implicit */ operator T() const noexcept {
    T uninit;
    FOLLY_PUSH_WARNING
    FOLLY_MSVC_DISABLE_WARNING(4701)
    FOLLY_MSVC_DISABLE_WARNING(4703)
    FOLLY_GNU_DISABLE_WARNING("-Wuninitialized")
    return uninit;
    FOLLY_POP_WARNING
  }
};
FOLLY_INLINE_VARIABLE constexpr unsafe_default_initialized_cv
    unsafe_default_initialized{};

struct to_signed_fn {
  template <typename..., typename T>
  constexpr auto operator()(T const& t) const noexcept ->
      typename std::make_signed<T>::type {
    using S = typename std::make_signed<T>::type;
    // note: static_cast<S>(t) would be more straightforward, but it would also
    // be implementation-defined behavior and that is typically to be avoided;
    // the following code optimized into the same thing, though
    constexpr auto m = static_cast<T>(std::numeric_limits<S>::max());
    return m < t ? -static_cast<S>(~t) + S{-1} : static_cast<S>(t);
  }
};
FOLLY_INLINE_VARIABLE constexpr to_signed_fn to_signed{};

struct to_unsigned_fn {
  template <typename..., typename T>
  constexpr auto operator()(T const& t) const noexcept ->
      typename std::make_unsigned<T>::type {
    using U = typename std::make_unsigned<T>::type;
    return static_cast<U>(t);
  }
};
FOLLY_INLINE_VARIABLE constexpr to_unsigned_fn to_unsigned{};

namespace detail {
template <typename Src, typename Dst>
FOLLY_INLINE_VARIABLE constexpr bool is_to_narrow_convertible_v =
    (std::is_integral<Dst>::value) &&
    (std::is_signed<Dst>::value == std::is_signed<Src>::value);
}

template <typename Src>
class to_narrow_convertible {
  static_assert(std::is_integral<Src>::value, "not an integer");

  template <typename Dst>
  struct to_ : bool_constant<detail::is_to_narrow_convertible_v<Src, Dst>> {};

 public:
  explicit constexpr to_narrow_convertible(Src const& value) noexcept
      : value_(value) {}
#if __cplusplus >= 201703L
  explicit to_narrow_convertible(to_narrow_convertible const&) = default;
  explicit to_narrow_convertible(to_narrow_convertible&&) = default;
#else
  to_narrow_convertible(to_narrow_convertible const&) = default;
  to_narrow_convertible(to_narrow_convertible&&) = default;
#endif
  to_narrow_convertible& operator=(to_narrow_convertible const&) = default;
  to_narrow_convertible& operator=(to_narrow_convertible&&) = default;

  template <typename Dst, std::enable_if_t<to_<Dst>::value, int> = 0>
  /* implicit */ constexpr operator Dst() const noexcept {
    FOLLY_PUSH_WARNING
    FOLLY_MSVC_DISABLE_WARNING(4244) // lossy conversion: arguments
    FOLLY_MSVC_DISABLE_WARNING(4267) // lossy conversion: variables
    FOLLY_GNU_DISABLE_WARNING("-Wconversion")
    return value_;
    FOLLY_POP_WARNING
  }

 private:
  Src value_;
};

//  to_narrow
//
//  A utility for performing explicit possibly-narrowing integral conversion
//  without specifying the destination type. Does not permit changing signs.
//  Sometimes preferable to static_cast<Dst>(src) to document the intended
//  semantics of the cast.
//
//  Models explicit conversion with an elided destination type. Sits in between
//  a stricter explicit conversion with a named destination type and a more
//  lenient implicit conversion. Implemented with implicit conversion in order
//  to take advantage of the undefined-behavior sanitizer's inspection of all
//  implicit conversions - it checks for truncation, with suppressions in place
//  for warnings which guard against narrowing implicit conversions.
struct to_narrow_fn {
  template <typename..., typename Src>
  constexpr auto operator()(Src const& src) const noexcept
      -> to_narrow_convertible<Src> {
    return to_narrow_convertible<Src>{src};
  }
};
FOLLY_INLINE_VARIABLE constexpr to_narrow_fn to_narrow{};

template <typename Src>
class to_integral_convertible {
  static_assert(std::is_floating_point<Src>::value, "not a floating-point");

  template <typename Dst>
  static constexpr bool to_ = std::is_integral<Dst>::value;

 public:
  explicit constexpr to_integral_convertible(Src const& value) noexcept
      : value_(value) {}

#if __cplusplus >= 201703L
  explicit to_integral_convertible(to_integral_convertible const&) = default;
  explicit to_integral_convertible(to_integral_convertible&&) = default;
#else
  to_integral_convertible(to_integral_convertible const&) = default;
  to_integral_convertible(to_integral_convertible&&) = default;
#endif
  to_integral_convertible& operator=(to_integral_convertible const&) = default;
  to_integral_convertible& operator=(to_integral_convertible&&) = default;

  template <typename Dst, std::enable_if_t<to_<Dst>, int> = 0>
  /* implicit */ constexpr operator Dst() const noexcept {
    FOLLY_PUSH_WARNING
    FOLLY_MSVC_DISABLE_WARNING(4244) // lossy conversion: arguments
    FOLLY_MSVC_DISABLE_WARNING(4267) // lossy conversion: variables
    FOLLY_GNU_DISABLE_WARNING("-Wconversion")
    return value_;
    FOLLY_POP_WARNING
  }

 private:
  Src value_;
};

//  to_integral
//
//  A utility for performing explicit floating-point-to-integral conversion
//  without specifying the destination type. Sometimes preferable to
//  static_cast<Dst>(src) to document the intended semantics of the cast.
//
//  Models explicit conversion with an elided destination type. Sits in between
//  a stricter explicit conversion with a named destination type and a more
//  lenient implicit conversion. Implemented with implicit conversion in order
//  to take advantage of the undefined-behavior sanitizer's inspection of all
//  implicit conversions.
struct to_integral_fn {
  template <typename..., typename Src>
  constexpr auto operator()(Src const& src) const noexcept
      -> to_integral_convertible<Src> {
    return to_integral_convertible<Src>{src};
  }
};
FOLLY_INLINE_VARIABLE constexpr to_integral_fn to_integral{};

template <typename Src>
class to_floating_point_convertible {
  static_assert(std::is_integral<Src>::value, "not a floating-point");

  template <typename Dst>
  static constexpr bool to_ = std::is_floating_point<Dst>::value;

 public:
  explicit constexpr to_floating_point_convertible(Src const& value) noexcept
      : value_(value) {}

#if __cplusplus >= 201703L
  explicit to_floating_point_convertible(to_floating_point_convertible const&) =
      default;
  explicit to_floating_point_convertible(to_floating_point_convertible&&) =
      default;
#else
  to_floating_point_convertible(to_floating_point_convertible const&) = default;
  to_floating_point_convertible(to_floating_point_convertible&&) = default;
#endif
  to_floating_point_convertible& operator=(
      to_floating_point_convertible const&) = default;
  to_floating_point_convertible& operator=(to_floating_point_convertible&&) =
      default;

  template <typename Dst, std::enable_if_t<to_<Dst>, int> = 0>
  /* implicit */ constexpr operator Dst() const noexcept {
    FOLLY_PUSH_WARNING
    FOLLY_GNU_DISABLE_WARNING("-Wconversion")
    return value_;
    FOLLY_POP_WARNING
  }

 private:
  Src value_;
};

//  to_floating_point
//
//  A utility for performing explicit integral-to-floating-point conversion
//  without specifying the destination type. Sometimes preferable to
//  static_cast<Dst>(src) to document the intended semantics of the cast.
//
//  Models explicit conversion with an elided destination type. Sits in between
//  a stricter explicit conversion with a named destination type and a more
//  lenient implicit conversion. Implemented with implicit conversion in order
//  to take advantage of the undefined-behavior sanitizer's inspection of all
//  implicit conversions.
struct to_floating_point_fn {
  template <typename..., typename Src>
  constexpr auto operator()(Src const& src) const noexcept
      -> to_floating_point_convertible<Src> {
    return to_floating_point_convertible<Src>{src};
  }
};
FOLLY_INLINE_VARIABLE constexpr to_floating_point_fn to_floating_point{};

struct to_underlying_fn {
  template <typename..., class E>
  constexpr std::underlying_type_t<E> operator()(E e) const noexcept {
    static_assert(std::is_enum<E>::value, "not an enum type");
    return static_cast<std::underlying_type_t<E>>(e);
  }
};
FOLLY_INLINE_VARIABLE constexpr to_underlying_fn to_underlying{};

} // namespace folly
