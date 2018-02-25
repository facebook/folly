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

#include <folly/compression/Counters.h>
#include <folly/portability/Config.h>

namespace folly {
#if FOLLY_HAVE_WEAK_SYMBOLS
#define FOLLY_WEAK_SYMBOL __attribute__((__weak__))
#else
#define FOLLY_WEAK_SYMBOL
#endif

folly::Function<void(double)> FOLLY_WEAK_SYMBOL makeCompressionCounterHandler(
    folly::io::CodecType,
    folly::StringPiece,
    const folly::Optional<int>&,
    CompressionCounterKey,
    CompressionCounterType) {
  return {};
}
} // namespace folly
