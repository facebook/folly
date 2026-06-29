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

#include <folly/debugging/symbolizer/test/OmitFramePointerFunc.h>

#include <folly/lang/Hint.h>

namespace folly {
namespace symbolizer {
namespace test {

FOLLY_NOINLINE void omitFpMiddle(void (*next)()) {
  next();
  compiler_must_not_elide(0); // prevent tail-call optimization
}

} // namespace test
} // namespace symbolizer
} // namespace folly
