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
#include <functional>
#include <limits>
#include <memory>
#include <tuple>
#include <type_traits>

#include <folly/Portability.h>

namespace folly {

#if defined(__cpp_lib_type_identity) && __cpp_lib_type_identity >= 201806L

using std::type_identity;
using std::type_identity_t;

#else

/// type_identity_t
/// type_identity
///
/// mimic: std::type_identity_t, std::type_identity, c++20
template <typename T>
struct type_identity {
  using type = T;
};
template <typename T>
using type_identity_t = typename type_identity<T>::type;

#endif

/// tag_t
/// tag
///
/// A generic type-list value type and value.
///
/// A type-list is a class template parameterized by a pack of types.
template <typename...>
struct tag_t {};
template <typename... T>
inline constexpr tag_t<T...> tag{};

/// vtag_t
/// vtag
///
/// A generic value-list value type and value.
///
/// A value-list is a class template parameterized by a pack of values.
template <auto...>
struct vtag_t {};
template <auto... V>
inline constexpr vtag_t<V...> vtag{};

template <std::size_t I>
using index_constant = std::integral_constant<std::size_t, I>;

/// always_false
///
/// A variable template that is always false but requires template arguments to
/// be provided (which are then ignored). This is useful in very specific cases
/// where we want type-dependent expressions to defer static_assert's.
///
/// A common use-case is for exhaustive constexpr if branches:
///
///   template <typename T>
///   void foo(T value) {
///     if constexpr (std::is_integral_v<T>) foo_integral(value);
///     else if constexpr (std::is_same_v<T, std::string>) foo_string(value);
///     else static_assert(always_false<T>, "Unsupported type");
///   }
///
/// If we had used static_assert(false), then this would always fail to compile,
/// even if foo is never instantiated!
///
/// Another use case is if a template that is expected to always be specialized
/// is erroneously instantiated with the base template.
///
///   template <typename T>
///   struct Foo {
///     static_assert(always_false<T>, "Unsupported type");
///   };
///   template <>
///   struct Foo<int> {};
///
///   Foo<int> a;         // fine
///   Foo<std::string> b; // fails! And you get a nice (custom) error message
///
/// This is similar to leaving the base template undefined but we get a nicer
/// compiler error message with static_assert.
template <typename...>
inline constexpr bool always_false = false;

namespace detail {

template <typename Void, typename T>
struct require_sizeof_ {
  static_assert(always_false<T>, "application of sizeof fails substitution");
};
template <typename T>
struct require_sizeof_<decltype(void(sizeof(T))), T> {
  template <typename V>
  using apply_t = V;

