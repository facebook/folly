/*
 * Copyright 2015 Facebook, Inc.
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
namespace folly {

/// In functional programming, the degenerate case is often called "unit". In
/// C++, "void" is often the best analogue, however because of the syntactic
/// special-casing required for void it is a liability for template
/// metaprogramming. So, instead of e.g. Future<void>, we have Future<Unit>.
/// You can ignore the actual value, and we port some of the syntactic
/// niceties like setValue() instead of setValue(Unit{}).
// We will soon return Future<Unit> wherever we currently return Future<void>
// #6847876
struct Unit {
  /// Lift type T into Unit. This is the definition for all non-void types.
  template <class T> struct Lift : public std::false_type {
    using type = T;
  };
};

// Lift void into Unit.
template <>
struct Unit::Lift<void> : public std::true_type {
  using type = Unit;
};

// Lift Unit into Unit (identity).
template <>
struct Unit::Lift<Unit> : public std::true_type {
  using type = Unit;
};

template <class T>
struct is_void_or_unit : public Unit::Lift<T>
{};

}
