/*
 * Copyright 2018-present Facebook, Inc.
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

#include <folly/container/detail/F14Defaults.h>

namespace folly {
template <
    typename K,
    typename H = f14::DefaultHasher<K>,
    typename E = f14::DefaultKeyEqual<K>,
    typename A = f14::DefaultAlloc<K>>
class F14NodeSet;

template <
    typename K,
    typename H = f14::DefaultHasher<K>,
    typename E = f14::DefaultKeyEqual<K>,
    typename A = f14::DefaultAlloc<K>>
class F14ValueSet;

template <
    typename K,
    typename H = f14::DefaultHasher<K>,
    typename E = f14::DefaultKeyEqual<K>,
    typename A = f14::DefaultAlloc<K>>
class F14VectorSet;

template <
    typename K,
    typename H = f14::DefaultHasher<K>,
    typename E = f14::DefaultKeyEqual<K>,
    typename A = f14::DefaultAlloc<K>>
class F14FastSet;

} // namespace folly