  static constexpr std::size_t size = sizeof(T);
};

} // namespace detail

/// require_sizeof
///
/// Equivalent to sizeof, but with a static_assert enforcing that application of
/// sizeof would not fail substitution.
template <typename T>
constexpr std::size_t require_sizeof = detail::require_sizeof_<void, T>::size;

/// is_unbounded_array_v
/// is_unbounded_array
///
/// A trait variable and type to check if a given type is an unbounded array.
///
/// mimic: std::is_unbounded_array_d, std::is_unbounded_array (C++20)
template <typename T>
inline constexpr bool is_unbounded_array_v = false;
template <typename T>
inline constexpr bool is_unbounded_array_v<T[]> = true;
template <typename T>
struct is_unbounded_array : std::bool_constant<is_unbounded_array_v<T>> {};

/// is_bounded_array_v
/// is_bounded_array
///
/// A trait variable and type to check if a given type is a bounded array.
///
/// mimic: std::is_bounded_array_d, std::is_bounded_array (C++20)
template <typename T>
inline constexpr bool is_bounded_array_v = false;
template <typename T, std::size_t S>
inline constexpr bool is_bounded_array_v<T[S]> = true;
template <typename T>
struct is_bounded_array : std::bool_constant<is_bounded_array_v<T>> {};

/// is_instantiation_of_v
/// is_instantiation_of
/// instantiated_from
/// uncvref_instantiated_from
///
/// A trait variable and type to check if a given type is an instantiation of a
/// class template. And corresponding concepts.
///
/// Note that this only works with type template parameters. It does not work
/// with non-type template parameters, template template parameters, or alias
/// templates.
template <template <typename...> class, typename>
inline constexpr bool is_instantiation_of_v = false;
template <template <typename...> class C, typename... T>
inline constexpr bool is_instantiation_of_v<C, C<T...>> = true;
template <template <typename...> class C, typename... T>
struct is_instantiation_of
    : std::bool_constant<is_instantiation_of_v<C, T...>> {};

#if defined(__cpp_concepts)

template <typename T, template <typename...> class Templ>
concept instantiated_from = is_instantiation_of_v<Templ, T>;

template <typename T, template <typename...> class Templ>
concept uncvref_instantiated_from =
    is_instantiation_of_v<Templ, std::remove_cvref_t<T>>;

#endif

/// member_pointer_traits
///
/// For a member-pointer, reveals its constituent member-type and object-type.
///
/// Works for both member-object-pointer and member-function-pointer.
template <typename>
struct member_pointer_traits;
template <typename M, typename O>
struct member_pointer_traits<M O::*> {
  using member_type = M;
  using object_type = O;
};

namespace detail {

struct is_constexpr_default_constructible_ {
  template <typename T>
  static constexpr auto make(tag_t<T>) -> decltype(void(T()), 0) {
    return (void(T()), 0);
  }
  //  second param should just be: int = (void(T()), 0)
  //  but under clang 10, crash: https://bugs.llvm.org/show_bug.cgi?id=47620
  //  and, with assertions disabled, expectation failures showing compiler
  //  deviation from the language spec
  //  xcode renumbers clang versions so detection is tricky, but, if detection
  //  were desired, a combination of __apple_build_version__ and __clang_major__
  //  may be used to reduce frontend overhead under correct compilers: clang 12
  //  under xcode and clang 10 otherwise
  template <typename T, int = make(tag<T>)>
  static std::true_type sfinae(T*);
  static std::false_type sfinae(void*);
  template <typename T>
  static constexpr bool apply =
      !require_sizeof<T> || decltype(sfinae(static_cast<T*>(nullptr)))::value;
};

} // namespace detail

/// is_constexpr_default_constructible_v
/// is_constexpr_default_constructible
///
/// A trait variable and type which determines whether the type parameter is
/// constexpr default-constructible, that is, default-constructible in a
/// constexpr context.
template <typename T>
inline constexpr bool is_constexpr_default_constructible_v =
    detail::is_constexpr_default_constructible_::apply<T>;
template <typename T>
struct is_constexpr_default_constructible
    : std::bool_constant<is_constexpr_default_constructible_v<T>> {};

/***
 *  _t
 *
 *  Instead of:
 *
 *    using decayed = typename std::decay<T>::type;
 *
 *  With the C++14 standard trait aliases, we could use:
 *
 *    using decayed = std::decay_t<T>;
 *
 *  Without them, we could use:
 *
 *    using decayed = _t<std::decay<T>>;
 *
 *  Also useful for any other library with template types having dependent
 *  member types named `type`, like the standard trait types.
 */
template <typename T>
using _t = typename T::type;

/**
 * A type trait to remove all const volatile and reference qualifiers on a
 * type T
 */
template <typename T>
struct remove_cvref {
  using type =
      typename std::remove_cv<typename std::remove_reference<T>::type>::type;
};
template <typename T>
using remove_cvref_t = typename remove_cvref<T>::type;

namespace detail {
template <typename Src>
struct like_ {
  template <typename Dst>
  using apply = Dst;
};
template <typename Src>
struct like_<Src const> {
  template <typename Dst>
  using apply = Dst const;
};
template <typename Src>
struct like_<Src volatile> {
  template <typename Dst>
  using apply = Dst volatile;
};
template <typename Src>
struct like_<Src const volatile> {
  template <typename Dst>
  using apply = Dst const volatile;
};
template <typename Src>
struct like_<Src&> {
  template <typename Dst>
  using apply = typename like_<Src>::template apply<Dst>&;
};
template <typename Src>
struct like_<Src&&> {
  template <typename Dst>
  using apply = typename like_<Src>::template apply<Dst>&&;
};
} // namespace detail

//  mimic: like_t, p0847r0
template <typename Src, typename Dst>
using like_t = typename detail::like_<Src>::template apply<remove_cvref_t<Dst>>;

//  mimic: like, p0847r0
template <typename Src, typename Dst>
struct like {
  using type = like_t<Src, Dst>;
};

#if defined(__cpp_concepts)

/**
 *  Concept to check that a type is same as a given type,
 *  when stripping qualifiers and refernces.
 *  Especially useful for perfect forwarding of a specific type.
 *
 *  Example:
 *
 *    void foo(folly::uncvref_same_as<std::vector<int>> auto&& vec);
 *
 */
template <typename Ref, typename To>
concept uncvref_same_as = std::is_same_v<std::remove_cvref_t<Ref>, To>;

#endif

/**
 *  type_t
 *
 *  A type alias for the first template type argument. `type_t` is useful for
 *  controlling class-template and function-template partial specialization.
 *
 *  Example:
 *
 *    template <typename Value>
 *    class Container {
 *     public:
 *      template <typename... Args>
 *      Container(
 *          type_t<in_place_t, decltype(Value(std::declval<Args>()...))>,
 *          Args&&...);
 *    };
 *
 *  void_t
 *
 *  A type alias for `void`. `void_t` is useful for controlling class-template
 *  and function-template partial specialization.
 *
 *  Example:
 *
 *    // has_value_type<T>::value is true if T has a nested type `value_type`
 *    template <class T, class = void>
 *    struct has_value_type
 *        : std::false_type {};
 *
 *    template <class T>
 *    struct has_value_type<T, folly::void_t<typename T::value_type>>
 *        : std::true_type {};
 */

/**
 * There is a bug in libstdc++, libc++, and MSVC's STL that causes it to
 * ignore unused template parameter arguments in template aliases and does not
 * cause substitution failures. This defect has been recorded here:
 * http://open-std.org/JTC1/SC22/WG21/docs/cwg_defects.html#1558.
 *
 * This causes the implementation of std::void_t to be buggy, as it is likely
 * defined as something like the following:
 *
 *  template <typename...>
 *  using void_t = void;
 *
 * This causes the compiler to ignore all the template arguments and does not
 * help when one wants to cause substitution failures.  Rather declarations
 * which have void_t in orthogonal specializations are treated as the same.
 * For example, assuming the possible `T` types are only allowed to have
 * either the alias `one` or `two` and never both or none:
 *
 *  template <typename T,
 *            typename std::void_t<std::decay_t<T>::one>* = nullptr>
 *  void foo(T&&) {}
 *  template <typename T,
 *            typename std::void_t<std::decay_t<T>::two>* = nullptr>
 *  void foo(T&&) {}
 *
 * The second foo() will be a redefinition because it conflicts with the first
 * one; void_t does not cause substitution failures - the template types are
 * just ignored.
 */

namespace traits_detail {
template <class T, class...>
struct type_t_ {
  using type = T;
};
} // namespace traits_detail

template <class T, class... Ts>
using type_t = typename traits_detail::type_t_<T, Ts...>::type;
template <class... Ts>
using void_t = type_t<void, Ts...>;

/// nonesuch
///
/// A tag type which traits may use to indicate lack of a result type.
///
/// Similar to void in that no values of this type may be constructed. Different
/// from void in that no functions may be defined with this return type and no
/// complete expressions may evaluate with this expression type.
///
/// mimic: std::experimental::nonesuch, Library Fundamentals TS v2
struct nonesuch {
  ~nonesuch() = delete;
  nonesuch(nonesuch const&) = delete;
  void operator=(nonesuch const&) = delete;
};

namespace detail {

template <typename Void, typename D, template <typename...> class, typename...>
struct detected_ {
  using value_t = std::false_type;
  using type = D;
};
template <typename D, template <typename...> class T, typename... A>
struct detected_<void_t<T<A...>>, D, T, A...> {
  using value_t = std::true_type;
  using type = T<A...>;
};

} // namespace detail

/// detected_or
///
/// If T<A...> substitutes, has member type alias value_t as std::true_type
/// and has member type alias type as T<A...>. Otherwise, has member type
/// alias value_t as std::false_type and has member type alias type as D.
///
/// mimic: std::experimental::detected_or, Library Fundamentals TS v2
///
/// Note: not resilient against incomplete types; may violate ODR.
template <typename D, template <typename...> class T, typename... A>
using detected_or = detail::detected_<void, D, T, A...>;

/// detected_or_t
///
/// A trait type alias which results in T<A...> if substitution would succeed
/// and in D otherwise.
///
/// Equivalent to detected_or<D, T, A...>::type.
///
/// mimic: std::experimental::detected_or_t, Library Fundamentals TS v2
///
/// Note: not resilient against incomplete types; may violate ODR.
template <typename D, template <typename...> class T, typename... A>
using detected_or_t = typename detected_or<D, T, A...>::type;

/// detected_t
///
/// A trait type alias which results in T<A...> if substitution would succeed
/// and in nonesuch otherwise.
///
/// Equivalent to detected_or_t<nonesuch, T, A...>.
///
/// mimic: std::experimental::detected_t, Library Fundamentals TS v2
///
/// Note: not resilient against incomplete types; may violate ODR.
template <template <typename...> class T, typename... A>
using detected_t = detected_or_t<nonesuch, T, A...>;

//  is_detected_v
//  is_detected
//
//  A trait variable and type to test whether some metafunction from types to
//  types would succeed or fail in substitution over a given set of arguments.
//
//  The trait variable is_detected_v<T, A...> is equivalent to
//  detected_or<nonesuch, T, A...>::value_t::value.
//  The trait type is_detected<T, A...> unambiguously inherits
//  std::bool_constant<V> where V is is_detected_v<T, A...>.
//
//  mimic: std::experimental::is_detected, std::experimental::is_detected_v,
//    Library Fundamentals TS v2
//
//  Note: not resilient against incomplete types; may violate ODR.
//
//  Note: the trait type is_detected differs here by being deferred.
template <template <typename...> class T, typename... A>
inline constexpr bool is_detected_v =
    detected_or<nonesuch, T, A...>::value_t::value;
template <template <typename...> class T, typename... A>
struct is_detected : detected_or<nonesuch, T, A...>::value_t {};

template <typename T>
using aligned_storage_for_t =
    typename std::aligned_storage<sizeof(T), alignof(T)>::type;

//  ----

namespace fallback {
template <typename From, typename To>
inline constexpr bool is_nothrow_convertible_v =
    (std::is_void<From>::value && std::is_void<To>::value) ||
    ( //
        std::is_convertible<From, To>::value &&
        std::is_nothrow_constructible<To, From>::value);
template <typename From, typename To>
struct is_nothrow_convertible
    : std::bool_constant<is_nothrow_convertible_v<From, To>> {};
} // namespace fallback

//  is_nothrow_convertible
//  is_nothrow_convertible_v
//
//  Import or backport:
//  * std::is_nothrow_convertible
//  * std::is_nothrow_convertible_v
//
//  mimic: is_nothrow_convertible, C++20
#if defined(__cpp_lib_is_nothrow_convertible) && \
    __cpp_lib_is_nothrow_convertible >= 201806L
using std::is_nothrow_convertible;
using std::is_nothrow_convertible_v;
#else
using fallback::is_nothrow_convertible;
using fallback::is_nothrow_convertible_v;
#endif

/**
 * IsRelocatable<T>::value describes the ability of moving around
 * memory a value of type T by using memcpy (as opposed to the
 * conservative approach of calling the copy constructor and then
 * destroying the old temporary. Essentially for a relocatable type,
 * the following two sequences of code should be semantically
 * equivalent:
 *
 * void move1(T * from, T * to) {
 *   new(to) T(from);
 *   (*from).~T();
 * }
 *
 * void move2(T * from, T * to) {
 *   memcpy(to, from, sizeof(T));
 * }
 *
 * Most C++ types are relocatable; the ones that aren't would include
 * internal pointers or (very rarely) would need to update remote
 * pointers to pointers tracking them. All C++ primitive types and
 * type constructors are relocatable.
 *
 * This property can be used in a variety of optimizations. Currently
 * fbvector uses this property intensively.
 *
 * The default conservatively assumes the type is not
 * relocatable. Several specializations are defined for known
 * types. You may want to add your own specializations. Do so in
 * namespace folly and make sure you keep the specialization of
 * IsRelocatable<SomeStruct> in the same header as SomeStruct.
 *
 * You may also declare a type to be relocatable by including
 *    `typedef std::true_type IsRelocatable;`
 * in the class header.
 *
 * It may be unset in a base class by overriding the typedef to false_type.
 */
/*
 * IsZeroInitializable describes the property that value-initialization
 * is the same as memset(dst, 0, sizeof(T)).
 */

namespace traits_detail {

#define FOLLY_HAS_TRUE_XXX(name)                                             \
  template <typename T>                                                      \
  using detect_##name = typename T::name;                                    \
  template <class T>                                                         \
  struct name##_is_true : std::is_same<typename T::name, std::true_type> {}; \
  template <class T>                                                         \
  struct has_true_##name : std::conditional<                                 \
                               is_detected_v<detect_##name, T>,              \
                               name##_is_true<T>,                            \
                               std::false_type>::type {}

FOLLY_HAS_TRUE_XXX(IsRelocatable);
FOLLY_HAS_TRUE_XXX(IsZeroInitializable);

#undef FOLLY_HAS_TRUE_XXX

} // namespace traits_detail

struct Ignore {
  Ignore() = default;
  template <class T>
  constexpr /* implicit */ Ignore(const T&) {}
  template <class T>
  const Ignore& operator=(T const&) const {
    return *this;
  }
};

template <class...>
using Ignored = Ignore;

namespace traits_detail_IsEqualityComparable {
Ignore operator==(Ignore, Ignore);

template <class T, class U = T>
struct IsEqualityComparable
    : std::is_convertible<
          decltype(std::declval<T>() == std::declval<U>()),
          bool> {};
} // namespace traits_detail_IsEqualityComparable

/* using override */ using traits_detail_IsEqualityComparable::
    IsEqualityComparable;

namespace traits_detail_IsLessThanComparable {
Ignore operator<(Ignore, Ignore);

template <class T, class U = T>
struct IsLessThanComparable
    : std::is_convertible<
          decltype(std::declval<T>() < std::declval<U>()),
          bool> {};
} // namespace traits_detail_IsLessThanComparable

/* using override */ using traits_detail_IsLessThanComparable::
    IsLessThanComparable;

template <class T>
struct IsRelocatable
    : std::conditional<
          !require_sizeof<T> ||
              is_detected_v<traits_detail::detect_IsRelocatable, T>,
          traits_detail::has_true_IsRelocatable<T>,
#if defined(__cpp_lib_is_trivially_relocatable) // P1144
          std::is_trivially_relocatable<T>
#else
          std::is_trivially_copyable<T>
#endif
          >::type {
};

template <class T>
struct IsZeroInitializable
    : std::conditional<
          !require_sizeof<T> ||
              is_detected_v<traits_detail::detect_IsZeroInitializable, T>,
          traits_detail::has_true_IsZeroInitializable<T>,
          std::bool_constant< //
              !std::is_class<T>::value && //
              !std::is_union<T>::value && //
              !std::is_member_object_pointer<T>::value && // itanium
              true>>::type {};

namespace detail {
template <bool>
struct conditional_;
template <>
struct conditional_<false> {
  template <typename, typename T>
  using apply = T;
};
template <>
struct conditional_<true> {
  template <typename T, typename>
  using apply = T;
};
} // namespace detail

/// conditional_t
///
/// Like std::conditional_t but with only two total class template instances,
/// rather than as many class template instances as there are uses.
///
/// As one effect, the result can be used in deducible contexts, allowing
/// deduction of conditional_t<V, T, F> to work when T or F is a template param.
template <bool V, typename T, typename F>
using conditional_t = typename detail::conditional_<V>::template apply<T, F>;

template <typename...>
struct Conjunction : std::true_type {};
template <typename T>
struct Conjunction<T> : T {};
template <typename T, typename... TList>
struct Conjunction<T, TList...>
    : std::conditional<T::value, Conjunction<TList...>, T>::type {};

template <typename...>
struct Disjunction : std::false_type {};
template <typename T>
struct Disjunction<T> : T {};
template <typename T, typename... TList>
struct Disjunction<T, TList...>
    : std::conditional<T::value, T, Disjunction<TList...>>::type {};

template <typename T>
struct Negation : std::bool_constant<!T::value> {};

template <bool... Bs>
struct Bools {
  using valid_type = bool;
  static constexpr std::size_t size() { return sizeof...(Bs); }
};

//  Lighter-weight than Conjunction, but evaluates all sub-conditions eagerly.
template <class... Ts>
struct StrictConjunction
    : std::is_same<Bools<Ts::value...>, Bools<(Ts::value || true)...>> {};

template <class... Ts>
struct StrictDisjunction
    : Negation<
          std::is_same<Bools<Ts::value...>, Bools<(Ts::value && false)...>>> {};

namespace detail {
template <typename T>
using is_transparent_ = typename T::is_transparent;
} // namespace detail

/// is_transparent_v
/// is_transparent
///
/// A trait variable and type to test whether a less, equal-to, or hash type
/// follows the is-transparent protocol used by containers with optional
/// heterogeneous access.
template <typename T>
inline constexpr bool is_transparent_v =
    is_detected_v<detail::is_transparent_, T>;
template <typename T>
struct is_transparent : std::bool_constant<is_transparent_v<T>> {};

namespace detail {

template <typename T, typename = void>
inline constexpr bool is_allocator_ = !require_sizeof<T>;
template <typename T>
inline constexpr bool is_allocator_<
    T,
    void_t<
        typename T::value_type,
        decltype(std::declval<T&>().allocate(std::size_t{})),
        decltype(std::declval<T&>().deallocate(
            static_cast<typename T::value_type*>(nullptr), std::size_t{}))>> =
    true;

} // namespace detail

/// is_allocator_v
/// is_allocator
///
/// A trait variable and type to test whether a type is an allocator according
/// to the minimum protocol required by std::allocator_traits.
template <typename T>
inline constexpr bool is_allocator_v = detail::is_allocator_<T>;
template <typename T>
struct is_allocator : std::bool_constant<is_allocator_v<T>> {};

} // namespace folly

