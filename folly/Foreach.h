/*
 * Copyright 2013 Facebook, Inc.
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

#ifndef FOLLY_BASE_FOREACH_H_
#define FOLLY_BASE_FOREACH_H_

/*
 * Iterim macros (until we have C++0x range-based for) that simplify
 * writing loops of the form
 *
 * for (Container<data>::iterator i = c.begin(); i != c.end(); ++i) statement
 *
 * Just replace the above with:
 *
 * FOR_EACH (i, c) statement
 *
 * and everything is taken care of.
 *
 * The implementation is a bit convoluted to make sure the container is
 * only evaluated once (however, keep in mind that c.end() is evaluated
 * at every pass through the loop). To ensure the container is not
 * evaluated multiple times, the macro defines one do-nothing if
 * statement to inject the Boolean variable FOR_EACH_state1, and then a
 * for statement that is executed only once, which defines the variable
 * FOR_EACH_state2 holding a reference to the container being
 * iterated. The workhorse is the last loop, which uses the just defined
 * reference FOR_EACH_state2.
 *
 * The state variables are nested so they don't interfere; you can use
 * FOR_EACH multiple times in the same scope, either at the same level or
 * nested.
 *
 * In optimized builds g++ eliminates the extra gymnastics entirely and
 * generates code 100% identical to the handwritten loop.
 *
 * This will not work with temporary containers.  Consider BOOST_FOREACH
 * if you need that.
 */

#include <boost/type_traits/remove_cv.hpp>

namespace folly { namespace detail {

/*
 * Simple template for obtaining the unqualified type given a generic
 * type T. For example, if T is const int,
 * typeof(remove_cv_from_expression(T())) yields int. Due to a bug in
 * g++, you need to actually use
 * typeof(remove_cv_from_expression(T())) instead of typename
 * boost::remove_cv<T>::type. Note that the function
 * remove_cv_from_expression is never defined - use it only inside
 * typeof.
 */
template <class T> typename boost::remove_cv<T>::type
remove_cv_from_expression(T value);

}}

/*
 * Use a "reference reference" (auto&&) to take advantage of reference
 * collapsing rules, if available.  In this case, FOR_EACH* will work with
 * temporary containers.
 */
#define FB_AUTO_RR(x, y) auto&& x = y

/*
 * The first AUTO should be replaced by decltype((c)) &
 * FOR_EACH_state2, but bugs in gcc prevent that from functioning
 * properly. The second pair of parens in decltype is actually
 * required, see
 * cpp-next.com/archive/2011/04/appearing-and-disappearing-consts-in-c/
 */
#define FOR_EACH(i, c)                              \
  if (bool FOR_EACH_state1 = false) {} else         \
    for (auto & FOR_EACH_state2 = (c);              \
         !FOR_EACH_state1; FOR_EACH_state1 = true)  \
      for (auto i = FOR_EACH_state2.begin();        \
           i != FOR_EACH_state2.end(); ++i)

/*
 * Similar to FOR_EACH, but iterates the container backwards by
 * using rbegin() and rend().
 */
#define FOR_EACH_R(i, c)                                \
  if (bool FOR_EACH_R_state1 = false) {} else           \
    for (auto & FOR_EACH_R_state2 = (c);                \
         !FOR_EACH_R_state1; FOR_EACH_R_state1 = true)  \
      for (auto i = FOR_EACH_R_state2.rbegin();         \
           i != FOR_EACH_R_state2.rend(); ++i)

/*
 * Similar to FOR_EACH but also allows client to specify a 'count' variable
 * to track the current iteration in the loop (starting at zero).
 * Similar to python's enumerate() function.  For example:
 * string commaSeparatedValues = "VALUES: ";
 * FOR_EACH_ENUMERATE(ii, value, columns) {   // don't want comma at the end!
 *   commaSeparatedValues += (ii == 0) ? *value : string(",") + *value;
 * }
 */
#define FOR_EACH_ENUMERATE(count, i, c)                                \
  if (bool FOR_EACH_state1 = false) {} else                            \
    for (auto & FOR_EACH_state2 = (c);                                 \
         !FOR_EACH_state1; FOR_EACH_state1 = true)                     \
      if (size_t FOR_EACH_privateCount = 0) {} else                    \
        if (const size_t& count = FOR_EACH_privateCount) {} else       \
          for (auto i = FOR_EACH_state2.begin();                       \
               i != FOR_EACH_state2.end(); ++FOR_EACH_privateCount, ++i)

/**
 * Similar to FOR_EACH, but gives the user the key and value for each entry in
 * the container, instead of just the iterator to the entry. For example:
 *   map<string, string> testMap;
 *   FOR_EACH_KV(key, value, testMap) {
 *      cout << key << " " << value;
 *   }
 */
