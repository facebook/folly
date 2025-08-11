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

#include <memory>

#include <folly/futures/Future.h>
#include <folly/portability/GTest.h>

namespace folly {

using A = std::unique_ptr<int>;
struct B {};

template <class T>
using EnableIfFuture = typename std::enable_if<isFuture<T>::value>::type;

template <class T>
using EnableUnlessFuture = typename std::enable_if<!isFuture<T>::value>::type;

template <class T>
Future<T> someFuture() {
  return makeFuture(T());
}

template <class Ret, class... Params>
typename std::enable_if<isFuture<Ret>::value, Ret>::type aFunction(Params...) {
  using T = typename Ret::value_type;
  return makeFuture(T());
}

template <class Ret, class... Params>
typename std::enable_if<!isFuture<Ret>::value, Ret>::type aFunction(Params...) {
  return Ret();
}

template <class Ret, class... Params>
std::function<Ret(Params...)> aStdFunction(
    typename std::enable_if<!isFuture<Ret>::value, bool>::type = false) {
  return [](Params...) -> Ret { return Ret(); };
}

template <class Ret, class... Params>
std::function<Ret(Params...)> aStdFunction(
    typename std::enable_if<isFuture<Ret>::value, bool>::type = true) {
  using T = typename Ret::value_type;
  return [](Params...) -> Future<T> { return makeFuture(T()); };
}

class SomeClass {
 public:
  template <class Ret, class... Params>
  static typename std::enable_if<!isFuture<Ret>::value, Ret>::type
  aStaticMethod(Params...) {
    return Ret();
  }

  template <class Ret, class... Params>
  static typename std::enable_if<isFuture<Ret>::value, Ret>::type aStaticMethod(
      Params...) {
    using T = typename Ret::value_type;
    return makeFuture(T());
  }

  template <class Ret, class... Params>
  typename std::enable_if<!isFuture<Ret>::value, Ret>::type aMethod(Params...) {
    return Ret();
  }

  template <class Ret, class... Params>
  typename std::enable_if<isFuture<Ret>::value, Ret>::type aMethod(Params...) {
    using T = typename Ret::value_type;
    return makeFuture(T());
  }
};

} // namespace folly