/**
 * Use this macro ONLY inside namespace folly. When using it with a
 * regular type, use it like this:
 *
 * // Make sure you're at namespace ::folly scope
 * template <> FOLLY_ASSUME_RELOCATABLE(MyType)
 *
 * When using it with a template type, use it like this:
 *
 * // Make sure you're at namespace ::folly scope
 * template <class T1, class T2>
 * FOLLY_ASSUME_RELOCATABLE(MyType<T1, T2>)
 */
#define FOLLY_ASSUME_RELOCATABLE(...) \
  struct IsRelocatable<__VA_ARGS__> : std::true_type {}

/**
 * The FOLLY_ASSUME_FBVECTOR_COMPATIBLE* macros below encode the
 * assumption that the type is relocatable per IsRelocatable
 * above. Many types can be assumed to satisfy this condition, but
 * it is the responsibility of the user to state that assumption.
 * User-defined classes will not be optimized for use with
 * fbvector (see FBVector.h) unless they state that assumption.
 *
 * Use FOLLY_ASSUME_FBVECTOR_COMPATIBLE with regular types like this:
 *
 * FOLLY_ASSUME_FBVECTOR_COMPATIBLE(MyType)
 *
 * The versions FOLLY_ASSUME_FBVECTOR_COMPATIBLE_1, _2, _3, and _4
 * allow using the macro for describing templatized classes with 1, 2,
 * 3, and 4 template parameters respectively. For template classes
 * just use the macro with the appropriate number and pass the name of
 * the template to it. Example:
 *
 * template <class T1, class T2> class MyType { ... };
 * ...
 * // Make sure you're at global scope
 * FOLLY_ASSUME_FBVECTOR_COMPATIBLE_2(MyType)
 */

