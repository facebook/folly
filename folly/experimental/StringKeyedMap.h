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

// @author: Pavlo Kushnir (pavlo)

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace folly {

template <
    class Value,
    class Compare = std::less<void>,
    class Alloc = std::allocator<std::pair<const std::string, Value>>>
using StringKeyedMap = std::map<std::string, Value, Compare, Alloc>;

} // namespace folly
