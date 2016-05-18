/*
 * Copyright 2016 Facebook, Inc.
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

#include <type_traits>

namespace folly {

/// In functional programming, the degenerate case is often called "unit". In
/// C++, "void" is often the best analogue, however because of the syntactic
/// special-casing required for void it is a liability for template
/// metaprogramming. So, instead of e.g. Future<void>, we have Future<Unit>.
/// You can ignore the actual value, and we port some of the syntactic
/// niceties like setValue() instead of setValue(Unit{}).
struct Unit {
  template <typename T>
  using Lift = std::conditional<std::is_same<T, void>::value, Unit, T>;
  template <typename T>
  using Drop = std::conditional<std::is_same<T, Unit>::value, void, T>;

  bool operator==(const Unit& /*other*/) const { return true; }
  bool operator!=(const Unit& /*other*/) const { return false; }
};

constexpr Unit unit {};

}
