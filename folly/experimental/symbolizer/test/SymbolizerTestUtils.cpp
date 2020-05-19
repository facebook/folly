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

#include <folly/experimental/symbolizer/test/SymbolizerTestUtils.h>

namespace folly {
namespace symbolizer {
namespace test {

void* framesToFill = {nullptr};

size_t kQsortCallLineNo = 0;
size_t kFooCallByStandaloneBarLineNo = 0;
size_t kFooCallByStandaloneBazLineNo = 0;
size_t kFooCallByClassBarLineNo = 0;
size_t kFooCallByClassStaticBarLineNo = 0;
size_t kFooCallByClassInDifferentFileBarLineNo = 0;
size_t kFooCallByClassInDifferentFileStaticBarLineNo = 0;
size_t kInlineBarCallByLexicalBarLineNo = 0;
size_t kInlineBazCallByLexicalBazLineNo = 0;

void inlineBaz(FrameArray<100>& frames) {
  kFooCallByStandaloneBazLineNo = __LINE__ + 1;
  inlineFoo(frames);
}

} // namespace test
} // namespace symbolizer
} // namespace folly
