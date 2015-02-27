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

#ifndef FOLLY_GEN_CORE_H
#define FOLLY_GEN_CORE_H

namespace folly { namespace gen {

template<class Value, class Self>
class GenImpl;

template<class Self>
class Operator;

namespace detail {

template<class Self>
struct FBounded;

template<class First, class Second>
class Composed;

template<class Value, class First, class Second>
class Chain;

} // detail

}} // folly::gen

#include <folly/gen/Core-inl.h>

#endif // FOLLY_GEN_CORE_H
