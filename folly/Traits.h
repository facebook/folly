/*
 * Copyright 2012 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// @author: Andrei Alexandrescu

#ifndef FOLLY_BASE_TRAITS_H_
#define FOLLY_BASE_TRAITS_H_

#include <memory>
#include <type_traits>

#include <bits/c++config.h>

#include <boost/type_traits.hpp>
#include <boost/mpl/and.hpp>
#include <boost/mpl/has_xxx.hpp>
#include <boost/mpl/not.hpp>

namespace folly {

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
 * IsTriviallyCopyable describes the value semantics property. C++11 contains
 * the type trait is_trivially_copyable; however, it is not yet implemented
 * in gcc (as of 4.7.1), and the user may wish to specify otherwise.
 */
/*
 * IsZeroInitializable describes the property that default construction is the
 * same as memset(dst, 0, sizeof(T)).
 */

namespace traits_detail {

#define FOLLY_HAS_TRUE_XXX(name)                          \
  BOOST_MPL_HAS_XXX_TRAIT_DEF(name);                      \
  template <class T> struct name ## _is_true              \
    : std::is_same<typename T::name, std::true_type> {};  \
  template <class T> struct has_true_ ## name             \
    : std::conditional<                                   \
        has_ ## name <T>::value,                          \
        name ## _is_true<T>,                              \
        std::false_type                                   \
      >:: type {};

FOLLY_HAS_TRUE_XXX(IsRelocatable)
FOLLY_HAS_TRUE_XXX(IsZeroInitializable)
FOLLY_HAS_TRUE_XXX(IsTriviallyCopyable)

#undef FOLLY_HAS_TRUE_XXX
}

template <class T> struct IsTriviallyCopyable
  : std::integral_constant<bool,
      !std::is_class<T>::value ||
      // TODO: add alternate clause is_trivially_copyable, when available
      traits_detail::has_true_IsTriviallyCopyable<T>::value
    > {};

template <class T> struct IsRelocatable
  : std::integral_constant<bool,
      !std::is_class<T>::value ||
      // TODO add this line (and some tests for it) when we upgrade to gcc 4.7
      //std::is_trivially_move_constructible<T>::value ||
      IsTriviallyCopyable<T>::value ||
      traits_detail::has_true_IsRelocatable<T>::value
    > {};

template <class T> struct IsZeroInitializable
  : std::integral_constant<bool,
      !std::is_class<T>::value ||
      traits_detail::has_true_IsZeroInitializable<T>::value
    > {};

} // namespace folly

/**
 * Use this macro ONLY inside namespace folly. When using it with a
 * regular type, use it like this:
 *
 * // Make sure you're at namespace ::folly scope
 * template<> FOLLY_ASSUME_RELOCATABLE(MyType)
 *
 * When using it with a template type, use it like this:
 *
 * // Make sure you're at namespace ::folly scope
 * template<class T1, class T2>
 * FOLLY_ASSUME_RELOCATABLE(MyType<T1, T2>)
 */
#define FOLLY_ASSUME_RELOCATABLE(...) \
  struct IsRelocatable<  __VA_ARGS__ > : std::true_type {};

/**
 * Use this macro ONLY inside namespace boost. When using it with a
 * regular type, use it like this:
 *
 * // Make sure you're at namespace ::boost scope
 * template<> FOLLY_ASSUME_HAS_NOTHROW_CONSTRUCTOR(MyType)
 *
 * When using it with a template type, use it like this:
 *
 * // Make sure you're at namespace ::boost scope
 * template<class T1, class T2>
 * FOLLY_ASSUME_HAS_NOTHROW_CONSTRUCTOR(MyType<T1, T2>)
 */
#define FOLLY_ASSUME_HAS_NOTHROW_CONSTRUCTOR(...) \
  struct has_nothrow_constructor<  __VA_ARGS__ > : ::boost::true_type {};