// Use this macro ONLY at global level (no namespace)
#define FOLLY_ASSUME_FBVECTOR_COMPATIBLE(...) \
  namespace folly {                           \
  template <>                                 \
  FOLLY_ASSUME_RELOCATABLE(__VA_ARGS__);      \
  }
// Use this macro ONLY at global level (no namespace)
#define FOLLY_ASSUME_FBVECTOR_COMPATIBLE_1(...) \
  namespace folly {                             \
  template <class T1>                           \
  FOLLY_ASSUME_RELOCATABLE(__VA_ARGS__<T1>);    \
  }
// Use this macro ONLY at global level (no namespace)
#define FOLLY_ASSUME_FBVECTOR_COMPATIBLE_2(...)  \
  namespace folly {                              \
  template <class T1, class T2>                  \
  FOLLY_ASSUME_RELOCATABLE(__VA_ARGS__<T1, T2>); \
  }
// Use this macro ONLY at global level (no namespace)
#define FOLLY_ASSUME_FBVECTOR_COMPATIBLE_3(...)      \
  namespace folly {                                  \
  template <class T1, class T2, class T3>            \
  FOLLY_ASSUME_RELOCATABLE(__VA_ARGS__<T1, T2, T3>); \
  }
// Use this macro ONLY at global level (no namespace)
#define FOLLY_ASSUME_FBVECTOR_COMPATIBLE_4(...)          \
  namespace folly {                                      \
  template <class T1, class T2, class T3, class T4>      \
  FOLLY_ASSUME_RELOCATABLE(__VA_ARGS__<T1, T2, T3, T4>); \
  }

