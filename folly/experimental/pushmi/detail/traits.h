/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <type_traits>

#include <folly/experimental/pushmi/detail/concept_def.h>

namespace folly {
namespace pushmi {

PUSHMI_CONCEPT_DEF(
    template(class T) //
    concept Object,
    requires(T* p) ( //
      *p, implicitly_convertible_to<const volatile void*>(p))//
  );

PUSHMI_CONCEPT_DEF(
    template(class T, class... Args) //
    (concept Constructible)(T, Args...),
    PUSHMI_PP_IS_CONSTRUCTIBLE(T, Args...));

PUSHMI_CONCEPT_DEF(
    template(class From, class To) //
    concept ExplicitlyConvertibleTo,
    requires(From (&f)()) ( //
      static_cast<To>(f()))
  );

PUSHMI_CONCEPT_DEF(
    template(class From, class To) //
    concept ConvertibleTo,
    ExplicitlyConvertibleTo<From, To>&& std::is_convertible<From, To>::value);

PUSHMI_CONCEPT_DEF(
    template(class T) //
    concept SemiMovable,
    Object<T>&& Constructible<T, T>&& ConvertibleTo<T, T>);

} // namespace pushmi
} // namespace folly