/**
 * The FOLLY_ASSUME_FBVECTOR_COMPATIBLE* macros below encode two
 * assumptions: first, that the type is relocatable per IsRelocatable
 * above, and that it has a nothrow constructor. Most types can be
 * assumed to satisfy both conditions, but it is the responsibility of
 * the user to state that assumption. User-defined classes will not
 * work with fbvector (see FBVector.h) unless they state this
 * combination of properties.
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
#define FOLLY_ASSUME_FBVECTOR_COMPATIBLE(...)                           \
  namespace folly { template<> FOLLY_ASSUME_RELOCATABLE(__VA_ARGS__) }   \
  namespace boost { \
  template<> FOLLY_ASSUME_HAS_NOTHROW_CONSTRUCTOR(__VA_ARGS__) }
// Use this macro ONLY at global level (no namespace)
#define FOLLY_ASSUME_FBVECTOR_COMPATIBLE_1(...)                         \
  namespace folly {                                                     \
  template <class T1> FOLLY_ASSUME_RELOCATABLE(__VA_ARGS__<T1>) }       \
    namespace boost {                                                   \
    template <class T1> FOLLY_ASSUME_HAS_NOTHROW_CONSTRUCTOR(__VA_ARGS__<T1>) }
// Use this macro ONLY at global level (no namespace)
#define FOLLY_ASSUME_FBVECTOR_COMPATIBLE_2(...)                 \
  namespace folly {                                             \
  template <class T1, class T2>                                 \
  FOLLY_ASSUME_RELOCATABLE(__VA_ARGS__<T1, T2>) }               \
    namespace boost {                                           \
    template <class T1, class T2>                               \
    FOLLY_ASSUME_HAS_NOTHROW_CONSTRUCTOR(__VA_ARGS__<T1, T2>) }
// Use this macro ONLY at global level (no namespace)
#define FOLLY_ASSUME_FBVECTOR_COMPATIBLE_3(...)                         \
  namespace folly {                                                     \
  template <class T1, class T2, class T3>                               \
  FOLLY_ASSUME_RELOCATABLE(__VA_ARGS__<T1, T2, T3>) }                   \
    namespace boost {                                                   \
    template <class T1, class T2, class T3>                             \
    FOLLY_ASSUME_HAS_NOTHROW_CONSTRUCTOR(__VA_ARGS__<T1, T2, T3>) }
// Use this macro ONLY at global level (no namespace)
#define FOLLY_ASSUME_FBVECTOR_COMPATIBLE_4(...)                         \
  namespace folly {                                                     \
  template <class T1, class T2, class T3, class T4>                     \
  FOLLY_ASSUME_RELOCATABLE(__VA_ARGS__<T1, T2, T3, T4>) }               \
    namespace boost {                                                   \
    template <class T1, class T2, class T3, class T4>                   \
    FOLLY_ASSUME_HAS_NOTHROW_CONSTRUCTOR(__VA_ARGS__<T1, T2, T3, T4>) }

/**
 * Instantiate FOLLY_ASSUME_FBVECTOR_COMPATIBLE for a few types. It is
 * safe to assume that pair is compatible if both of its components
 * are. Furthermore, all STL containers can be assumed to comply,
 * although that is not guaranteed by the standard.
 */

namespace std {

template <class T, class U>
  struct pair;
#ifndef _GLIBCXX_USE_FB
template <class T, class R, class A>
  class basic_string;
#else
template <class T, class R, class A, class S>
  class basic_string;
#endif
template <class T, class A>
  class vector;
template <class T, class A>
  class deque;
template <class T, class A>
  class list;
template <class T, class C, class A>
  class set;
template <class K, class V, class C, class A>
  class map;
template <class T>
  class shared_ptr;

}

namespace boost {

template <class T> class shared_ptr;

template <class T, class U>
struct has_nothrow_constructor< std::pair<T, U> >
    : ::boost::mpl::and_< has_nothrow_constructor<T>,
                          has_nothrow_constructor<U> > {};

} // namespace boost

namespace folly {

// STL commonly-used types
template <class T, class U>
struct IsRelocatable<  std::pair<T, U> >
    : ::boost::mpl::and_< IsRelocatable<T>, IsRelocatable<U> > {};

// Is T one of T1, T2, ..., Tn?
template <class T, class... Ts>
struct IsOneOf {
  enum { value = false };
};

template <class T, class T1, class... Ts>
struct IsOneOf<T, T1, Ts...> {
  enum { value = std::is_same<T, T1>::value || IsOneOf<T, Ts...>::value };
};

} // namespace folly

FOLLY_ASSUME_FBVECTOR_COMPATIBLE_3(std::basic_string);
FOLLY_ASSUME_FBVECTOR_COMPATIBLE_2(std::vector);
FOLLY_ASSUME_FBVECTOR_COMPATIBLE_2(std::list);
FOLLY_ASSUME_FBVECTOR_COMPATIBLE_2(std::deque);
FOLLY_ASSUME_FBVECTOR_COMPATIBLE_4(std::map);
FOLLY_ASSUME_FBVECTOR_COMPATIBLE_3(std::set);
FOLLY_ASSUME_FBVECTOR_COMPATIBLE_2(std::unique_ptr);
FOLLY_ASSUME_FBVECTOR_COMPATIBLE_1(std::shared_ptr);
FOLLY_ASSUME_FBVECTOR_COMPATIBLE_1(std::function);

// Boost
FOLLY_ASSUME_FBVECTOR_COMPATIBLE_1(boost::shared_ptr);

#endif //FOLLY_BASE_TRAITS_H_
