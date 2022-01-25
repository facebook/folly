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

#include <folly/Portability.h>
#include <folly/lang/Hint.h>

namespace folly {

/**
 * Call doNotOptimizeAway(var) to ensure that var will be computed even
 * post-optimization.  Use it for variables that are computed during
 * benchmarking but otherwise are useless. The compiler tends to do a
 * good job at eliminating unused variables, and this function fools it
 * into thinking var is in fact needed.
 *
 * Call makeUnpredictable(var) when you don't want the optimizer to use
 * its knowledge of var to shape the following code.  This is useful
 * when constant propagation or power reduction is possible during your
 * benchmark but not in real use cases.
 */

template <class T>
FOLLY_ALWAYS_INLINE void doNotOptimizeAway(const T& datum) {
  compiler_must_not_elide(datum);
}

template <typename T>
FOLLY_ALWAYS_INLINE void makeUnpredictable(T& datum) {
  compiler_must_not_predict(datum);
}

} // namespace folly
