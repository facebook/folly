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

struct Unit {
  template <class T> struct Lift : public std::false_type {
    using type = T;
  };
};

template <>
struct Unit::Lift<void> : public std::true_type {
  using type = Unit;
};

template <class T>
struct is_void_or_unit : public std::conditional<
  std::is_void<T>::value || std::is_same<Unit, T>::value,
  std::true_type,
  std::false_type>::type
{};

}
