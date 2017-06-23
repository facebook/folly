/*
 * Copyright 2017 Facebook, Inc.
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

#pragma once

#include <cstdint>
#include <type_traits>
#include <utility>

namespace folly {

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
 *      std::copy(std::move(data)); // force-move, not just a cast to &&
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
constexpr typename std::decay<T>::type copy(T&& value) noexcept(
    noexcept(typename std::decay<T>::type(std::forward<T>(value)))) {
  return std::forward<T>(value);
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
#if __cpp_lib_as_const || _MSC_VER

/* using override */ using std::as_const;

#else

template <class T>
constexpr T const& as_const(T& t) noexcept {
  return t;
}

template <class T>
void as_const(T const&&) = delete;

#endif

#if __cpp_lib_integer_sequence || _MSC_VER

/* using override */ using std::integer_sequence;
/* using override */ using std::index_sequence;
/* using override */ using std::make_index_sequence;

#else

template <class T, T... Ints>
struct integer_sequence {
  using value_type = T;

  static constexpr std::size_t size() noexcept {
    return sizeof...(Ints);
  }
};

template <std::size_t... Ints>
using index_sequence = folly::integer_sequence<std::size_t, Ints...>;

namespace detail {
template <std::size_t N, std::size_t... Ints>
struct make_index_sequence
    : detail::make_index_sequence<N - 1, N - 1, Ints...> {};

template <std::size_t... Ints>
struct make_index_sequence<0, Ints...> : folly::index_sequence<Ints...> {};
}

template <std::size_t N>
using make_index_sequence = detail::make_index_sequence<N>;

#endif

/**
 * A simple function object that passes its argument through unchanged.
 *
 * Example:
 *
 *   int i = 42;
 *   int &j = Identity()(i);
 *   assert(&i == &j);
 *
 * Warning: passing a prvalue through Identity turns it into an xvalue,
 * which can effect whether lifetime extension occurs or not. For instance:
 *
 *   auto&& x = std::make_unique<int>(42);
 *   cout << *x ; // OK, x refers to a valid unique_ptr.
 *
 *   auto&& y = Identity()(std::make_unique<int>(42));
 *   cout << *y ; // ERROR: y did not lifetime-extend the unique_ptr. It
 *                // is no longer valid
 */
struct Identity {
  using is_transparent = void;
  template <class T>
  constexpr T&& operator()(T&& x) const noexcept {
    return static_cast<T&&>(x);
  }
};

namespace moveonly_ { // Protection from unintended ADL.

/**
 * Disallow copy but not move in derived types. This is essentially
 * boost::noncopyable (the implementation is almost identical) but it
 * doesn't delete move constructor and move assignment.
 */
class MoveOnly {
 protected:
  constexpr MoveOnly() = default;
  ~MoveOnly() = default;

  MoveOnly(MoveOnly&&) = default;
  MoveOnly& operator=(MoveOnly&&) = default;
  MoveOnly(const MoveOnly&) = delete;
  MoveOnly& operator=(const MoveOnly&) = delete;
};

} // namespace moveonly_

using MoveOnly = moveonly_::MoveOnly;

} // namespace folly
