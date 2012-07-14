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

/*
 * Defines a function folly::applyTuple, which takes a function and a
 * std::tuple of arguments and calls the function with those
 * arguments.
 *
 * Example:
 *
 *    int x = folly::applyTuple(std::plus<int>(), std::make_tuple(12, 12));
 *    ASSERT(x == 24);
 */

#ifndef FOLLY_APPLYTUPLE_H_
#define FOLLY_APPLYTUPLE_H_

#include <tuple>
#include <functional>
#include <type_traits>

namespace folly {

//////////////////////////////////////////////////////////////////////

namespace detail {

// This is to allow using this with pointers to member functions,
// where the first argument in the tuple will be the this pointer.
template<class F> F& makeCallable(F& f) { return f; }
template<class R, class C, class ...A>
auto makeCallable(R (C::*d)(A...)) -> decltype(std::mem_fn(d)) {
  return std::mem_fn(d);
}

template<class Tuple>
struct DerefSize
  : std::tuple_size<typename std::remove_reference<Tuple>::type>
{};

// CallTuple recursively unpacks tuple arguments so we can forward
// them into the function.
template<class Ret>
struct CallTuple {
  template<class F, class Tuple, class ...Unpacked>
  static typename std::enable_if<
    (sizeof...(Unpacked) < DerefSize<Tuple>::value),
    Ret
  >::type call(const F& f, Tuple&& t, Unpacked&&... unp) {
    typedef typename std::tuple_element<
      sizeof...(Unpacked),
      typename std::remove_reference<Tuple>::type
    >::type ElementType;
    return CallTuple<Ret>::call(f, std::forward<Tuple>(t),
      std::forward<Unpacked>(unp)...,
      std::forward<ElementType>(std::get<sizeof...(Unpacked)>(t))
    );
  }

  template<class F, class Tuple, class ...Unpacked>
  static typename std::enable_if<
    (sizeof...(Unpacked) == DerefSize<Tuple>::value),
    Ret
  >::type call(const F& f, Tuple&& t, Unpacked&&... unp) {
    return makeCallable(f)(std::forward<Unpacked>(unp)...);
  }
};

// The point of this meta function is to extract the contents of the
// tuple as a parameter pack so we can pass it into std::result_of<>.
template<class F, class Args> struct ReturnValue {};
template<class F, class ...Args>
struct ReturnValue<F,std::tuple<Args...>> {
  typedef typename std::result_of<F (Args...)>::type type;
};

}

//////////////////////////////////////////////////////////////////////

template<class Callable, class Tuple>
typename detail::ReturnValue<
  typename std::decay<Callable>::type,
  typename std::remove_reference<Tuple>::type
>::type
applyTuple(const Callable& c, Tuple&& t) {
  typedef typename detail::ReturnValue<
    typename std::decay<Callable>::type,
    typename std::remove_reference<Tuple>::type
  >::type RetT;
  return detail::CallTuple<RetT>::call(c, std::forward<Tuple>(t));
}

//////////////////////////////////////////////////////////////////////

}

#endif
