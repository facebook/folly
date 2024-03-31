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

#include <folly/SingletonThreadLocal.h>

using namespace folly;

namespace folly {
using Make = detail::DefaultMake<int>;
} // namespace folly

struct TLTag1 : Make {};
struct TLTag2 : Make {};
struct DeathTag {};

int stl_get_sum() {
  auto& i1 = SingletonThreadLocal<int, DeathTag, Make, TLTag1>::get();
  auto& i2 = SingletonThreadLocal<int, DeathTag, Make, TLTag2>::get();
  return i1 + i2;
}