#define FOR_EACH_KV(k, v, c)                                    \
  if (unsigned int FOR_EACH_state1 = 0) {} else                 \
    for (FB_AUTO_RR(FOR_EACH_state2, (c));                      \
         !FOR_EACH_state1; FOR_EACH_state1 = 1)                 \
      for (auto FOR_EACH_state3 = FOR_EACH_state2.begin();      \
           FOR_EACH_state3 != FOR_EACH_state2.end();            \
           FOR_EACH_state1 == 2                                 \
             ? ((FOR_EACH_state1 = 0), ++FOR_EACH_state3)       \
             : (FOR_EACH_state3 = FOR_EACH_state2.end()))       \
        for (auto &k = FOR_EACH_state3->first;                  \
             !FOR_EACH_state1; ++FOR_EACH_state1)               \
          for (auto &v = FOR_EACH_state3->second;               \
               !FOR_EACH_state1; ++FOR_EACH_state1)

namespace folly { namespace detail {

// Boost 1.48 lacks has_less, we emulate a subset of it here.
template <typename T, typename U>
class HasLess {
  struct BiggerThanChar { char unused[2]; };
  template <typename C, typename D> static char test(decltype(C() < D())*);
  template <typename, typename> static BiggerThanChar test(...);
public:
  enum { value = sizeof(test<T, U>(0)) == 1 };
};

/**
 * notThereYet helps the FOR_EACH_RANGE macro by opportunistically
 * using "<" instead of "!=" whenever available when checking for loop
 * termination. This makes e.g. examples such as FOR_EACH_RANGE (i,
 * 10, 5) execute zero iterations instead of looping virtually
 * forever. At the same time, some iterator types define "!=" but not
 * "<". The notThereYet function will dispatch differently for those.
 *
 * Below is the correct implementation of notThereYet. It is disabled
 * because of a bug in Boost 1.46: The filesystem::path::iterator
 * defines operator< (via boost::iterator_facade), but that in turn
 * uses distance_to which is undefined for that particular
 * iterator. So HasLess (defined above) identifies
 * boost::filesystem::path as properly comparable with <, but in fact
 * attempting to do so will yield a compile-time error.
 *
 * The else branch (active) contains a conservative
 * implementation.
 */

#if 0

template <class T, class U>
typename std::enable_if<HasLess<T, U>::value, bool>::type
notThereYet(T& iter, const U& end) {
  return iter < end;
}

template <class T, class U>
typename std::enable_if<!HasLess<T, U>::value, bool>::type
notThereYet(T& iter, const U& end) {
  return iter != end;
}

#else

template <class T, class U>
typename std::enable_if<
  (std::is_arithmetic<T>::value && std::is_arithmetic<U>::value) ||
  (std::is_pointer<T>::value && std::is_pointer<U>::value),
  bool>::type
notThereYet(T& iter, const U& end) {
  return iter < end;
}

template <class T, class U>
typename std::enable_if<
  !(
    (std::is_arithmetic<T>::value && std::is_arithmetic<U>::value) ||
    (std::is_pointer<T>::value && std::is_pointer<U>::value)
  ),
  bool>::type
notThereYet(T& iter, const U& end) {
  return iter != end;
}

#endif


/**
 * downTo is similar to notThereYet, but in reverse - it helps the
 * FOR_EACH_RANGE_R macro.
 */
template <class T, class U>
typename std::enable_if<HasLess<U, T>::value, bool>::type
downTo(T& iter, const U& begin) {
  return begin < iter--;
}

template <class T, class U>
typename std::enable_if<!HasLess<U, T>::value, bool>::type
downTo(T& iter, const U& begin) {
  if (iter == begin) return false;
  --iter;
  return true;
}

} }

/*
 * Iteration with given limits. end is assumed to be reachable from
 * begin. end is evaluated every pass through the loop.
 *
 * NOTE: The type of the loop variable should be the common type of "begin"
 *       and "end". e.g. If "begin" is "int" but "end" is "long", we want "i"
 *       to be "long". This is done by getting the type of (true ? begin : end)
 */
#define FOR_EACH_RANGE(i, begin, end)           \
  for (auto i = (true ? (begin) : (end));       \
       ::folly::detail::notThereYet(i, (end));  \
       ++i)

/*
 * Iteration with given limits. begin is assumed to be reachable from
 * end by successive decrements. begin is evaluated every pass through
 * the loop.
 *
 * NOTE: The type of the loop variable should be the common type of "begin"
 *       and "end". e.g. If "begin" is "int" but "end" is "long", we want "i"
 *       to be "long". This is done by getting the type of (false ? begin : end)
 */
#define FOR_EACH_RANGE_R(i, begin, end) \
  for (auto i = (false ? (begin) : (end)); ::folly::detail::downTo(i, (begin));)

#endif
