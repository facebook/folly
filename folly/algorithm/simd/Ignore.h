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

#include <folly/lang/Bits.h>

#include <type_traits>

namespace folly::simd {

/**
 * ignore(_none/_extrema)
 *
 * tag types to be used in some simd operations.
 *
 * They are used to indicate to the function that
 * some of the elements in are garbage.
 *
 * ignore_none indicates that the whole register is used.
 * ignore_extrema.first, .last show how many elements are out of the data.
 *
 * Example:
 * register: [true, true, false, false, false, false, false, true]
 * indexes   [0,    1,    2,     3,     4,     5,     6,     7   ]
 *
 * ignore_extema{.first = 1, .last = 2}
 * means that elements with indexes 0, 6, and 7 will be ignored
 * (w/e that means for an operation)
 */

struct ignore_extrema {
  int first = 0;
  int last = 0;
};

struct ignore_none {};

} // namespace folly::simd