namespace folly {

// STL commonly-used types
template <class T, class U>
struct IsRelocatable<std::pair<T, U>>
    : std::bool_constant<IsRelocatable<T>::value && IsRelocatable<U>::value> {};

// Is T one of T1, T2, ..., Tn?
template <typename T, typename... Ts>
using IsOneOf = StrictDisjunction<std::is_same<T, Ts>...>;

/*
 * Complementary type traits for integral comparisons.
 *
 * For instance, `if(x < 0)` yields an error in clang for unsigned types
 * when -Werror is used due to -Wtautological-compare
 */

// same as `x < 0`
template <typename T>
constexpr bool is_negative(T x) {
  return std::is_signed<T>::value && x < T(0);
}

// same as `x <= 0`
template <typename T>
constexpr bool is_non_positive(T x) {
  return !x || folly::is_negative(x);
}

// same as `x > 0`
template <typename T>
constexpr bool is_positive(T x) {
  return !is_non_positive(x);
}

// same as `x >= 0`
template <typename T>
constexpr bool is_non_negative(T x) {
  return !x || is_positive(x);
}

namespace detail {

//  folly::to integral specializations can end up generating code
//  inside what are really static ifs (not executed because of the templated
//  types) that violate -Wsign-compare and/or -Wbool-compare so suppress them
//  in order to not prevent all calling code from using it.
FOLLY_PUSH_WARNING
FOLLY_GNU_DISABLE_WARNING("-Wsign-compare")
FOLLY_GCC_DISABLE_WARNING("-Wbool-compare")
FOLLY_MSVC_DISABLE_WARNING(4287) // unsigned/negative constant mismatch
FOLLY_MSVC_DISABLE_WARNING(4388) // sign-compare
FOLLY_MSVC_DISABLE_WARNING(4804) // bool-compare

template <typename RHS, RHS rhs, typename LHS>
bool less_than_impl(LHS const lhs) {
  // clang-format off
  return
      // Ensure signed and unsigned values won't be compared directly.
      (!std::is_signed<RHS>::value && is_negative(lhs)) ? true :
      (!std::is_signed<LHS>::value && is_negative(rhs)) ? false :
      rhs > std::numeric_limits<LHS>::max() ? true :
      rhs <= std::numeric_limits<LHS>::lowest() ? false :
      lhs < rhs;
  // clang-format on
}

template <typename RHS, RHS rhs, typename LHS>
bool greater_than_impl(LHS const lhs) {
  // clang-format off
  return
      // Ensure signed and unsigned values won't be compared directly.
      (!std::is_signed<RHS>::value && is_negative(lhs)) ? false :
      (!std::is_signed<LHS>::value && is_negative(rhs)) ? true :
      rhs > std::numeric_limits<LHS>::max() ? false :
      rhs < std::numeric_limits<LHS>::lowest() ? true :
      lhs > rhs;
  // clang-format on
}

FOLLY_POP_WARNING

} // namespace detail

template <typename RHS, RHS rhs, typename LHS>
bool less_than(LHS const lhs) {
  return detail::
      less_than_impl<RHS, rhs, typename std::remove_reference<LHS>::type>(lhs);
}

template <typename RHS, RHS rhs, typename LHS>
bool greater_than(LHS const lhs) {
  return detail::
      greater_than_impl<RHS, rhs, typename std::remove_reference<LHS>::type>(
          lhs);
}
} // namespace folly

// Assume nothing when compiling with MSVC.
#ifndef _MSC_VER
FOLLY_ASSUME_FBVECTOR_COMPATIBLE_2(std::unique_ptr)
FOLLY_ASSUME_FBVECTOR_COMPATIBLE_1(std::shared_ptr)
#endif

namespace folly {

//  Some compilers have signed __int128 and unsigned __int128 types, and some
//  libraries with some compilers have traits for those types. It's a mess.
//  Import things into folly and then fill in whatever is missing.
//
//  The aliases:
//    int128_t
//    uint128_t
//
//  The traits:
//    is_arithmetic
//    is_arithmetic_v
//    is_integral
//    is_integral_v
//    is_signed
//    is_signed_v
//    is_unsigned
//    is_unsigned_v
//    make_signed
//    make_signed_t
//    make_unsigned
//    make_unsigned_t

template <typename T>
struct is_arithmetic : std::is_arithmetic<T> {};
template <typename T>
inline constexpr bool is_arithmetic_v = is_arithmetic<T>::value;

template <typename T>
struct is_integral : std::is_integral<T> {};
template <typename T>
inline constexpr bool is_integral_v = is_integral<T>::value;

template <typename T>
struct is_signed : std::is_signed<T> {};
template <typename T>
inline constexpr bool is_signed_v = is_signed<T>::value;

template <typename T>
struct is_unsigned : std::is_unsigned<T> {};
template <typename T>
inline constexpr bool is_unsigned_v = is_unsigned<T>::value;

template <typename T>
struct make_signed : std::make_signed<T> {};
template <typename T>
using make_signed_t = typename make_signed<T>::type;

template <typename T>
struct make_unsigned : std::make_unsigned<T> {};
template <typename T>
using make_unsigned_t = typename make_unsigned<T>::type;

#if FOLLY_HAVE_INT128_T

using int128_t = signed __int128;
using uint128_t = unsigned __int128;

template <>
struct is_arithmetic<int128_t> : std::true_type {};
template <>
struct is_arithmetic<uint128_t> : std::true_type {};

template <>
struct is_integral<int128_t> : std::true_type {};
template <>
struct is_integral<uint128_t> : std::true_type {};

template <>
struct is_signed<int128_t> : std::true_type {};
template <>
struct is_signed<uint128_t> : std::false_type {};
template <>
struct is_unsigned<int128_t> : std::false_type {};
template <>
struct is_unsigned<uint128_t> : std::true_type {};

template <>
struct make_signed<int128_t> {
  using type = int128_t;
};
template <>
struct make_signed<uint128_t> {
  using type = int128_t;
};

template <>
struct make_unsigned<int128_t> {
  using type = uint128_t;
};
template <>
struct make_unsigned<uint128_t> {
  using type = uint128_t;
};
#endif // FOLLY_HAVE_INT128_T

namespace traits_detail {
template <std::size_t>
struct uint_bits_t_ {};
template <>
struct uint_bits_t_<8> : type_t_<std::uint8_t> {};
template <>
struct uint_bits_t_<16> : type_t_<std::uint16_t> {};
template <>
struct uint_bits_t_<32> : type_t_<std::uint32_t> {};
template <>
struct uint_bits_t_<64> : type_t_<std::uint64_t> {};
#if FOLLY_HAVE_INT128_T
template <>
struct uint_bits_t_<128> : type_t_<uint128_t> {};
#endif // FOLLY_HAVE_INT128_T
} // namespace traits_detail

template <std::size_t bits>
using uint_bits_t = _t<traits_detail::uint_bits_t_<bits>>;

template <std::size_t lg_bits>
using uint_bits_lg_t = uint_bits_t<(1u << lg_bits)>;

template <std::size_t bits>
using int_bits_t = make_signed_t<uint_bits_t<bits>>;

template <std::size_t lg_bits>
using int_bits_lg_t = make_signed_t<uint_bits_lg_t<lg_bits>>;

namespace traits_detail {

template <std::size_t I, typename T>
struct type_pack_element_indexed_type {
  using type = T;
};

template <typename, typename...>
struct type_pack_element_set;
template <std::size_t... I, typename... T>
struct type_pack_element_set<std::index_sequence<I...>, T...>
    : type_pack_element_indexed_type<I, T>... {};
template <typename... T>
using type_pack_element_set_t =
    type_pack_element_set<std::index_sequence_for<T...>, T...>;

template <std::size_t I>
struct type_pack_element_test {
  template <typename T>
  static type_pack_element_indexed_type<I, T> impl(
      type_pack_element_indexed_type<I, T>*);
};

template <std::size_t I, typename... Ts>
using type_pack_element_fallback = _t<decltype(type_pack_element_test<I>::impl(
    static_cast<type_pack_element_set_t<Ts...>*>(nullptr)))>;

} // namespace traits_detail

/// type_pack_element_t
///
/// In the type pack Ts..., the Ith element.
///
/// Wraps the builtin __type_pack_element where the builtin is available; where
/// not, implemented directly.
///
/// Under gcc, the builtin is available but does not mangle. Therefore, this
/// trait must not be used anywhere it might be subject to mangling, such as in
/// a return-type expression.

#if FOLLY_HAS_BUILTIN(__type_pack_element)

template <std::size_t I, typename... Ts>
using type_pack_element_t = __type_pack_element<I, Ts...>;

#else

template <std::size_t I, typename... Ts>
using type_pack_element_t = traits_detail::type_pack_element_fallback<I, Ts...>;

#endif

/// type_pack_size_v
///
/// The size of a type pack.
///
/// A metafunction around sizeof...(Ts).
template <typename... Ts>
inline constexpr std::size_t type_pack_size_v = sizeof...(Ts);

/// type_pack_size_t
///
/// The size of a type pack.
///
/// A metafunction around index_constant<sizeof...(Ts)>.
template <typename... Ts>
using type_pack_size_t = index_constant<sizeof...(Ts)>;

namespace traits_detail {

template <std::size_t I, template <typename...> class List, typename... T>
type_identity<type_pack_element_t<I, T...>> type_list_element_(
    List<T...> const*);

template <template <typename...> class List, typename... T>
index_constant<sizeof...(T)> type_list_size_(List<T...> const*);

} // namespace traits_detail

/// type_list_element_t
///
/// In the type list List<T...>, where List has kind template <typename...> and
/// T... is a type-pack, equivalent to type_pack_element_t<I, T...>.
template <std::size_t I, typename List>
using type_list_element_t = _t<decltype(traits_detail::type_list_element_<I>(
    static_cast<List const*>(nullptr)))>;

/// type_list_size_v
///
/// The size of a type list.
///
/// For List<T...>, equivalent to type_pack_size_v<T...>.
template <typename List>
inline constexpr std::size_t type_list_size_v =
    decltype(traits_detail::type_list_size_(
        static_cast<List const*>(nullptr)))::value;

/// type_list_size_t
///
/// The size of a type list.
///
/// For List<T...>, equivalent to type_pack_size_t<T...>.
template <typename List>
using type_list_size_t =
    decltype(traits_detail::type_list_size_(static_cast<List const*>(nullptr)));

namespace detail {

// The arguments to this "error" type help the user debug bad invocations.
// It is purposely undefined to cause a compile error.
template <typename...>
struct error_list_concat_params_should_be_non_cvref;

// The primary template is only invoked for invalid parameters.
template <template <typename...> class Out, typename... T>
inline constexpr auto type_list_concat_ =
    error_list_concat_params_should_be_non_cvref<T...>{};

template <template <typename...> class Out>
inline constexpr type_identity<Out<>> type_list_concat_<Out>;

template <
    template <typename...>
    class Out,
    template <typename...>
    class In,
    typename... T>
inline constexpr auto type_list_concat_<Out, In<T...>> =
    type_identity<Out<T...>>{};

template <
    template <typename...>
    class Out,
    // Allow input lists to come from heterogeneous templates.
    template <typename...>
    class InA,
    typename... A,
    template <typename...>
    class InB,
    typename... B,
    typename... Tail>
inline constexpr auto type_list_concat_<Out, InA<A...>, InB<B...>, Tail...> =
    // Avoid instantiating the `In*` or `Out` types for the intermediate
    // lists, since those types may be invalid, or expensive.  Per my tests
    // on clang using `tag_t` for the intermediate list is no more expensive
    // than using a dedicated incomplete list type.
    type_list_concat_<Out, tag_t<A..., B...>, Tail...>;

} // namespace detail

/// type_list_concat_t
///
/// Each `List` is a type list of the form `InK<TypeK...>`, where the
/// templates `InK` are potentially heterogeneous.  Concatenates these
/// `List`s into a single type list `Out<Type1..., Type2..., ...>`.
template <template <typename...> class Out, typename... List>
using type_list_concat_t =
    typename decltype(detail::type_list_concat_<Out, List...>)::type;

namespace traits_detail {

template <decltype(auto) V>
struct value_pack_constant {
  inline static constexpr decltype(V) value = V;
};

} // namespace traits_detail

/// value_pack_size_v
///
/// The size of a value pack.
///
/// A metafunction around sizeof...(V).
template <auto... V>
inline constexpr std::size_t value_pack_size_v = sizeof...(V);

/// value_pack_size_t
///
/// The size of a value pack.
///
/// A metafunction around index_constant<sizeof...(V)>.
template <auto... V>
using value_pack_size_t = index_constant<sizeof...(V)>;

/// value_pack_element_type_t
///
/// In the value pack V..., the type of the Ith element.
template <std::size_t I, auto... V>
using value_pack_element_type_t = type_pack_element_t<I, decltype(V)...>;

/// value_pack_element_type_t
///
/// In the value pack V..., the Ith element.
template <std::size_t I, auto... V>
inline constexpr value_pack_element_type_t<I, V...> value_pack_element_v =
    type_pack_element_t<I, traits_detail::value_pack_constant<V>...>::value;

namespace traits_detail {

template <typename List>
struct value_list_traits_;
template <template <auto...> class List, auto... V>
struct value_list_traits_<List<V...>> {
  static constexpr std::size_t size = sizeof...(V);
  template <std::size_t I>
  using element_type = value_pack_element_type_t<I, V...>;
  template <std::size_t I>
  static constexpr value_pack_element_type_t<I, V...> element =
      value_pack_element_v<I, V...>;
};

} // namespace traits_detail

/// value_list_size_v
///
/// The size of a value list.
///
/// For List<V...>, equivalent to value_pack_size_v<V...>.
template <typename List>
inline constexpr std::size_t value_list_size_v =
    traits_detail::value_list_traits_<List>::size;

/// value_list_size_t
///
/// The size of a value list.
///
/// For List<V...>, equivalent to value_pack_size_t<V...>.
template <typename List>
using value_list_size_t = index_constant<value_list_size_v<List>>;

/// value_list_element_type_t
///
/// For List<V...>, the type of the Ith element.
template <std::size_t I, typename List>
using value_list_element_type_t =
    typename traits_detail::value_list_traits_<List>::template element_type<I>;

/// value_list_element_v
///
/// For List<V...>, the Ith element.
template <std::size_t I, typename List>
inline constexpr value_list_element_type_t<I, List> value_list_element_v =
    traits_detail::value_list_traits_<List>::template element<I>;

namespace detail {

// The primary template is only invoked for invalid parameters.
template <template <auto...> class Out, typename... T>
inline constexpr auto value_list_concat_ =
    error_list_concat_params_should_be_non_cvref<T...>{};

template <template <auto...> class Out>
inline constexpr type_identity<Out<>> value_list_concat_<Out>;

template <template <auto...> class Out, template <auto...> class In, auto... V>
inline constexpr auto value_list_concat_<Out, In<V...>> =
    type_identity<Out<V...>>{};

template <
    template <auto...>
    class Out,
    // Allow input lists to come from heterogeneous templates.
    template <auto...>
    class InA,
    auto... A,
    template <auto...>
    class InB,
    auto... B,
    typename... Tail>
inline constexpr auto value_list_concat_<Out, InA<A...>, InB<B...>, Tail...> =
    // The use of `vtag_t` is explained in the analogous `type_list_concat_.
    value_list_concat_<Out, vtag_t<A..., B...>, Tail...>;

} // namespace detail

/// value_list_concat_t
///
/// Each `List` is a value list of the form `InK<ValK...>`, where the
/// templates `InK` are potentially heterogeneous.  Concatenates these
/// `List`s into a single value list `Out<Val1..., Val2..., ...>`.
template <template <auto...> class Out, typename... List>
using value_list_concat_t =
    typename decltype(detail::value_list_concat_<Out, List...>)::type;

namespace detail {

template <typename V, typename... T>
constexpr std::size_t type_pack_find_() {
  bool eq[] = {std::is_same_v<V, T>..., true};
  for (size_t i = 0; i < sizeof...(T); ++i) {
    if (eq[i]) {
      return i;
    }
  }
  return sizeof...(T);
}

template <typename>
struct type_list_find_;
template <template <typename...> class List, typename... T>
struct type_list_find_<List<T...>> {
  template <typename V>
  static inline constexpr std::size_t apply = type_pack_find_<V, T...>();
};

} // namespace detail

/// type_pack_find_v
///
/// The index of the element of the type pack which is identical to the given
/// type, or the size of the pack if there is no such element.
template <typename V, typename... T>
inline constexpr std::size_t type_pack_find_v =
    detail::type_pack_find_<V, T...>();

/// type_pack_find_t
///
/// The index of the element of the type pack which is identical to the given
/// type, or the size of the pack if there is no such element.
template <typename V, typename... T>
using type_pack_find_t = index_constant<type_pack_find_v<V, T...>>;

/// type_list_find_v
///
/// The index of the element of the type list which is identical to the given
/// type, or the size of the list if there is no such element.
template <typename V, typename List>
inline constexpr std::size_t type_list_find_v =
    detail::type_list_find_<List>::template apply<V>;

/// type_list_find_t
///
/// The index of the element of the type list which is identical to the given
/// type, or the size of the list if there is no such element.
template <typename V, typename List>
using type_list_find_t = index_constant<type_list_find_v<V, List>>;

} // namespace folly
